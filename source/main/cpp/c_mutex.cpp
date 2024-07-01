#include "ccore/c_allocator.h"
#include "cactor/private/c_mutex.h"
#include "cactor/private/c_sema.h"

#include <thread>

#ifdef TARGET_PC
#    include <windows.h>
#    include <stdio.h>
#endif

#ifdef TARGET_MAC
#    include <stdio.h>
#    include <mutex>
#endif

namespace ncore
{
#ifdef TARGET_MAC

    struct sys_mutex_instance_t
    {
        pthread_mutex_t _mutex;
    };

    sys_mutex_t* create_sys_mutex(alloc_t* allocator)
    {
        sys_mutex_instance_t* m = (sys_mutex_instance_t*)allocator->allocate(sizeof(sys_mutex_instance_t), alignof(sys_mutex_instance_t));
        memset(m, 0, sizeof(sys_mutex_instance_t));
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutex_init(&m->_mutex, &attr);
        pthread_mutexattr_destroy(&attr);
        return (sys_mutex_t*)m;
    }

    void destroy(alloc_t* allocator, sys_mutex_t* mutex)
    {
        sys_mutex_instance_t* m = (sys_mutex_instance_t*)mutex;
        pthread_mutex_destroy(&m->_mutex);
        allocator->deallocate(m);
    }

    void lock(sys_mutex_t* mutex)
    {
        sys_mutex_instance_t* m = (sys_mutex_instance_t*)mutex;
        pthread_mutex_lock(&m->_mutex);
    }

    bool try_lock(sys_mutex_t* mutex)
    {
        sys_mutex_instance_t* m = (sys_mutex_instance_t*)mutex;
        return pthread_mutex_trylock(&m->_mutex) == 0;
    }

    void unlock(sys_mutex_t* mutex)
    {
        sys_mutex_instance_t* m = (sys_mutex_instance_t*)mutex;
        pthread_mutex_unlock(&m->_mutex);
    }

#elif defined(TARGET_PC)

    struct sys_mutex_instance_t
    {
        CRITICAL_SECTION _cs;
    };

    sys_mutex_t* create_sys_mutex(alloc_t* allocator)
    {
        sys_mutex_instance_t* m = (sys_mutex_instance_t*)allocator->allocate(sizeof(sys_mutex_instance_t), alignof(sys_mutex_instance_t));
        memset(m, 0, sizeof(sys_mutex_instance_t));
        InitializeCriticalSectionAndSpinCount((CRITICAL_SECTION*)&m->_cs, 4000);
        return (sys_mutex_t*)m;
    }

    void destroy(alloc_t* allocator, sys_mutex_t* mutex)
    {
        sys_mutex_instance_t* m = (sys_mutex_instance_t*)mutex;
        DeleteCriticalSection((CRITICAL_SECTION*)&m->_cs);
        allocator->deallocate(m);
    }

    void lock(sys_mutex_t* mutex)
    {
        sys_mutex_instance_t* m = (sys_mutex_instance_t*)mutex;
        EnterCriticalSection((CRITICAL_SECTION*)&m->_cs);
    }

    bool try_lock(sys_mutex_t* mutex)
    {
        sys_mutex_instance_t* m = (sys_mutex_instance_t*)mutex;
        return TryEnterCriticalSection((CRITICAL_SECTION*)&m->_cs) != 0;
    }

    void unlock(sys_mutex_t* mutex)
    {
        sys_mutex_instance_t* m = (sys_mutex_instance_t*)mutex;
        LeaveCriticalSection((CRITICAL_SECTION*)&m->_cs);
    }

#endif

    // Non-recursive, light weight, mutex using std::atomic, spinning and a semaphore for blocking.
    struct lw_mutex_instance_t
    {
        std::atomic<s32> m_contentionCount;
        lw_sema_t*       m_sema;
    };

    lw_mutex_t* create_lw_mutex(alloc_t* allocator)
    {
        lw_mutex_instance_t* m = allocator->construct<lw_mutex_instance_t>();
        m->m_sema              = create_lw_sema(allocator, 0);
        return (lw_mutex_t*)m;
    }

    void destroy(alloc_t* allocator, lw_mutex_t* mutex)
    {
        lw_mutex_instance_t* m = (lw_mutex_instance_t*)mutex;
        destroy(allocator, m->m_sema);
        allocator->destruct<lw_mutex_instance_t>(m);
    }

    void lock(lw_mutex_t* mutex)
    {
        lw_mutex_instance_t* m = (lw_mutex_instance_t*)mutex;
        if (m->m_contentionCount.fetch_add(1, std::memory_order_acquire) > 0)
        {
            wait(m->m_sema);
        }
    }

    bool try_lock(lw_mutex_t* mutex)
    {
        lw_mutex_instance_t* m = (lw_mutex_instance_t*)mutex;
        if (m->m_contentionCount.load(std::memory_order_relaxed) != 0)
            return false;
        s32 expected = 0;
        return m->m_contentionCount.compare_exchange_strong(expected, 1, std::memory_order_acquire);
    }

    void unlock(lw_mutex_t* mutex)
    {
        lw_mutex_instance_t* m        = (lw_mutex_instance_t*)mutex;
        s32                  oldCount = m->m_contentionCount.fetch_sub(1, std::memory_order_release);
        ASSERT(oldCount > 0);
        if (oldCount > 1)
        {
            post(m->m_sema);
        }
    }

} // namespace ncore
