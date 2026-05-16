#ifndef __CACTOR_WORKER_QUEUE_H__
#define __CACTOR_WORKER_QUEUE_H__
#include "ccore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

namespace ncore
{
    namespace nactor
    {
        struct actor_t;

        struct worker_queue_t;
        worker_queue_t* worker_queue_create();
        void            worker_queue_shutdown(worker_queue_t* queue);

        void     worker_queue_push(worker_queue_t* queue, actor_t* actor);
        actor_t* worker_queue_pop(worker_queue_t* queue);
    } // namespace nactor
} // namespace ncore

#endif // __CACTOR_WORKER_QUEUE_H__
