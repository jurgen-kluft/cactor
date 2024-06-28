#include "ccore/c_allocator.h"
#include "cbase/c_hash.h"
#include "cbase/c_integer.h"
#include "cactor/c_actor.h"
#include "cactor/private/c_queue.h"

#ifdef TARGET_PC
#    include <windows.h>
#    include <stdio.h>
#endif

#ifdef TARGET_MAC
#    include <stdio.h>
#    include <mutex>
#    include <condition_variable>
#endif

namespace ncore
{
    namespace nactor
    {
        id_t get_msgid(const char* str) { return (id_t)nhash::strhash(str); }

        class worker_thread_t
        {
        public:
            bool quit() const;
        };

#ifdef TARGET_PC
        class semaphore_t
        {
            HANDLE ghSemaphore;

        public:
            void init(s32 initial, s32 maximum)
            {
                ghSemaphore = ::CreateSemaphore(nullptr,  // default security attributes
                                                initial,  // initial count
                                                maximum,  // maximum count
                                                nullptr); // unnamed semaphore
            }

            void deinit() { CloseHandle(ghSemaphore); }
            void request()
            {
                DWORD dwWaitResult = WaitForSingleObject(ghSemaphore, INFINITE);
                switch (dwWaitResult)
                {
                    case WAIT_OBJECT_0: break; // The semaphore object was signaled.
                    case WAIT_FAILED: break;
                }
            }
            void release() { ::ReleaseSemaphore(ghSemaphore /*handle to semaphore*/, 1 /*increase count by one*/, nullptr); }

            DCORE_CLASS_PLACEMENT_NEW_DELETE
        };

        class mutex_t
        {
            CRITICAL_SECTION ghMutex;

        public:
            void init() { InitializeCriticalSectionAndSpinCount((CRITICAL_SECTION*)&ghMutex, 4000); }
            void deinit() { DeleteCriticalSection((CRITICAL_SECTION*)&ghMutex); }
            void lock() { EnterCriticalSection((CRITICAL_SECTION*)&ghMutex); }
            void unlock() { LeaveCriticalSection((CRITICAL_SECTION*)&ghMutex); }

            DCORE_CLASS_PLACEMENT_NEW_DELETE
        };
#elif defined(TARGET_MAC)
        class semaphore_t
        {
            size_t                  avail;
            std::mutex              m;
            std::condition_variable cv;

        public:
            void init(s32 initial, s32 maximum) { avail = maximum; }

            void deinit() {}

            void request()
            {
                std::unique_lock<std::mutex> lk(m);
                cv.wait(lk, [this] { return avail > 0; });
                avail--;
            }

            void release()
            {
                std::lock_guard<std::mutex> lk(m);
                avail++;
                cv.notify_one();
            }

            DCORE_CLASS_PLACEMENT_NEW_DELETE
        };

        class mutex_t
        {
            pthread_mutex_t ghMutex;

        public:
            void init() { pthread_mutex_init(&ghMutex, 0); }
            void deinit() { pthread_mutex_destroy(&ghMutex); }
            void lock() { pthread_mutex_lock(&ghMutex); }
            void unlock() { pthread_mutex_unlock(&ghMutex); }

            DCORE_CLASS_PLACEMENT_NEW_DELETE
        };
#endif

        // We base the receiving of messages on simple structs, messages are
        // always send back to the sender for garbage collection to simplify
        // creation, re-use and destruction of messages.

        // There are a fixed number of worker-threads, initialized according to what
        // the user needs. The user can use the csystem package to identify how
        // many physical and logical cores this machine has as well as how many
        // hardware threads.

        // R 16 | W 16

        const u32 WRITE_INDEX_BITS = 12;
        const u32 WRITE_INDEX_MASK = ((u32(1) << WRITE_INDEX_BITS) - 1);
        const u32 READ_INDEX_BITS  = 12;
        const u32 READ_INDEX_MASK  = ((u32(1) << READ_INDEX_BITS) - 1);
        const u32 MAX_MESSAGES     = (u32(1) << WRITE_INDEX_BITS);

        class message_queue_t
        {
            mpsc_queue_t* m_queue;

        public:
            inline message_queue_t()
                : m_queue(nullptr)
            {
            }

            void init(alloc_t* allocator, s32 producer_count, u32 queue_size) { m_queue = mpsc_queue_create(allocator, producer_count, queue_size); }
            void deinit(alloc_t* allocator) { queue_destroy(allocator, m_queue); }

            bool push(s32 producer_index, message_t* msg)
            {
                u64 p = (u64)msg;
                return queue_enqueue(m_queue, producer_index, p);
            }

            void claim(u32& idx, u32& end) { queue_inspect(m_queue, idx, end); }

            void deque(u32& idx, u32 end, message_t*& msg)
            {
                u64 p;
                queue_dequeue(m_queue, idx, end, p);
                msg = (message_t*)p;
            }

            s8 release(u32 idx, u32 end) { return queue_release(m_queue, idx, end); }

            DCORE_CLASS_PLACEMENT_NEW_DELETE
        };

        class mailbox_t;

        struct actor_t
        {
            s32        m_index;
            handler_t* m_actor;
            mailbox_t* m_mailbox;
        };

        void init(actor_t* a)
        {
            a->m_actor   = nullptr;
            a->m_mailbox = nullptr;
        }

        class work_queue_t
        {
            s32         m_read;
            s32         m_write;
            mutex_t     m_lock;
            actor_t**   m_work_actors;
            semaphore_t m_sema;
            u32         m_size;

        public:
            inline work_queue_t()
                : m_read(0)
                , m_write(0)
                , m_lock()
                , m_work_actors(nullptr)
                , m_size(0)
                , m_sema()
            {
            }

            void init(alloc_t* allocator, s32 max_actors)
            {
                // No need for 'size' parameter, should just be next-power-of-2 of 'max actors'
                m_size        = math::next_power_of_two(max_actors);
                m_work_actors = (actor_t**)allocator->allocate(sizeof(void*) * m_size);
                m_sema.init(0, max_actors);
                m_lock.init();
            }

            void deinit(alloc_t* allocator)
            {
                m_read  = 0;
                m_write = 0;
                m_lock.deinit();
                allocator->deallocate(m_work_actors);
                m_work_actors = nullptr;
                m_size        = 0;
                m_sema.deinit();
            }

            void push(actor_t* actor)
            {
                m_lock.lock();
                s32 const cw                     = m_write;
                s32 const nw                     = (cw + 1);
                m_work_actors[cw & (m_size - 1)] = actor;
                m_write                          = nw;
                m_lock.unlock();
                m_sema.release();
            }

            void pop(actor_t*& actor)
            {
                m_sema.request();
                m_lock.lock();
                s32 const ri = m_read;
                m_read += 1;
                actor = m_work_actors[ri & (m_size - 1)];
                m_lock.unlock();
            }
        };

        class mailbox_t
        {
        public:
            actor_t*        m_actor;
            work_queue_t*   m_work;
            message_queue_t m_messages;

            void init(actor_t* actor, work_queue_t* work, alloc_t* allocator, s32 max_actors, s32 max_messages)
            {
                m_actor = actor;
                m_work  = work;
                m_messages.init(allocator, max_actors, max_messages);
            }

            void deinit(alloc_t* allocator) { m_messages.deinit(allocator); }

            void send(message_t* msg, actor_t* recipient);
            s32  push(s32 producer_index, message_t* msg);
            void claim(u32& idx, u32& end); // return [i,end] range of messages
            void deque(u32& idx, u32 end, message_t*& msg);
            s8 release(u32 idx, u32 end); // return 1 when there are more messages queued up
        };

        // Work queue init and deinit
        void work_init(work_queue_t* queue, alloc_t* allocator, s32 max_num_actors = 8) { queue->init(allocator, max_num_actors); }
        void work_deinit(work_queue_t* queue, alloc_t* allocator) { queue->deinit(allocator); }

        // @Note: Multiple Producers
        void work_add(work_queue_t* queue, actor_t* sender, message_t* msg, actor_t* recipient)
        {
            if (sender->m_mailbox->push(sender->m_index, msg) == 1)
            {
                // mailbox indicated that we have pushed a message and the actor is already
                // marked as 'idle' and we are the one here to push him in the work-queue.
                queue->push(recipient);
            }
        }

        // @Note: Multiple Consumers
        void work_take(work_queue_t* queue, actor_t*& actor, message_t*& msg, u32& msgidx, u32& msgend)
        {
            if (actor == nullptr)
            {
                // This will make the calling thread block if the queue is empty
                queue->pop(actor);

                // Claim a batch of messages to be processed here by this worker
                actor->m_mailbox->claim(msgidx, msgend);
                actor->m_mailbox->deque(msgidx, msgend, msg);
            }
            else
            {
                // Take the next message out of the batch from the mailbox of the actor
                actor->m_mailbox->deque(msgidx, msgend, msg);
            }
        }

        // @Note: Multiple Consumers
        void work_done(work_queue_t* queue, actor_t*& actor, message_t*& msg, u32& msgidx, u32& msgend)
        {
            // If 'msgidx == msgend' then try and add the actor back to the work-queue since
            // it was the last message we supposed to have processed from the batch.
            if (msgidx == msgend)
            {
                if (actor->m_mailbox->release(msgidx, msgend) == 1)
                {
                    // mailbox indicated that we have to push back the actor in the work-queue
                    // because there are still messages pending.
                    queue->push(actor);
                }
                actor = nullptr;
            }
            msg = nullptr;
        }

        void mailbox_t::send(message_t* msg, actor_t* recipient) { work_add(m_work, m_actor, msg, recipient); }
        s32  mailbox_t::push(s32 producer_index, message_t* msg) { return (m_messages.push(producer_index, msg)); }
        void mailbox_t::claim(u32& idx, u32& end) { m_messages.claim(idx, end); }
        void mailbox_t::deque(u32& idx, u32 end, message_t*& msg) { m_messages.deque(idx, end, msg); }
        s8 mailbox_t::release(u32 idx, u32 end) { return m_messages.release(idx, end); }

        struct ctxt_t
        {
            inline ctxt_t()
                : m_i(0)
                , m_e(0)
                , m_actor(nullptr)
                , m_mailbox(nullptr)
                , m_msg(nullptr)
            {
            }

            u32        m_i;
            u32        m_e;
            actor_t*   m_actor;
            mailbox_t* m_mailbox;
            message_t* m_msg;
        };

        void worker_tick(work_queue_t* work, ctxt_t* ctx)
        {
            // Try and take an [actor, message[i,e]] piece of work
            // If no work is available it will block on the semaphore.
            work_take(work, ctx->m_actor, ctx->m_msg, ctx->m_i, ctx->m_e);

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
                    work_add(work, ctx->m_actor, ctx->m_msg, ctx->m_msg->get_sender());
                }
            }
            else if (ctx->m_msg->is_sender(ctx->m_actor))
            {
                ctx->m_actor->m_actor->returned(ctx->m_msg);
            }

            // Report the [actor, message[i,e]] back as 'done'
            work_done(work, ctx->m_actor, ctx->m_msg, ctx->m_i, ctx->m_e);
        }

        // a single work_queue_t is shared between multiple worker_thread_t
        void worker_run(worker_thread_t* thread, work_queue_t* work)
        {
            ctxt_t ctx;
            while (thread->quit() == false)
            {
                worker_tick(work, &ctx);
            }
        }

        class system_t
        {
        public:
            void     start(s32 num_threads, s32 max_actors);
            void     stop();
            actor_t* join(handler_t* actor);
            void     leave(actor_t* actor);

            alloc_t*         m_allocator;
            s32              m_numthreads;
            work_queue_t     m_work_queue;
            worker_thread_t* m_thread_workers;
            s32              m_num_actors;
            s32              m_max_actors;
            actor_t*         m_actors;
        };

        void system_t::start(s32 num_threads, s32 max_actors)
        {
            m_numthreads = num_threads;
            m_work_queue.init(m_allocator, max_actors);

            m_actors = (actor_t*)m_allocator->allocate(sizeof(actor_t) * max_actors);
            for (s32 i = 0; i < max_actors; ++i)
                init(&m_actors[i]);
        }

        void system_t::stop() {}

        actor_t* actor_join(system_t* system, handler_t* handler) { return system->join(handler); }
        void     actor_leave(system_t* system, actor_t* actor) { system->leave(actor); }

        void send(system_t* system, actor_t* sender, message_t* msg, actor_t* recipient) { work_add(&system->m_work_queue, sender, msg, recipient); }

    } // namespace nactor

} // namespace ncore
