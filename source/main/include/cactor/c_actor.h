#ifndef __C_ACTOR_ACTORMODEL_H__
#define __C_ACTOR_ACTORMODEL_H__
#include "ccore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

namespace ncore
{
    class alloc_t;

    namespace nactor
    {
        struct msg_t;
        struct actor_t;
        struct system_t;

        system_t* create_system(alloc_t* allocator, s32 num_threads, s32 max_actors, s32 max_messages, s32 max_producers);
        void      destroy_system(system_t* system);

        typedef void (*actor_received_fn)(void* user, msg_t* msg);
        typedef void (*actor_process_fn)(void* user, msg_t* msg);
        typedef void (*actor_returned_fn)(void* user, msg_t*& msg);

        actor_t* actor_join(system_t* system, void* user, actor_received_fn received, actor_process_fn process, actor_returned_fn returned);
        void     actor_leave(system_t* system, actor_t* actor);
        void     actor_send(actor_t* sender, msg_t* msg, actor_t* recipient);

    } // namespace nactor

} // namespace ncore

#endif // __C_ACTOR_ACTORMODEL_H__
