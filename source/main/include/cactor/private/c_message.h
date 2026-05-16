#ifndef __CACTOR_MESSAGE_H__
#define __CACTOR_MESSAGE_H__
#include "ccore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

namespace ncore
{
    namespace nactor
    {
        struct actor_t;

        typedef u64 msg_id_t;

        // For messages we can have one allocator per actor for sending messages.
        // This makes the actor be able to control/limit the messages that it
        // creates and sends.
        // The necessary information for a message is where the message came
        // from so that the receiving actor can send the message back to the
        // sender.
        // We base the receiving of messages on simple structs, messages are
        // always send back to the sender for garbage collection to simplify
        // creation, re-use and destruction of messages.

        msg_id_t get_msgid(const char*);

        struct msg_t
        {
            actor_t* m_sender;
            actor_t* m_recipient;
            msg_id_t m_id;
            void*    m_message;
            msg_t*   m_next; // For mailbox linked list
        };

        inline bool     is_sender(msg_t* msg, actor_t* s) { return msg->m_sender == s; }
        inline bool     is_recipient(msg_t* msg, actor_t* r) { return msg->m_recipient == r; }
        inline actor_t* get_sender(msg_t* msg) { return msg->m_sender; }
        inline actor_t* get_recipient(msg_t* msg) { return msg->m_recipient; }
        inline bool     has_id(msg_t* msg, msg_id_t _id) { return msg->m_id == _id; }

    } // namespace nactor
} // namespace ncore

#endif // __CACTOR_MESSAGE_H__
