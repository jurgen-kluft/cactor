#include "ccore/c_allocator.h"
#include "cactor/private/c_sema.h"

#include <thread>

#ifdef TARGET_PC
#    include <windows.h>
#    include <stdio.h>
#endif

#ifdef TARGET_MAC
#    include <mach/mach.h>
#    include <mach/task.h>
#    include <mach/semaphore.h>
#endif

namespace ncore
{

#ifdef TARGET_PC
    struct sys_sema_instance_t
    {
        HANDLE _sema;
    };

    sys_sema_t* create_sys_sema(alloc_t* allocator, s32 initial_count, s32 max_count)
    {
        sys_sema_instance_t* data = (sys_sema_instance_t*)allocator->allocate(sizeof(sys_sema_instance_t), alignof(sys_sema_instance_t));
        data->_sema               = CreateSemaphoreW(nullptr, initial_count, max_count, nullptr);
        if (!data->_sema)
        {
            // cannot create semaphore
            allocator->deallocate(data);
            return nullptr;
        }
        return (sys_sema_t*)data;
    }

    void destroy(alloc_t* allocator, sys_sema_t* semaphore)
    {
        sys_sema_instance_t* data = (sys_sema_instance_t*)semaphore;
        CloseHandle(data->_sema);
        allocator->deallocate(data);
    }

    void post(sys_sema_t* semaphore)
    {
        sys_sema_instance_t* data = (sys_sema_instance_t*)semaphore;
        if (!ReleaseSemaphore(data->_sema, 1, nullptr))
        {
            // cannot signal semaphore
        }
    }

    void post(sys_sema_t* semaphore, s32 count)
    {
        sys_sema_instance_t* data = (sys_sema_instance_t*)semaphore;
        ReleaseSemaphore(data->_sema, count, NULL);
    }

    void wait(sys_sema_t* semaphore)
    {
        sys_sema_instance_t* data = (sys_sema_instance_t*)semaphore;
        switch (WaitForSingleObject(data->_sema, INFINITE))
        {
            case WAIT_OBJECT_0: return;
            default:
                // wait for semaphore failed
                break;
        }
    }

    bool try_wait(sys_sema_t* semaphore, u32 milliseconds)
    {
        sys_sema_instance_t* data = (sys_sema_instance_t*)semaphore;
        switch (WaitForSingleObject(data->_sema, milliseconds))
        {
            case WAIT_OBJECT_0: return true;
            default:
                // wait for semaphore failed
                break;
        }
        return false;
    }

#elif defined(TARGET_MAC)
    struct sys_sema_instance_t
    {
        ::semaphore_t _sema;
    };

    sys_sema_t* create_sys_sema(alloc_t* allocator, s32 initial_count, s32 max_count)
    {
        sys_sema_instance_t* data = allocator->construct<sys_sema_instance_t>();
        semaphore_create(mach_task_self(), &data->_sema, SYNC_POLICY_FIFO, initial_count);
        return (sys_sema_t*)data;
    }

    void destroy(alloc_t* allocator, sys_sema_t* semaphore)
    {
        sys_sema_instance_t* data = (sys_sema_instance_t*)semaphore;
        semaphore_destroy(mach_task_self(), data->_sema);
        allocator->destruct<sys_sema_instance_t>(data);
    }

    void post(sys_sema_t* semaphore)
    {
        sys_sema_instance_t* data = (sys_sema_instance_t*)semaphore;
        semaphore_signal(data->_sema);
    }

    void post(sys_sema_t* semaphore, s32 count)
    {
        sys_sema_instance_t* data = (sys_sema_instance_t*)semaphore;
        while (count-- > 0)
        {
            semaphore_signal(data->_sema);
        }
    }

    void wait(sys_sema_t* semaphore)
    {
        sys_sema_instance_t* data = (sys_sema_instance_t*)semaphore;
        semaphore_wait(data->_sema);
    }

    bool try_wait(sys_sema_t* semaphore, u32 milliseconds)
    {
        sys_sema_instance_t* data = (sys_sema_instance_t*)semaphore;
        mach_timespec_t      ts;
        ts.tv_sec  = milliseconds / 1000;
        ts.tv_nsec = (milliseconds % 1000) * 1000000;
        return semaphore_timedwait(data->_sema, ts) == KERN_SUCCESS;
    }
#endif

    struct lw_sema_instance_t
    {
        std::atomic<s32>    m_count;
        sys_sema_instance_t m_sys_sema_instance;
    };

    lw_sema_t* create_lw_sema(alloc_t* allocator, s32 initial_count)
    {
        lw_sema_instance_t* data = allocator->construct<lw_sema_instance_t>();
        semaphore_create(mach_task_self(), &data->m_sys_sema_instance._sema, SYNC_POLICY_FIFO, initial_count);
        return (lw_sema_t*)data;
    }

    void destroy(alloc_t* allocator, lw_sema_t* semaphore)
    {
        lw_sema_instance_t* data = (lw_sema_instance_t*)semaphore;
        semaphore_destroy(mach_task_self(), data->m_sys_sema_instance._sema);
        allocator->destruct<lw_sema_instance_t>(data);
    }

    static void s_wait_with_partial_spinning(lw_sema_t* sema)
    {
        lw_sema_instance_t* s = (lw_sema_instance_t*)sema;

        // Is there a better way to set the initial spin count? Low values can have the code paths hit the kernal semaphore
        // a lot more often, too high values are burning electricity.
        s32 oldCount;
        s32 spin = 10000;
        while (spin--)
        {
            oldCount = s->m_count.load(std::memory_order_relaxed);
            if ((oldCount > 0) && s->m_count.compare_exchange_strong(oldCount, oldCount - 1, std::memory_order_acquire))
                return;
            std::atomic_signal_fence(std::memory_order_acquire); // Prevent the compiler from collapsing the loop.
        }
        oldCount = s->m_count.fetch_sub(1, std::memory_order_acquire);
        if (oldCount <= 0)
        {
            sys_sema_t* sys_sema = (sys_sema_t*)&s->m_sys_sema_instance;
            wait(sys_sema);
        }
    }

    bool try_wait(lw_sema_t* sema)
    {
        lw_sema_instance_t* s        = (lw_sema_instance_t*)sema;
        s32                 oldCount = s->m_count.load(std::memory_order_relaxed);
        return (oldCount > 0 && s->m_count.compare_exchange_strong(oldCount, oldCount - 1, std::memory_order_acquire));
    }

    void wait(lw_sema_t* sema)
    {
        if (!try_wait(sema))
            s_wait_with_partial_spinning(sema);
    }

    void post(lw_sema_t* sema, s32 count)
    {
        lw_sema_instance_t* s         = (lw_sema_instance_t*)sema;
        s32                 oldCount  = s->m_count.fetch_add(count, std::memory_order_release);
        s32                 toRelease = -oldCount < count ? -oldCount : count;
        if (toRelease > 0)
        {
            sys_sema_t* sys_sema = (sys_sema_t*)&s->m_sys_sema_instance;
            post(sys_sema, toRelease);
        }
    }

} // namespace ncore
