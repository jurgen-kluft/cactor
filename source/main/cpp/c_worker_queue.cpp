#include "ccore/c_target.h"
#include "cbase/c_allocator.h"
#include "cbase/c_integer.h"

#include "cactor/private/c_worker_queue.h"
#include "cactor/private/c_actor.h"

#include <atomic>
#include <mutex>
#include <condition_variable>

namespace ncore
{
    namespace nactor
    {
        struct worker_queue_t
        {
            std::mutex              mutex;
            std::condition_variable cv;

            actor_t* head     = nullptr;
            actor_t* tail     = nullptr;
            bool     shutdown = false; // Flags threads to wake up and exit during teardown
        };

        // Initialize the queue state
        void worker_queue_init(worker_queue_t* queue)
        {
            queue->head     = nullptr;
            queue->tail     = nullptr;
            queue->shutdown = false;
        }

        worker_queue_t* worker_queue_create()
        {
            // TODO: arena ?
            worker_queue_t* queue = new worker_queue_t();
            worker_queue_init(queue);
            return queue;
        }

        // Gracefully wake up all sleeping threads and shut down the model
        void worker_queue_shutdown(worker_queue_t* queue)
        {
            {
                std::lock_guard<std::mutex> lock(queue->mutex);
                queue->shutdown = true;
            }
            // Wake up all threads so they can read the shutdown flag and exit safely
            queue->cv.notify_all();
        }

        void worker_queue_destroy(worker_queue_t* queue)
        {
            // arena ?
            // delete queue;
        }

        // Push an actor to the back of the queue (Multi-Producer)
        void worker_queue_push(worker_queue_t* queue, actor_t* actor)
        {
            if (actor == nullptr)
                return;

            actor->m_next_scheduled = nullptr;

            {
                std::lock_guard<std::mutex> lock(queue->mutex);

                if (queue->shutdown)
                    return;

                if (queue->tail == nullptr)
                {
                    // Queue was completely empty
                    queue->head = actor;
                    queue->tail = actor;
                }
                else
                {
                    // Append to the end of the line
                    queue->tail->m_next_scheduled = actor;
                    queue->tail                 = actor;
                }
            }

            // Wake up one blocking worker thread to handle this actor
            queue->cv.notify_one();
        }

        // Pop an actor from the front of the queue, block if empty (Multi-Consumer)
        // Returns nullptr ONLY if the queue is shutting down.
        actor_t* worker_queue_pop(worker_queue_t* queue)
        {
            std::unique_lock<std::mutex> lock(queue->mutex);

            // Block the thread while the queue is empty AND system is running
            queue->cv.wait(lock, [queue]() { return queue->head != nullptr || queue->shutdown; });

            // If woken up due to shutdown and queue is empty, abort
            if (queue->shutdown && queue->head == nullptr)
            {
                return nullptr;
            }

            // Dequeue the front actor
            actor_t* actor = queue->head;
            queue->head    = actor->m_next_scheduled;

            // If the queue is now empty, clear the tail pointer too
            if (queue->head == nullptr)
            {
                queue->tail = nullptr;
            }

            actor->m_next_scheduled = nullptr; // Clean up pointer
            return actor;
        }

    } // namespace nactor
} // namespace ncore
