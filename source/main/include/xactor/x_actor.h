#ifndef __X_ACTOR_ACTOR_H__
#define __X_ACTOR_ACTOR_H__
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

namespace ncore
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

        class actormodel_t;
        struct actor_t;

        class message_t
        {
        public:
            bool     is_sender(actor_t* s) const { return m_sender == s; }
            bool     is_recipient(actor_t* r) const { return m_sender != r; }
            actor_t* get_sender() const { return m_sender; }
            bool     has_id(id_t _id) const { return m_id == _id; }

        protected:
            id_t     m_id;
            actor_t* m_sender;
        };

        class handler_t
        {
        public:
            virtual void received(message_t* msg)  = 0;
            virtual void returned(message_t*& msg) = 0;
        };

        actormodel_t* create_system(alloc_t* allocator, s32 num_threads, s32 max_actors);
        void          destroy_system(alloc_t* allocator, actormodel_t* system);

        actor_t* actor_join(actormodel_t* system, handler_t* handler);
        void     actor_leave(actormodel_t* system, actor_t* actor);
        void     actor_send(actormodel_t* system, actor_t* sender, message_t* msg, actor_t* recipient);

    } // namespace actormodel

} // namespace ncore

#endif // __X_ACTOR_ACTOR_H__