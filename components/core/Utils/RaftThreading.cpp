/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftThreading
//
// Rob Dobson 2012-2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdint.h>
#include <stdbool.h>
#include "RaftThreading.h"

// Platform-independent thread handle and mutex definitions
#if defined(MICROPY_PY_THREAD)

    // Mutex functions
    void RaftMutex_init(RaftMutex &mutex)
    {
        mp_thread_mutex_init(&mutex.mutex);
    }
    bool RaftMutex_lock(RaftMutex &mutex, uint32_t timeout_ms)
    {
        return mp_thread_mutex_lock(&mutex.mutex, timeout_ms != 0) != 0;
    }
    void RaftMutex_unlock(RaftMutex &mutex)
    {
        mp_thread_mutex_unlock(&mutex.mutex);
    }
    void RaftMutex_destroy(RaftMutex &mutex)
    {
        // No specific destroy action required in MicroPython
    }

    // Thread functions
    bool RaftThread_start(
        RaftThreadHandle& taskHandle,
        void (*pThreadFn)(void *), 
        void* pArg,
        size_t stackSize, 
        const char* pTaskName, 
        int taskPriority, 
        int taskCore, 
        bool pinToCore)
    {
        size_t stackSizeVar = stackSize;
        taskHandle = mp_thread_create(pThreadFn, pArg, &stackSizeVar);
        return true;
    }   
    void RaftThread_sleep(uint32_t ms)
    {
        mp_hal_delay_ms(ms);
    }

#elif defined(FREERTOS_CONFIG_H) || defined(FREERTOS_H) || defined(ESP_PLATFORM)

    // Mutex functions
    void RaftMutex_init(RaftMutex &mutex)
    {
        mutex.mutex = xSemaphoreCreateMutex();
    }
    bool RaftMutex_lock(RaftMutex &mutex, uint32_t timeout_ms)
    {
        uint32_t ticksToWait = timeout_ms;
        if (timeout_ms == UINT32_MAX)
            ticksToWait = portMAX_DELAY;
        else
            ticksToWait = pdMS_TO_TICKS(timeout_ms);
        return xSemaphoreTake(mutex.mutex, ticksToWait) == pdTRUE;
    }
    void RaftMutex_unlock(RaftMutex &mutex)
    {
        xSemaphoreGive(mutex.mutex);
    }
    void RaftMutex_destroy(RaftMutex &mutex)
    {
        if (mutex.mutex)
            vSemaphoreDelete(mutex.mutex);
    }

    // Thread functions
    bool RaftThread_start(
        RaftThreadHandle& taskHandle,
        void (*pThreadFn)(void *), 
        void* pArg,
        size_t stackSize, 
        const char* pTaskName, 
        int taskPriority, 
        int taskCore, 
        bool pinToCore)
    {
        if (pinToCore)
        {
            return (xTaskCreatePinnedToCore(pThreadFn, 
                pTaskName, 
                stackSize, 
                pArg, 
                taskPriority, 
                &taskHandle,
                taskCore)) == pdPASS;
        }
        return (xTaskCreate(pThreadFn, 
            pTaskName, 
            stackSize, 
            pArg, 
            taskPriority, 
            &taskHandle)) == pdPASS;
    }
    void RaftThread_sleep(uint32_t ms)
    {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }

#elif defined(__linux__)

    // Mutex functions
    void RaftMutex_init(RaftMutex &mutex)
    {
        pthread_mutex_init(&mutex.mutex, NULL);
    }
    bool RaftMutex_lock(RaftMutex &mutex, uint32_t timeout_ms)
    {
        if (timeout_ms == 0) {
            return pthread_mutex_trylock(&mutex.mutex) == 0;
        } else if (timeout_ms == UINT32_MAX) {
            return pthread_mutex_lock(&mutex.mutex) == 0;
        } else {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += timeout_ms / 1000;
            ts.tv_nsec += (timeout_ms % 1000) * 1000000;
            if (ts.tv_nsec >= 1000000000) {
                ts.tv_sec++;
                ts.tv_nsec -= 1000000000;
            }
            return pthread_mutex_timedlock(&mutex.mutex, &ts) == 0;
        }
    }
    void RaftMutex_unlock(RaftMutex &mutex)
    {
        pthread_mutex_unlock(&mutex.mutex);
    }
    void RaftMutex_destroy(RaftMutex &mutex)
    {
        pthread_mutex_destroy(&mutex.mutex);
    }

    // Thread functions
    bool RaftThread_start(
        RaftThreadHandle& taskHandle,
        void (*pThreadFn)(void *), 
        void* pArg,
        size_t stackSize, 
        const char* pTaskName, 
        int taskPriority, 
        int taskCore, 
        bool pinToCore
    )
    {
        return pthread_create(&taskHandle, NULL, (void *(*)(void *))pThreadFn, pArg) == 0;
    }
    void RaftThread_sleep(uint32_t ms)
    {
        usleep(ms * 1000); // Convert milliseconds to microseconds
    }

#endif // Platform-specific threading

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Atomic operations - platform independent
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Helper to convert RaftAtomicOrdering to platform-specific memory order
#if defined(ESP_PLATFORM) || defined(__linux__)
static inline int raftAtomicOrderingToBuiltin(RaftAtomicOrdering ordering)
{
    switch (ordering) {
        case RAFT_ATOMIC_RELAXED: return __ATOMIC_RELAXED;
        case RAFT_ATOMIC_ACQUIRE: return __ATOMIC_ACQUIRE;
        case RAFT_ATOMIC_RELEASE: return __ATOMIC_RELEASE;
        case RAFT_ATOMIC_SEQ_CST: return __ATOMIC_SEQ_CST;
        default: return __ATOMIC_SEQ_CST;
    }
}
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Atomic bool functions
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftAtomicBool_init(RaftAtomicBool &atomic, bool initialValue)
{
    atomic.value = initialValue ? 1 : 0;
}

void RaftAtomicBool_set(RaftAtomicBool &atomic, bool value)
{
#if defined(ESP_PLATFORM)
    // Use ESP-IDF atomic operations
    __atomic_store_n(&atomic.value, value ? 1 : 0, __ATOMIC_SEQ_CST);
#elif defined(__linux__)
    // Use GCC built-in atomic operations
    __atomic_store_n(&atomic.value, value ? 1 : 0, __ATOMIC_SEQ_CST);
#else
    // Fallback for other platforms - just use volatile
    atomic.value = value ? 1 : 0;
#endif
}

bool RaftAtomicBool_get(const RaftAtomicBool &atomic)
{
#if defined(ESP_PLATFORM)
    // Use ESP-IDF atomic operations
    return __atomic_load_n(&atomic.value, __ATOMIC_SEQ_CST) != 0;
#elif defined(__linux__)
    // Use GCC built-in atomic operations  
    return __atomic_load_n(&atomic.value, __ATOMIC_SEQ_CST) != 0;
#else
    // Fallback for other platforms - just use volatile
    return atomic.value != 0;
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Atomic uint32 functions
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftAtomicUint32_init(RaftAtomicUint32 &atomic, uint32_t initialValue)
{
    atomic.value = initialValue;
}

void RaftAtomicUint32_store(RaftAtomicUint32 &atomic, uint32_t value, RaftAtomicOrdering ordering)
{
#if defined(ESP_PLATFORM)
    // Use ESP-IDF atomic operations (available on Xtensa and RISC-V)
    __atomic_store_n(&atomic.value, value, raftAtomicOrderingToBuiltin(ordering));
#elif defined(__linux__)
    // Use GCC built-in atomic operations
    __atomic_store_n(&atomic.value, value, raftAtomicOrderingToBuiltin(ordering));
#else
    // Fallback for other platforms - just use volatile
    // This is NOT thread-safe but provides basic functionality
    (void)ordering;  // Suppress unused warning
    atomic.value = value;
#endif
}

uint32_t RaftAtomicUint32_load(const RaftAtomicUint32 &atomic, RaftAtomicOrdering ordering)
{
#if defined(ESP_PLATFORM)
    // Use ESP-IDF atomic operations (available on Xtensa and RISC-V)
    return __atomic_load_n(&atomic.value, raftAtomicOrderingToBuiltin(ordering));
#elif defined(__linux__)
    // Use GCC built-in atomic operations
    return __atomic_load_n(&atomic.value, raftAtomicOrderingToBuiltin(ordering));
#else
    // Fallback for other platforms - just use volatile
    // This is NOT thread-safe but provides basic functionality
    (void)ordering;  // Suppress unused warning
    return atomic.value;
#endif
}
