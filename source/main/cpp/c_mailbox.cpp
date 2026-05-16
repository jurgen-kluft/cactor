#include "ccore/c_target.h"
#include "cbase/c_allocator.h"
#include "cbase/c_integer.h"

#include "cactor/private/c_mailbox.h"
#include "cactor/private/c_message.h"

#include <atomic>
#include <cstdint>

namespace ncore
{
    namespace nactor
    {
        enum mailboxstate_t
        {
            MAILBOX_IDLE         = 0,
            MAILBOX_SCHEDULED    = 1,
            MAILBOX_HAS_NEW_WORK = 2
        };

        struct mailbox_t
        {
            std::atomic<msg_t*> inbound; // MPSC Shared Input Stack
            std::atomic<u32>    state;   // State Machine Flag
        };

        void mailbox_init(mailbox_t* mbox)
        {
            mbox->inbound.store(nullptr, std::memory_order_relaxed);
            mbox->state.store(MAILBOX_IDLE, std::memory_order_relaxed);
        }

        mailbox_t* mailbox_create()
        {
            // TODO pass arena to this function
            mailbox_t* mbox = (mailbox_t*)malloc(sizeof(mailbox_t));
            if (mbox != nullptr)
            {
                mailbox_init(mbox);
            }
            return mbox;
        }

        void mailbox_destroy(mailbox_t* mbox)
        {
            // Caller must ensure all messages are processed and memory freed before destroying the mailbox
        }

        bool mailbox_push(mailbox_t* mbox, msg_t* msg)
        {
            // 1. Lock-free LIFO atomic push to the inbound stack
            msg_t* old_head = mbox->inbound.load(std::memory_order_relaxed);
            do
            {
                msg->m_next = old_head;
            } while (!mbox->inbound.compare_exchange_weak(old_head, msg, std::memory_order_release, std::memory_order_relaxed));

            // 2. Atomic state transition loop
            while (true)
            {
                u32 current_state = mbox->state.load(std::memory_order_relaxed);

                if (current_state == MAILBOX_IDLE)
                {
                    if (mbox->state.compare_exchange_weak(current_state, MAILBOX_SCHEDULED, std::memory_order_release, std::memory_order_relaxed))
                    {
                        return true; // Caller must enqueue the parent actor
                    }
                }
                else if (current_state == MAILBOX_SCHEDULED)
                {
                    if (mbox->state.compare_exchange_weak(current_state, MAILBOX_HAS_NEW_WORK, std::memory_order_release, std::memory_order_relaxed))
                    {
                        return false; // Already scheduled/running; flag marked
                    }
                }
                else
                {
                    return false; // Already marked MAILBOX_HAS_NEW_WORK
                }
            }
        }

        // Rotates the mailbox and returns a thread-private, FIFO-ordered message linked list
        msg_t* mailbox_rotate(mailbox_t* mbox)
        {
            // 1. Atomically snapshot and clear public inbound queue
            msg_t* lifo_list = mbox->inbound.exchange(nullptr, std::memory_order_acquire);

            // 2. In-place pointer reversal (LIFO -> FIFO) directly on the local thread stack
            msg_t* fifo_list = nullptr;
            while (lifo_list != nullptr)
            {
                msg_t* next       = lifo_list->m_next;
                lifo_list->m_next = fifo_list;
                fifo_list         = lifo_list;
                lifo_list         = next;
            }

            // 3. Return the head of the isolated batch
            return fifo_list;
        }

        // mailbox_finalize handles the state transition of an actor's mailbox after a
        // worker thread finishes processing a batch of messages.
        bool mailbox_finalize(mailbox_t* mbox)
        {
            while (true)
            {
                u32 current_state = mbox->state.load(std::memory_order_relaxed);

                if (current_state == MAILBOX_HAS_NEW_WORK)
                {
                    if (mbox->state.compare_exchange_weak(current_state, MAILBOX_SCHEDULED, std::memory_order_release, std::memory_order_relaxed))
                    {
                        return true; // Re-enqueue required
                    }
                }
                else
                {
                    // Attempt transition to IDLE
                    if (mbox->state.compare_exchange_weak(current_state, MAILBOX_IDLE, std::memory_order_release, std::memory_order_relaxed))
                    {
                        // Mitigate race condition where work sneaks in right before IDLE swap
                        if (mbox->inbound.load(std::memory_order_acquire) != nullptr)
                        {
                            u32 expected_idle = MAILBOX_IDLE;
                            if (mbox->state.compare_exchange_strong(expected_idle, MAILBOX_SCHEDULED, std::memory_order_release, std::memory_order_relaxed))
                            {
                                return true; // Re-enqueue required due to race win
                            }
                        }
                        return false; // Safely IDLE, no re-enqueue
                    }
                }
            }
        }

    } // namespace nactor
} // namespace ncore
