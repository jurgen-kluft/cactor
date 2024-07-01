#ifndef __C_ACTOR_ACTOR_H__
#define __C_ACTOR_ACTOR_H__
#include "ccore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

namespace ncore
{
    class alloc_t;

    namespace nactor
    {
        class message_t;
        class actor_t;
        typedef u64 id_t;

        // For messages we can have one allocator per actor for sending messages.
        // This makes the actor be able to control/limit the messages that it
        // creates and sends.
        // The necessary information for a message is where the message came
        // from so that the receiving actor can send the message back to the
        // sender.
        // We base the receiving of messages on simple structs, messages are
        // always send back to the sender for garbage collection to simplify
        // creation, re-use and destruction of messages.

        id_t get_msgid(const char*);

        class system_t;
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

        system_t* create_system(alloc_t* allocator, s32 num_threads, s32 max_actors, s32 max_messages, s32 max_producers);
        void      destroy_system(system_t* system);

        actor_t* actor_join(system_t* system, handler_t* handler);
        void     actor_leave(system_t* system, actor_t* actor);

        // Actors by themselves are able to send messages to other actors, however, the user would also
        // like to send messages to actors.
        // Thus, if as a user you want to send a message to an actor, then depending on the
        // thread you are on, that thread should have its own actor instance to use as the sender. This
        // actor will have to have its own pool of messages to use for sending and it will receive back those
        // messages for garbage collection. So the user will have to create a class derived from handler_t
        // and implement the received and returned functions. With an instance of that class the user can
        // call actor_join to get an actor instance that can be used as the sender for messages.
        // One thing to keep in mind though, is that the 'returned' function can be called at any time
        // so threadsafe considerations should be taken into account.
        void actor_send(system_t* system, actor_t* sender, message_t* msg, actor_t* recipient);

    } // namespace nactor

} // namespace ncore

#endif // __C_ACTOR_ACTOR_H__
