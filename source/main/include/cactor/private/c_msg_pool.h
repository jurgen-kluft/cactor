#ifndef __CACTOR_MESSAGE_POOL_H__
#define __CACTOR_MESSAGE_POOL_H__
#include "ccore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

namespace ncore
{
    namespace nactor
    {
        struct msg_t; // Forward declaration

        struct msg_pool_t;

        // Allocation & Lifetime
        msg_pool_t* msg_pool_create();
        void        msg_pool_destroy(msg_pool_t* pool);

        // Global Setup: Tie raw preallocated memory arrays to the pool structure
        void msg_pool_populate(msg_pool_t* pool, msg_t* storage_array, size_t capacity);

        // Lock-free Pool Operations
        msg_t* msg_pool_pop(msg_pool_t* pool);
        void   msg_pool_push(msg_pool_t* pool, msg_t* msg);

    } // namespace nactor
} // namespace ncore

#endif // __CACTOR_MESSAGE_POOL_H__
