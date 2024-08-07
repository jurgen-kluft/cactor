#include "ccore/c_target.h"
#include "cbase/c_allocator.h"
#include "cbase/c_integer.h"
#include "cactor/private/c_queue.h"
#include "cactor/private/c_sema.h"

#include <atomic>
#include <cassert>
#include <stdexcept>

namespace ncore
{
    namespace mpmc
    {
        static constexpr int_t c_cacheline_size = 64; // std::hardware_destructive_interference_size;
        static constexpr u32   c_item_size      = 8;

        struct slot_t
        {
            std::atomic<s32> turn;
            s32              dummy;
            u64              item;
            // storage ....
            s64 padding[6];
        };

        class queue_t
        {
        public:
            explicit queue_t(slot_t* array, u32 array_size, sys_sema_t* semaphore)
                : m_producer(array, array_size)
                , m_consumer(array, array_size)
            {
                m_producer.m_semaphore = semaphore;
                m_consumer.m_semaphore = semaphore;

                static_assert(sizeof(slot_t) == c_cacheline_size, "sizeof(slot_t) must be cache line size");
                static_assert(sizeof(header_t) == c_cacheline_size, "sizeof(header_t) must be cache line size");
                static_assert(sizeof(queue_t) == 2 * c_cacheline_size, "sizeof(queue_t) must be a multiple of cache line size to prevent false sharing between adjacent queues");
                static_assert(offsetof(queue_t, m_consumer.m_index) - offsetof(queue_t, m_producer.m_index) == static_cast<std::ptrdiff_t>(c_cacheline_size), "head and tail must be a cache line apart to prevent false sharing");
            }

            DCORE_CLASS_PLACEMENT_NEW_DELETE

            bool try_push(u64 item) noexcept
            {
                auto head = m_producer.m_index.load(std::memory_order_acquire);
                for (;;)
                {
                    slot_t* slot = m_producer.m_slots + m_producer.idx(head);

                    if (m_producer.turn(head) * 2 == slot->turn.load(std::memory_order_acquire))
                    {
                        if (m_producer.m_index.compare_exchange_strong(head, head + 1))
                        {
                            slot->item = item;
                            slot->turn.store(m_producer.turn(head) * 2 + 1, std::memory_order_release);
                            sema_post(m_producer.m_semaphore); // signal consumer that there is a new item
                            return true;
                        }
                    }
                    else
                    {
                        auto const prevHead = head;
                        head                = m_producer.m_index.load(std::memory_order_acquire);
                        if (head == prevHead)
                        {
                            // The queue is full?
                            return false;
                        }
                    }
                }
            }

            bool try_pop(u64& item) noexcept
            {
                sema_wait(m_consumer.m_semaphore);

                auto tail = m_consumer.m_index.load(std::memory_order_acquire);
                for (;;)
                {
                    slot_t* slot = m_consumer.m_slots + m_consumer.idx(tail);
                    if (m_consumer.turn(tail) * 2 + 1 == slot->turn.load(std::memory_order_acquire))
                    {
                        if (m_consumer.m_index.compare_exchange_strong(tail, tail + 1))
                        {
                            item = slot->item;
                            slot->turn.store(m_consumer.turn(tail) * 2 + 2, std::memory_order_release);
                            return true;
                        }
                    }
                    else
                    {
                        auto const prevTail = tail;
                        tail                = m_consumer.m_index.load(std::memory_order_acquire);
                        if (tail == prevTail)
                        {
                            ASSERTS(false, "the queue seems to be empty, however the semaphore was signaled, this situation should not happen, check the semaphore implementation");
                            // return false;
                        }
                    }
                }
            }

            // Returns the number of elements in the queue.
            // The size can be negative when the queue is empty and there is at least one
            // reader waiting. Since this is a concurrent queue the size is only a best
            // effort guess until all reader and writer threads have been joined.
            s32 size() const
            {
                s32 const n = (m_producer.m_index.load(std::memory_order_relaxed) - m_consumer.m_index.load(std::memory_order_relaxed));
                return n < 0 ? (n + m_producer.m_capacity) : n;
            }

            struct header_t
            {
                header_t(slot_t* slots, s32 capacity)
                    : m_index(0)
                    , m_pad0(0)
                    , m_slots(slots)
                    , m_capacity((capacity / c_item_size) - 1)
                    , m_semaphore(nullptr)
                {
                }

                constexpr s32 idx(s32 i) const noexcept { return i % m_capacity; }
                constexpr s32 turn(s32 i) const noexcept { return i / m_capacity; }

                std::atomic<s32> m_index;
                s32 const        m_pad0;
                slot_t* const    m_slots;
                sys_sema_t*      m_semaphore;
                s32 const        m_capacity;
                s32              m_padding[16 - 7];
            };

            // Align to avoid false sharing between head and tail
            header_t m_producer; // head
            header_t m_consumer; // tail
        };
    } // namespace mpmc

    struct mpmc_queue_blocking_t
    {
    };

    mpmc_queue_blocking_t* mpmc_queue_create(alloc_t* allocator, s32 item_count)
    {
        s32 const array_size = (item_count + 1) * sizeof(mpmc::slot_t);
        void*     mem        = allocator->allocate(sizeof(mpmc::queue_t) + array_size, mpmc::c_cacheline_size);
        if (mem == nullptr)
            return nullptr;
        sys_sema_t*   semaphore  = create_sys_sema(allocator, 0, 0);
        mpmc::slot_t* array_data = (mpmc::slot_t*)((byte*)mem + sizeof(mpmc::queue_t));
        ASSERTS(math::isAligned((int_t)array_data, mpmc::c_cacheline_size), "array must be aligned to cache line boundary to prevent false sharing");
        mpmc::queue_t* queue = new (mem) mpmc::queue_t(array_data, array_size, semaphore);
        return (mpmc_queue_blocking_t*)queue;
    }

    void queue_destroy(alloc_t* allocator, mpmc_queue_blocking_t* queue)
    {
        mpmc::queue_t* mpmc_queue = (mpmc::queue_t*)queue;
        sema_destroy(allocator, mpmc_queue->m_producer.m_semaphore);
        allocator->deallocate(mpmc_queue->m_producer.m_slots);
        allocator->deallocate(mpmc_queue);
    }

    bool queue_enqueue(mpmc_queue_blocking_t* queue, u64 item)
    {
        mpmc::queue_t* mpmc_queue = (mpmc::queue_t*)queue;
        return mpmc_queue->try_push(item);
    }

    bool queue_dequeue(mpmc_queue_blocking_t* queue, u64& item)
    {
        mpmc::queue_t* mpmc_queue = (mpmc::queue_t*)queue;
        return mpmc_queue->try_pop(item);
    }

} // namespace ncore
