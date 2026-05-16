#include "ccore/c_allocator.h"
#include "cbase/c_hash.h"
#include "cbase/c_integer.h"

#include "cactor/c_actor.h"
#include "cactor/private/c_actor.h"
#include "cactor/private/c_queue.h"
#include "cactor/private/c_mailbox.h"
#include "cactor/private/c_message.h"
#include "cactor/private/c_worker_queue.h"

#include <thread>

namespace ncore
{
    namespace nactor
    {
        void init_actor(actor_t* actor, system_t* system, s32 index, void* user, actor_received_fn received, actor_process_fn process, actor_returned_fn returned)
        {
            actor->m_index              = index;
            actor->m_user               = user;
            actor->m_received           = received;
            actor->m_process            = process;
            actor->m_returned           = returned;
            actor->m_mailbox            = mailbox_create();
            actor->m_free_messages_pool = nullptr;
            actor->m_system             = system;
            actor->m_next_scheduled     = nullptr;
        }

        struct worker_thread_t
        {
            void start(worker_queue_t* work);
            void stop();

        protected:
            bool        tick(worker_queue_t* work);
            std::thread m_thread;
        };

        bool worker_thread_t::tick(worker_queue_t* global_queue)
        {
            actor_t* actor = worker_queue_pop(global_queue);
            if (actor == nullptr)
                return false; // No more work, time to exit

            // 1. Isolate the current rotation batch
            msg_t* msg_batch = mailbox_rotate(actor->m_mailbox);

            // 2. Consume the isolated batch
            while (msg_batch != nullptr)
            {
                msg_t* msg = msg_batch;
                msg_batch  = msg->m_next;
                // Execute business logic...
                actor->m_process(actor->m_user, msg);
                // Automatic Garbage Collection (recycle message back to sender pool)
                actor->m_returned(actor->m_user, msg);
            }

            // 3. Finalize state and check if re-scheduling is necessary
            if (mailbox_finalize(actor->m_mailbox))
            {
                worker_queue_push(global_queue, actor);
            }

            return true; // Continue processing
        }

        void worker_thread_t::start(worker_queue_t* work)
        {
            // Prevent starting an already active thread
            if (m_thread.joinable())
                return;

            // Correctly passes 'this' and the queue pointer into the lambda context
            m_thread = std::thread(
                [this, work]()
                {
                    // Keep executing tick() until it signals that the queue has shut down
                    while (this->tick(work))
                    {
                        // Intentionally empty: loop condition drives the execution
                    }
                });
        }

        // Assuming worker_queue_shutdown wakes up all threads and causes worker_queue_pop to return nullptr
        void worker_thread_t::stop()
        {
            // Check if the thread is actually running before trying to stop it
            if (m_thread.joinable())
            {
                // Note: The global system shutdown coordinator should call
                // worker_queue_shutdown(global_queue) right BEFORE calling stop()
                // on individual worker threads to unblock them cleanly.

                // Wait for this specific background thread to finish its current loop and exit
                m_thread.join();
            }
        }

        class system_t
        {
        public:
            void     setup(alloc_t* allocator, s32 num_threads, s32 max_actors);
            void     teardown();
            void     start();
            void     stop();
            actor_t* join(void* user, actor_received_fn received, actor_process_fn process, actor_returned_fn returned);
            void     leave(actor_t* actor);

            alloc_t*         m_allocator;
            s32              m_numthreads;
            worker_queue_t*  m_work_queue;
            worker_thread_t* m_thread_workers;
            s32              m_num_actors;
            s32              m_max_actors;
            actor_t*         m_actors;
        };

        void system_t::setup(alloc_t* allocator, s32 num_threads, s32 max_actors)
        {
            m_allocator      = allocator;
            m_numthreads     = num_threads;
            m_thread_workers = (worker_thread_t*)m_allocator->allocate(sizeof(worker_thread_t) * num_threads);
            m_num_actors     = 0;
            m_max_actors     = max_actors;
            m_actors         = (actor_t*)m_allocator->allocate(sizeof(actor_t) * max_actors);
            for (s32 i = 0; i < max_actors; ++i)
                init_actor(&m_actors[i], this, i, nullptr, nullptr, nullptr, nullptr);
            m_work_queue = worker_queue_create();
        }

        void system_t::teardown()
        {
            stop();                              // stop all workers
            worker_queue_shutdown(m_work_queue); // shutdown the queue to unblock workers

            // deallocate all resources
            m_allocator->deallocate(m_actors);
            m_allocator->deallocate(m_thread_workers);
        }

        void system_t::start()
        {
            // start all the thread workers
            for (s32 i = 0; i < m_numthreads; ++i)
            {
                m_thread_workers[i].start(m_work_queue);
            }
        }

        void system_t::stop()
        {
            // push 'quit' work into the queue for each actor

            // wait for all thread workers to join
            // start all the thread workers
            for (s32 i = 0; i < m_numthreads; ++i)
            {
                m_thread_workers[i].stop();
            }
        }

        system_t* create_system(alloc_t* allocator, s32 num_threads, s32 max_actors, s32 max_messages, s32 max_producers)
        {
            system_t* sys    = (system_t*)allocator->allocate(sizeof(system_t));
            sys->m_allocator = allocator;
            sys->setup(allocator, num_threads, max_actors);
            return sys;
        }

        void destroy_system(system_t* system)
        {
            alloc_t* allocator = system->m_allocator;
            system->teardown();
            allocator->deallocate(system);
        }

        actor_t* actor_join(system_t* system, void* user, actor_received_fn received, actor_process_fn process, actor_returned_fn returned) { return system->join(user, received, process, returned); }
        void     actor_leave(system_t* system, actor_t* actor) { system->leave(actor); }

        void actor_send(actor_t* sender, msg_t* msg, actor_t* recipient)
        {
            msg->m_sender    = sender;
            msg->m_recipient = recipient;

            // If push returns true, we are responsible for queueing the actor
            if (mailbox_push(recipient->m_mailbox, msg))
            {
                worker_queue_push(sender->m_system->m_work_queue, recipient);
            }
        }

    } // namespace nactor

} // namespace ncore
