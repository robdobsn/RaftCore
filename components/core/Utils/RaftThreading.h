/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftThreading
//
// Rob Dobson 2012-2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
    #define RAFT_THREAD_CPP_INIT = {}
#else
    #define RAFT_THREAD_CPP_INIT
#endif

#include <stdint.h>
#include <stdbool.h>

// Platform-independent mutex timeout constant
// Use this value to wait indefinitely for a mutex lock
static const uint32_t RAFT_MUTEX_WAIT_FOREVER = 0xFFFFFFFF;

// Platform-independent thread handle and mutex definitions
#if defined(MICROPY_PY_THREAD)

    // MicroPython platform
    #include "py/mpthread.h"

    // Mutex functions
    // MicroPython platform
    #include "py/mpthread.h"

    typedef struct {
        mp_thread_mutex_t mutex RAFT_THREAD_CPP_INIT;
    } RaftMutex;

    // Mutex functions
    void RaftMutex_init(RaftMutex &mutex);
    bool RaftMutex_lock(RaftMutex &mutex, uint32_t timeout_ms);
    void RaftMutex_unlock(RaftMutex &mutex);
    void RaftMutex_destroy(RaftMutex &mutex);

    // Thread handle
    static const mp_uint_t RAFT_THREAD_HANDLE_INVALID = 0;
    typedef mp_uint_t RaftThreadHandle;

    // Thread functions
    bool RaftThread_start(
        RaftThreadHandle& taskHandle,
        void (*pThreadFn)(void *), 
        void* pArg,
        size_t stackSize = 4000, 
        const char* pTaskName = nullptr, 
        int taskPriority = 0, 
        int taskCore = 0, 
        bool pinToCore = false);
    void RaftThread_sleep(uint32_t ms);

#elif defined(FREERTOS_CONFIG_H) || defined(FREERTOS_H) || defined(ESP_PLATFORM)

    // FreeRTOS platform
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"
    #include "freertos/semphr.h"
    typedef struct {
        SemaphoreHandle_t mutex RAFT_THREAD_CPP_INIT;
    } RaftMutex;

    // Mutex functions
    void RaftMutex_init(RaftMutex &mutex);
    bool RaftMutex_lock(RaftMutex &mutex, uint32_t timeout_ms);
    void RaftMutex_unlock(RaftMutex &mutex);
    void RaftMutex_destroy(RaftMutex &mutex);

    // Thread handle
    static const TaskHandle_t RAFT_THREAD_HANDLE_INVALID = nullptr;
    typedef TaskHandle_t RaftThreadHandle;
    
    // Thread functions
    bool RaftThread_start(
        RaftThreadHandle& taskHandle,
        void (*pThreadFn)(void *), 
        void* pArg,
        size_t stackSize = 4000, 
        const char* pTaskName = nullptr, 
        int taskPriority = configMAX_PRIORITIES - 1, 
        int taskCore = 0, 
        bool pinToCore = false);
    void RaftThread_sleep(uint32_t ms);

#elif defined(__linux__)

    // Linux platform using pthread
    #include <cstddef>
    #include <pthread.h>
    #include <semaphore.h>
    #include <time.h>
    #include <unistd.h>

    typedef struct {
        pthread_mutex_t mutex RAFT_THREAD_CPP_INIT;
    } RaftMutex;

    // Mutex functions
    void RaftMutex_init(RaftMutex &mutex);
    bool RaftMutex_lock(RaftMutex &mutex, uint32_t timeout_ms);
    void RaftMutex_unlock(RaftMutex &mutex);
    void RaftMutex_destroy(RaftMutex &mutex);

    // Thread handle
    static const pthread_t RAFT_THREAD_HANDLE_INVALID = 0;
    typedef pthread_t RaftThreadHandle;

    // Thread functions
    bool RaftThread_start(
        RaftThreadHandle& taskHandle,
        void (*pThreadFn)(void *), 
        void* pArg,
        size_t stackSize = 10000, 
        const char* pTaskName = nullptr, 
        int taskPriority = SCHED_OTHER, 
        int taskCore = 0, 
        bool pinToCore = false);
    void RaftThread_sleep(uint32_t ms);

#else

    #error "Unsupported platform for RaftThreading"

#endif


#ifdef __cplusplus
}
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Platform-independent atomics
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Memory ordering for atomic operations
typedef enum {
    RAFT_ATOMIC_RELAXED,  // No synchronization or ordering constraints
    RAFT_ATOMIC_ACQUIRE,  // Prevents memory reordering of subsequent reads/writes before this load
    RAFT_ATOMIC_RELEASE,  // Prevents memory reordering of prior reads/writes after this store
    RAFT_ATOMIC_SEQ_CST   // Full sequential consistency (acquire + release + total order)
} RaftAtomicOrdering;

// Platform-independent atomic bool
// Simple wrapper that provides atomic bool operations across platforms
typedef struct {
    volatile uint32_t value;
} RaftAtomicBool;

// Platform-independent atomic uint32
// Provides lock-free atomic operations for SPSC ring buffers
typedef struct {
    volatile uint32_t value;
} RaftAtomicUint32;

#ifdef __cplusplus
extern "C" {
#endif

// Atomic bool functions
void RaftAtomicBool_init(RaftAtomicBool &atomic, bool initialValue);
void RaftAtomicBool_set(RaftAtomicBool &atomic, bool value);
bool RaftAtomicBool_get(const RaftAtomicBool &atomic);

// Atomic uint32 functions
void RaftAtomicUint32_init(RaftAtomicUint32 &atomic, uint32_t initialValue);

#if defined(FREERTOS_CONFIG_H) || defined(FREERTOS_H) || defined(ESP_PLATFORM)
void IRAM_ATTR RaftAtomicUint32_store(RaftAtomicUint32 &atomic, uint32_t value, RaftAtomicOrdering ordering);
uint32_t IRAM_ATTR RaftAtomicUint32_load(const RaftAtomicUint32 &atomic, RaftAtomicOrdering ordering);
#else
void RaftAtomicUint32_store(RaftAtomicUint32 &atomic, uint32_t value, RaftAtomicOrdering ordering);
uint32_t RaftAtomicUint32_load(const RaftAtomicUint32 &atomic, RaftAtomicOrdering ordering);
#endif

#ifdef __cplusplus
}
#endif
