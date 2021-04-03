#include "xbase/x_allocator.h"
#include "xactor/x_actor.h"

#ifdef TARGET_PC
#include <windows.h>
#include <stdio.h>
#endif

#ifdef TARGET_MAC
#include <stdio.h>
#include <mutex>
#include <condition_variable>
#endif

// Actor Model:
// - An actor is executed on one thread only
// - Order of execution of messages is undetermined
//

namespace xcore
{
    class xisemaphore
    {
    public:
        virtual void setup(s32 initial, s32 maximum) = 0;
        virtual void teardown()                      = 0;

        virtual void request() = 0;
        virtual void release() = 0;
    };
    class ximutex
    {
    public:
        virtual void setup()    = 0;
        virtual void teardown() = 0;

        virtual void lock()   = 0;
        virtual void unlock() = 0;
    };

#ifdef TARGET_PC
    class xsemaphore : public xisemaphore
    {
        HANDLE ghSemaphore;

    public:
        virtual void setup(s32 initial, s32 maximum)
        {
            ghSemaphore = ::CreateSemaphore(NULL,    // default security attributes
                                            initial, // initial count
                                            maximum, // maximum count
                                            NULL);   // unnamed semaphore
        }

        virtual void teardown() { CloseHandle(ghSemaphore); }
        virtual void request()
        {
            DWORD dwWaitResult = WaitForSingleObject(ghSemaphore, INFINITE);
            switch (dwWaitResult)
            {
                case WAIT_OBJECT_0: break; // The semaphore object was signaled.
                case WAIT_FAILED: break;
            }
        }
        virtual void release() { ::ReleaseSemaphore(ghSemaphore /*handle to semaphore*/, 1 /*increase count by one*/, NULL); }

        XCORE_CLASS_PLACEMENT_NEW_DELETE
    };

    class xmutex : public ximutex
    {
        CRITICAL_SECTION ghMutex;

    public:
        virtual void setup() { InitializeCriticalSectionAndSpinCount((CRITICAL_SECTION*)&ghMutex, 4000); }
        virtual void teardown() { DeleteCriticalSection((CRITICAL_SECTION*)&ghMutex); }
        virtual void lock() { EnterCriticalSection((CRITICAL_SECTION*)&ghMutex); }
        virtual void unlock() { LeaveCriticalSection((CRITICAL_SECTION*)&ghMutex); }

        XCORE_CLASS_PLACEMENT_NEW_DELETE
    };
#elif defined(TARGET_MAC)
    class xsemaphore : public xisemaphore
    {
        size_t avail;
        std::mutex m;
        std::condition_variable cv;

    public:
        virtual void setup(s32 initial, s32 maximum)
        {
            avail=maximum;
        }

        virtual void teardown() 
        { 

        }

        virtual void request()
        {
            std::unique_lock<std::mutex> lk(m);
            cv.wait(lk, [this] { return avail > 0; });
            avail--;
        }

        virtual void release() 
        { 
            std::lock_guard<std::mutex> lk(m);
            avail++;
            cv.notify_one();
        }

        XCORE_CLASS_PLACEMENT_NEW_DELETE
    };

    class xmutex : public ximutex
    {
        pthread_mutex_t ghMutex;

    public:
        virtual void setup() { pthread_mutex_init(&ghMutex,0); }
        virtual void teardown() { pthread_mutex_destroy(&ghMutex); }
        virtual void lock() { pthread_mutex_lock(&ghMutex); }
        virtual void unlock() { pthread_mutex_unlock(&ghMutex); }

        XCORE_CLASS_PLACEMENT_NEW_DELETE
    };
#endif

    class xworker_thread
    {
    public:
        virtual bool quit() const = 0;
    };

    // We base the receiving of messages on simple structs, messages are
    // always send back to the sender for garbage collection to simplify
    // creation, re-use and destruction of messages.

    // There are a fixed number of worker-threads, initialized according to what
    // the user needs. The user can use the xsystem package to identify how
    // many physical and logical cores this machine has as well as how many
    // hardware threads.

    // C 1 | R 15 | W 15

    const u32 WRITE_INDEX_SHIFT = 0;
    const u32 WRITE_INDEX_BITS  = 12;
    const u32 WRITE_INDEX_MASK  = ((u32(1) << WRITE_INDEX_BITS) - 1) << WRITE_INDEX_SHIFT;
    const u32 READ_INDEX_SHIFT  = 16;
    const u32 READ_INDEX_BITS   = 12;
    const u32 READ_INDEX_MASK   = ((u32(1) << READ_INDEX_BITS) - 1) << READ_INDEX_SHIFT;
    const u32 MAX_MESSAGES      = (u32(1) << WRITE_INDEX_BITS);
    // const u32 QUEUED_FLAG_SHIFT = 31;
    // const u32 QUEUED_FLAG = u32(1) << QUEUED_FLAG_SHIFT;

    // Queue using a ringbuffer, it is important that this queue
    // is initialized with a size that will never be reached. This queue
    // will fail HARD when messages are queued up and the size is exceeding
    // the initialized size.
    // The initialized size of this queue cannot exceed 4096 messages.
    // When writing a system using this actor-model messages should be
    // pre-allocated (bounded) and not dynamically allocated.
    class xqueue
    {
        u32 const m_size;
        void**    m_queue;
        s32       m_readwrite;
        xmutex    m_lock;

    public:
        inline xqueue(u32 size)
            : m_size(size)
            , m_queue(nullptr)
            , m_readwrite(0)
        {
        }

        void init(alloc_t* allocator)
        {
            m_queue = (void**)allocator->allocate(m_size * sizeof(void*), sizeof(void*));
            m_lock.setup();
        }

        void destroy(alloc_t* allocator)
        {
            allocator->deallocate(m_queue);
            m_lock.teardown();
        }

        inline u32 size() const { return m_size; }

        // Multiple 'producers'
        s32 push(void* p)
        {
            m_lock.lock();
            s32 const crw = m_readwrite;
            s32 const cw  = (crw & WRITE_INDEX_MASK);
            s32 const nw  = cw + 1;
            s32 const nrw = (crw & READ_INDEX_MASK) | (nw & WRITE_INDEX_MASK);
            m_readwrite   = nrw;
            m_queue[cw]   = p;
            m_lock.unlock();

            // If the QUEUED flag was 'false' and we have pushed a new piece of work into
            // the queue and before the queue was empty we are the one that should return
            // '1' to indicate that the actor should be queued-up for processing.
            s32 const wi = (crw & WRITE_INDEX_MASK) >> WRITE_INDEX_SHIFT;
            s32 const ri = (crw & READ_INDEX_MASK) >> READ_INDEX_SHIFT;
            return (ri == wi) ? 1 : 0;
        }

        void claim(u32& idx, u32& end)
        {
            m_lock.lock();
            s32 i = m_readwrite;
            m_lock.unlock();
            idx = u32((i & READ_INDEX_MASK) >> READ_INDEX_SHIFT);
            end = u32((i & WRITE_INDEX_MASK) >> WRITE_INDEX_SHIFT);
        }

        void deque(u32& idx, u32 end, void*& p)
        {
            p = m_queue[idx & (m_size - 1)];
            idx++;
        }

        s32 release(u32 idx, u32 end)
        {
            // There is only one 'consumer'
            // This is our new 'read' index
            u32 const r = end & (m_size - 1);
            m_lock.lock();
            s32       o = m_readwrite;
            s32       w = u32((o & WRITE_INDEX_MASK) >> WRITE_INDEX_SHIFT);
            s32       n = ((r & READ_INDEX_MASK) << READ_INDEX_SHIFT) | (w << WRITE_INDEX_SHIFT);
            s32 const q = (r != w);
            m_readwrite = n;
            m_lock.unlock();

            // So we now have updated 'm_readwrite' with 'read'/'write' and detected that we have
            // pushed a message into an empty queue, so we are the ones to indicate that the actor
            // should be queued.
            return q;
        }
    };

    class xmessage_queue: public xmessages
    {
        xqueue m_queue;

    public:
        inline xmessage_queue(u32 size)
            : m_queue(size)
        {
        }

        void init(alloc_t* allocator) { m_queue.init(allocator); }

        virtual s32 push(xmessage* msg)
        {
            void* p = (void*)msg;
            return m_queue.push(p);
        }

        virtual void claim(u32& idx, u32& end) { m_queue.claim(idx, end); }

        virtual void deque(u32& idx, u32 end, xmessage*& msg)
        {
            void* p;
            m_queue.deque(idx, end, p);
            msg = (xmessage*)p;
        }

        virtual s32 release(u32 idx, u32 end) { return m_queue.release(idx, end); }

        XCORE_CLASS_PLACEMENT_NEW_DELETE
    };

    class xwork
    {
    public:
        virtual void add(xactor* sender, xmessage* msg, xactor* recipient) = 0;

        virtual void queue(xactor* actor)                                                                = 0;
        virtual void take(xworker* worker, xactor*& actor, xmessage*& msg, u32& idx_begin, u32& idx_end) = 0;
        virtual void done(xworker* worker, xactor*& actor, xmessage*& msg, u32& idx_begin, u32& idx_end) = 0;
    };

    class xactor_mailbox : public xmailbox
    {
    public:
        xactor*    m_actor;
        xwork*     m_work;
        xmessages* m_messages;

        void setup(xactor* actor, xwork* work, alloc_t* allocator, s32 max_messages)
        {
            m_actor    = actor;
            m_work     = work;
            m_messages = allocator->construct<xmessage_queue>(max_messages);
        }

        void teardown() {}

        virtual void send(xmessage* msg, xactor* recipient) { m_work->add(m_actor, msg, recipient); }

        s32  push(xmessage* msg);
        void claim(u32& idx, u32& end); // return [i,end] range of messages
        void deque(u32& idx, u32 end, xmessage*& msg);
        s32  release(u32 idx, u32 end); // return 1 when there are messages pending
    };

    s32  xactor_mailbox::push(xmessage* msg) { return (m_messages->push(msg)); }
    void xactor_mailbox::claim(u32& idx, u32& end) { m_messages->claim(idx, end); }
    void xactor_mailbox::deque(u32& idx, u32 end, xmessage*& msg) { m_messages->deque(idx, end, msg); }
    s32  xactor_mailbox::release(u32 idx, u32 end) { return (m_messages->release(idx, end)); }

    class xwork_queue
    {
        s32        m_read;
        s32        m_write;
        xmutex     m_lock;
        xactor**   m_work;
        xsemaphore m_sema;
        u32        m_size;

    public:
        inline xwork_queue(u32 size)
            : m_read(0)
            , m_write(0)
            , m_lock()
            , m_work(nullptr)
            , m_size(size)
            , m_sema()
        {
        }

        void setup(alloc_t* allocator, s32 max_actors)
        {
            m_work = (xactor**)allocator->allocate(sizeof(void*) * m_size);
            m_sema.setup(0, max_actors);
            m_lock.setup();
        }

        void teardown()
        {
            m_sema.teardown();
            m_lock.teardown();
        }

        void push(xactor* actor)
        {
            m_lock.lock();
            s32 cw                    = m_write;
            s32 nw                    = (cw + 1);
            m_work[cw & (m_size - 1)] = actor;
            m_write                   = nw;
            m_lock.unlock();
            m_sema.release();
        }

        void pop(xactor*& actor)
        {
            m_sema.request();
            m_lock.lock();
            s32 const ri = m_read;
            m_read += 1;
            actor = m_work[ri & (m_size - 1)];
            m_lock.unlock();
        }
    };

    class xwork_imp : public xwork
    {
        xwork_queue m_queue;

    public:
        inline xwork_imp(u32 queue_size)
            : m_queue(queue_size)
        {
        }

        void init(alloc_t* allocator, s32 max_num_actors = 8) { m_queue.setup(allocator, max_num_actors); }

        // @Note: This can be called from multiple threads!
        void add(xactor* sender, xmessage* msg, xactor* recipient)
        {
            xactor_mailbox* mb = static_cast<xactor_mailbox*>(recipient->getmailbox());
            if (mb->push(msg) == 1)
            {
                // mailbox indicated that we have pushed a message and the actor is already
                // marked as 'idle' and we are the one here to push him in the work-queue.
                m_queue.push(recipient);
            }
        }

        void take(xworker_thread* thread, xactor*& actor, xmessage*& msg, u32& msgidx, u32& msgend)
        {
            if (actor == NULL)
            {
                // This will make the calling thread block if the queue is empty
                m_queue.pop(actor);

                xactor_mailbox* mb = static_cast<xactor_mailbox*>(actor->getmailbox());
                // Claim a batch of messages to be processed here by this worker
                mb->claim(msgidx, msgend);
                mb->deque(msgidx, msgend, msg);
            }
            else
            {
                // Take the next message out of the batch from the mailbox of the actor
                xactor_mailbox* mb = static_cast<xactor_mailbox*>(actor->getmailbox());
                mb->deque(msgidx, msgend, msg);
            }
        }

        void done(xworker_thread* thread, xactor*& actor, xmessage*& msg, u32& msgidx, u32& msgend)
        {
            // If 'msgidx == msgend' then try and add the actor back to the work-queue since
            // it was the last message we supposed to have processed from the batch.
            if (msgidx == msgend)
            {
                xactor_mailbox* mb = static_cast<xactor_mailbox*>(actor->getmailbox());
                if (mb->release(msgidx, msgend) == 1)
                {
                    // mailbox indicated that we have to push back the actor in the work-queue
                    // because there are new messages pending.
                    m_queue.push(actor);
                }
                actor = NULL;
            }
            msg = NULL;
        }
    };
    class xworker
    {
    public:
        virtual void run(xworker_thread* thread, xwork* work) = 0;
    };

    class xworker_imp : public xworker
    {
    public:
        void run(xworker_thread* thread, xwork* work)
        {
            u32 i = 0;
            u32 e = 0;

            xactor*   actor = nullptr;
            xmessage* msg   = nullptr;

            while (thread->quit() == false)
            {
                // Try and take an [actor, message[i,e]] piece of work
                work->take(this, actor, msg, i, e);

                // Let the actor handle the message
                if (msg->is_recipient(actor))
                {
                    actor->received(msg);
                    if (msg->is_sender(actor))
                    {
                        // Garbage collect the message immediately
                        actor->returned(msg);
                    }
                    else
                    {
                        // Send this message back to sender
                        work->add(msg->get_recipient(), msg, msg->get_sender());
                    }
                }
                else if (msg->is_sender(actor))
                {
                    actor->returned(msg);
                }

                // Report the [actor, message[i,e]] back as 'done'
                work->done(this, actor, msg, i, e);
            }
        }
    };

} // namespace xcore
