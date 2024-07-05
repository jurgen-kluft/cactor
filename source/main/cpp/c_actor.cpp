#include "ccore/c_allocator.h"
#include "cbase/c_hash.h"
#include "cbase/c_integer.h"
#include "cactor/c_actor.h"
#include "cactor/private/c_queue.h"

#include <thread>

namespace ncore
{
    namespace nactor
    {
        id_t get_msgid(const char* str) { return (id_t)nhash::strhash(str); }

        struct worker_thread_t
        {
            void start(mpmc_queue_blocking_t* work);
            void stop();

        protected:
            bool tick(mpmc_queue_blocking_t* work, ctxt_t* ctx);
            void entry(worker_thread_t* thread, mpmc_queue_blocking_t* work);

            std::thread m_thread;
        };

        // We base the receiving of messages on simple structs, messages are
        // always send back to the sender for garbage collection to simplify
        // creation, re-use and destruction of messages.

        // There are a fixed number of worker-threads, initialized according to what
        // the user needs. The user can use the csystem package to identify how
        // many physical and logical cores this machine has as well as how many
        // hardware threads.

        class mailbox_t;

        struct actor_t
        {
            s32        m_index;
            handler_t* m_actor;
            mailbox_t* m_mailbox;
        };

        static void s_actor_init(actor_t* a)
        {
            a->m_actor   = nullptr;
            a->m_mailbox = nullptr;
        }

        class mailbox_t
        {
        public:
            actor_t*               m_actor;
            mpmc_queue_blocking_t* m_work;
            mpsc_queue_t*          m_messages;

            void init(actor_t* actor, mpmc_queue_blocking_t* work, alloc_t* allocator, s32 max_actors, s32 max_messages)
            {
                m_actor    = actor;
                m_work     = work;
                m_messages = mpsc_queue_create(allocator, max_actors, max_messages);
            }

            void deinit(alloc_t* allocator) { queue_destroy(allocator, m_messages); }

            void send(message_t* msg, actor_t* recipient);
            s32  push(s32 producer_index, message_t* msg);
            bool claim(u32& idx, u32& end); // return [i,end] range of messages
            void deque(u32& idx, u32 end, message_t*& msg);
            s8   release(u32 idx, u32 end); // return 1 when there are more messages queued up
        };

        // @Note: Multiple Producers
        static void s_work_add(mpmc_queue_blocking_t* queue, actor_t* sender, message_t* msg, actor_t* recipient)
        {
            if (sender->m_mailbox->push(sender->m_index, msg) == 1)
            {
                queue_enqueue(queue, (u64)recipient);
            }
        }

        static const actor_t* s_actor_quit = (actor_t*)-1;

        // @Note: Multiple Consumers
        static bool s_work_take(mpmc_queue_blocking_t* queue, actor_t*& actor, message_t*& msg, u32& msgidx, u32& msgend)
        {
            if (actor == nullptr)
            {
                // This will make the calling thread block on a semaphore if the queue is empty and we will be
                // signaled when there is work to be done.
                actor = queue_dequeue<actor_t>(queue);
                if (actor == s_actor_quit)
                {
                    actor  = nullptr;
                    msg    = nullptr;
                    msgidx = 0;
                    msgend = 0;
                    return false;
                }

                // Claim a batch of messages to be processed here by this worker
                actor->m_mailbox->claim(msgidx, msgend);
                actor->m_mailbox->deque(msgidx, msgend, msg);
            }
            else
            {
                // Take the next message out of the batch from the mailbox of the actor
                actor->m_mailbox->deque(msgidx, msgend, msg);
            }
            return true;
        }

        // @Note: Multiple Consumers
        static void s_work_done(mpmc_queue_blocking_t* queue, actor_t*& actor, message_t*& msg, u32& msgidx, u32& msgend)
        {
            // If 'msgidx == msgend' then try and add the actor back to the work-queue since
            // it was the last message we supposed to have processed from the batch.
            if (msgidx == msgend)
            {
                if (actor->m_mailbox->release(msgidx, msgend) == 1)
                {
                    // mailbox indicated that we have to push back the actor in the work-queue
                    // because there are still messages pending.
                    queue_enqueue(queue, (u64)actor);
                }
                actor = nullptr;
            }
            msg = nullptr;
        }

        void mailbox_t::send(message_t* msg, actor_t* recipient) { s_work_add(m_work, m_actor, msg, recipient); }
        s32  mailbox_t::push(s32 producer_index, message_t* msg) { queue_enqueue(m_messages, producer_index, (u64)msg); }
        bool mailbox_t::claim(u32& idx, u32& end) { return queue_inspect(m_messages, idx, end); }
        void mailbox_t::deque(u32& idx, u32 end, message_t*& msg) { msg = queue_dequeue<message_t>(m_messages, idx, end); }
        s8   mailbox_t::release(u32 idx, u32 end) { return queue_release(m_messages, idx, end); }

        struct ctxt_t
        {
            inline ctxt_t()
                : m_i(0)
                , m_e(0)
                , m_actor(nullptr)
                , m_msg(nullptr)
            {
            }

            u32        m_i;
            u32        m_e;
            actor_t*   m_actor;
            message_t* m_msg;
        };

        bool worker_thread_t::tick(mpmc_queue_blocking_t* work, ctxt_t* ctx)
        {
            // Try and take an [actor, message[i,e]] piece of work
            // If no work is available it will block on the semaphore.
            if (!s_work_take(work, ctx->m_actor, ctx->m_msg, ctx->m_i, ctx->m_e))
            {
                return false;
            }

            // Let the actor handle the message
            if (ctx->m_msg->is_recipient(ctx->m_actor))
            {
                ctx->m_actor->m_actor->received(ctx->m_msg);
                if (ctx->m_msg->is_sender(ctx->m_actor))
                {
                    // Garbage collect the message immediately
                    ctx->m_actor->m_actor->returned(ctx->m_msg);
                }
                else
                {
                    // Send this message back to sender, where is the mailbox of the sender?
                    s_work_add(work, ctx->m_actor, ctx->m_msg, ctx->m_msg->get_sender());
                }
            }
            else if (ctx->m_msg->is_sender(ctx->m_actor))
            {
                ctx->m_actor->m_actor->returned(ctx->m_msg);
            }

            // Report the [actor, message[i,e]] back as 'done'
            s_work_done(work, ctx->m_actor, ctx->m_msg, ctx->m_i, ctx->m_e);

            return true;
        }

        // a single mpmc_queue_blocking_t is shared between multiple worker_thread_t
        void worker_thread_t::entry(worker_thread_t* thread, mpmc_queue_blocking_t* work)
        {
            ctxt_t ctx;
            while (tick(work, &ctx)) {}
        }

        void worker_thread_t::start(mpmc_queue_blocking_t* work)
        {
            m_thread = std::thread([this, work]() { entry(this, work); });
        }

        class system_t
        {
        public:
            void     setup(alloc_t* allocator, s32 num_threads, s32 max_actors);
            void     teardown();
            void     start();
            void     stop();
            actor_t* join(handler_t* actor);
            void     leave(actor_t* actor);

            alloc_t*               m_allocator;
            s32                    m_numthreads;
            mpmc_queue_blocking_t* m_work_queue;
            worker_thread_t*       m_thread_workers;
            s32                    m_num_actors;
            s32                    m_max_actors;
            handler_t**            m_handlers;
            actor_t*               m_actors;
        };

        void system_t::setup(alloc_t* allocator, s32 num_threads, s32 max_actors)
        {
            m_allocator      = allocator;
            m_numthreads     = num_threads;
            m_thread_workers = (worker_thread_t*)m_allocator->allocate(sizeof(worker_thread_t) * num_threads);
            m_num_actors     = 0;
            m_max_actors     = max_actors;
            m_handlers       = (handler_t**)m_allocator->allocate(sizeof(handler_t*) * max_actors);
            m_actors         = (actor_t*)m_allocator->allocate(sizeof(actor_t) * max_actors);
            for (s32 i = 0; i < max_actors; ++i)
            {
                m_handlers[i] = nullptr;
                s_actor_init(&m_actors[i]);
            }
            m_work_queue = mpmc_queue_create(m_allocator, 2048);
        }

        void system_t::teardown()
        {
            stop(); // stop all workers

            queue_destroy(m_allocator, m_work_queue);

            // deallocate all resources
            m_allocator->deallocate(m_actors);
            m_allocator->deallocate(m_handlers);
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

        actor_t* actor_join(system_t* system, handler_t* handler) { return system->join(handler); }
        void     actor_leave(system_t* system, actor_t* actor) { system->leave(actor); }

        void actor_send(system_t* system, actor_t* sender, message_t* msg, actor_t* recipient) { s_work_add(system->m_work_queue, sender, msg, recipient); }

    } // namespace nactor

} // namespace ncore
