#ifndef __CACTOR_MUTEX_LOCK_H__
#define __CACTOR_MUTEX_LOCK_H__
#include "ccore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

namespace ncore
{
    class alloc_t;

    struct sys_mutex_t;
    sys_mutex_t* create_sys_mutex(alloc_t* allocator);
    void         destroy(alloc_t* allocator, sys_mutex_t* mutex);
    void         lock(sys_mutex_t* mutex);
    bool         try_lock(sys_mutex_t* mutex);
    void         unlock(sys_mutex_t* mutex);

    struct lw_mutex_t;
    lw_mutex_t* create_lw_mutex(alloc_t* allocator);
    void        destroy(alloc_t* allocator, lw_mutex_t* mutex);
    void        lock(lw_mutex_t* mutex);
    bool        try_lock(lw_mutex_t* mutex);
    void        unlock(lw_mutex_t* mutex);

} // namespace ncore

#endif // __CACTOR_MUTEX_LOCK_H__
