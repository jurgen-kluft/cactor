#ifndef __CACTOR_SEMAPHORE_H__
#define __CACTOR_SEMAPHORE_H__
#include "ccore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

namespace ncore
{
    class alloc_t;

    // System Semaphore
    struct sys_sema_t;
    sys_sema_t* create_sys_sema(alloc_t* allocator, s32 initial_count, s32 max_count);
    void        sema_destroy(alloc_t* allocator, sys_sema_t* semaphore);
    void        sema_wait(sys_sema_t* semaphore);
    bool        sema_try_wait(sys_sema_t* semaphore, u32 milliseconds);
    void        sema_post(sys_sema_t* semaphore);
    void        sema_post(sys_sema_t* semaphore, s32 count);

    // Lightweight Semaphore
    struct lw_sema_t;
    lw_sema_t*  create_lw_sema(alloc_t* allocator, s32 initial_count);
    void        sema_destroy(alloc_t* allocator, lw_sema_t* semaphore);
    void        sema_wait(lw_sema_t* semaphore);
    bool        sema_try_wait(lw_sema_t* semaphore);
    void        sema_post(lw_sema_t* semaphore, s32 count = 1);

} // namespace ncore

#endif // __CACTOR_SEMAPHORE_H__
