#ifndef __X_ACTOR_ACTOR_H__
#define __X_ACTOR_ACTOR_H__
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

namespace xcore
{
    class alloc_t;

    namespace actormodel
    {
        class message_t;
        class messages_t;
        class actor_t;
        class work_t;
        class worker_t;
        typedef u64 id_t;

        class worker_thread_t
        {
        public:
            virtual bool quit() const = 0;
        };

        class isemaphore_t
        {
        public:
            virtual void setup(s32 initial, s32 maximum) = 0;
            virtual void teardown()                      = 0;

            virtual void request() = 0;
            virtual void release() = 0;
        };

        class imutex_t
        {
        public:
            virtual void setup()    = 0;
            virtual void teardown() = 0;

            virtual void lock()   = 0;
            virtual void unlock() = 0;
        };

        // For messages we can have one allocator per actor for sending messages.
        // This makes the actor be able to control/limit the messages that it
        // creates and sends.
        // The necessary information for a message is where the message came
        // from so that the receiving actor can send a message back to the
        // sender.
        // We base the receiving of messages on simple structs, messages are
        // always send back to the sender for garbage collection to simplify
        // creation, re-use and destruction of messages.

        id_t get_msgid(const char*);

        class message_t
        {
        public:
            bool is_sender(actor_t* s) const { return m_sender == s; }
            bool is_recipient(actor_t* r) const { return m_sender != r; }
            actor_t* get_sender() const { return m_sender; }
            bool has_id(id_t _id) const { return m_id == _id; }

        protected:
            id_t     m_id;
            actor_t* m_sender;
        };

        class mailbox_t
        {
        public:
            virtual void send(message_t* msg, actor_t* recipient) = 0;
        };

        class messages_t
        {
        public:
            virtual s32  push(message_t* msg)                      = 0; // return==1 size was '0' before push
            virtual void claim(u32& idx, u32& end)                 = 0; // claim a message batch
            virtual void deque(u32& idx, u32 end, message_t*& msg) = 0; // next message from the claimed batch
            virtual s32  release(u32 idx, u32 end)                 = 0; // release batch, return 'count'
        };

        class actor_t
        {
        public:
            virtual void       setmailbox(mailbox_t* mailbox) = 0;
            virtual mailbox_t* getmailbox()                   = 0;

            virtual void received(message_t* msg)  = 0;
            virtual void returned(message_t*& msg) = 0;
        };

        class work_t
        {
        public:
            virtual void add(actor_t* sender, message_t* msg, actor_t* recipient) = 0;

            virtual void take(worker_t* worker, actor_t*& actor, message_t*& msg, u32& idx_begin, u32& idx_end) = 0;
            virtual void done(worker_t* worker, actor_t*& actor, message_t*& msg, u32& idx_begin, u32& idx_end) = 0;
        };

        class worker_t
        {
        public:
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
            virtual void tick(worker_thread_t* thread, work_t* work, ctxt_t* ctx) = 0;
            virtual void run(worker_thread_t* thread, work_t* work)               = 0;
        };

        worker_t* create_worker(alloc_t* allocator);
        void      destroy_worker(alloc_t* allocator, worker_t* worker);

        class actormodel_t
        {
        public:
            virtual void start() = 0;
            virtual void stop()  = 0;

            virtual void join(actor_t* actor)  = 0;
            virtual void leave(actor_t* actor) = 0;
        };

        actormodel_t* create_system(alloc_t* allocator, u32 num_threads);
        void          destroy_system(alloc_t* allocator, actormodel_t* system);

        void send(actormodel_t* system, actor_t* sender, message_t* msg, actor_t* recipient);
    } // namespace actormodel

} // namespace xcore

#endif // __X_ACTOR_ACTOR_H__