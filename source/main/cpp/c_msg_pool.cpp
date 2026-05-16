#include "ccore/c_target.h"
#include "cbase/c_allocator.h"
#include "cbase/c_integer.h"

#include "cactor/private/c_msg_pool.h"
#include "cactor/private/c_message.h"

#include <atomic>
#include <cstdint>
#include <cstdlib>

namespace ncore
{
    namespace nactor
    {
        // Hidden internal definition of the lock-free message pool
        struct msg_pool_t
        {
            std::atomic<msg_t*> head;
        };

        // --- Initialization Mechanics ---

        msg_pool_t* msg_pool_create()
        {
            // Allocation only during startup initialization
            msg_pool_t* pool = (msg_pool_t*)malloc(sizeof(msg_pool_t));
            if (pool != nullptr)
            {
                pool->head.store(nullptr, std::memory_order_relaxed);
            }
            return pool;
        }

        void msg_pool_destroy(msg_pool_t* pool)
        {
            if (pool != nullptr)
            {
                free(pool);
            }
        }

        // Links a blocks of preallocated messages straight to the pool
        void msg_pool_populate(msg_pool_t* pool, msg_t* storage_array, size_t capacity)
        {
            if (capacity == 0 || storage_array == nullptr)
                return;

            // Chain the block elements together sequentially
            for (size_t i = 0; i < capacity - 1; ++i)
            {
                storage_array[i].m_next = &storage_array[i + 1];
            }
            storage_array[capacity - 1].m_next = nullptr;

            // Initialize head to point to the first node of our new storage contiguous array
            pool->head.store(&storage_array[0], std::memory_order_relaxed);
        }

        // --- Lock-free Stack Operations ---

        msg_t* msg_pool_pop(msg_pool_t* pool)
        {
            msg_t* old_head = pool->head.load(std::memory_order_acquire);

            // Compare-and-swap loop to cleanly pop from the top of the stack
            do
            {
                if (old_head == nullptr)
                {
                    return nullptr; // Pool is temporarily exhausted!
                }
            } while (!pool->head.compare_exchange_weak(old_head, old_head->m_next, std::memory_order_release, std::memory_order_acquire));

            old_head->m_next = nullptr; // Clear tracking reference before returning pointer
            return old_head;
        }

        void msg_pool_push(msg_pool_t* pool, msg_t* msg)
        {
            if (msg == nullptr)
                return;

            msg_t* old_head = pool->head.load(std::memory_order_relaxed);

            // Lock-free atomic push loop
            do
            {
                msg->m_next = old_head;
            } while (!pool->head.compare_exchange_weak(old_head, msg, std::memory_order_release, std::memory_order_relaxed));
        }

    } // namespace nactor
} // namespace ncore
