#ifndef __CACTOR_ACTOR_H__
#define __CACTOR_ACTOR_H__
#include "ccore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

namespace ncore
{
    namespace nactor
    {
        struct system_t;
        struct mailbox_t;

        typedef void (*actor_process_fn)(void* user, msg_t* msg);
        typedef void (*actor_returned_fn)(void* user, msg_t*& msg);

        struct actor_t
        {
            s32               m_index;
            mailbox_t*        m_mailbox;
            actor_process_fn  m_process;
            actor_returned_fn m_returned;
            void*             m_user;
            msg_t*            m_free_messages_pool;
            system_t*         m_system;
            actor_t*          m_next_scheduled;
        };

        void init_actor(actor_t* actor, system_t* system, s32 index, void* user, actor_received_fn received, actor_process_fn process, actor_returned_fn returned);

    } // namespace nactor
} // namespace ncore

#endif // __CACTOR_ACTOR_H__
