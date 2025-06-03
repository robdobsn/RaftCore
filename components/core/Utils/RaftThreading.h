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
    #include <pthread.h>
    #include <semaphore.h>
    #include <time.h>
    #include <unistd.h>
    #include <cstddef>

    typedef struct {
        pthread_mutex_t mutex RAFT_THREAD_CPP_INIT;
    } RaftMutex;

    // Mutex functions
    void RaftMutex_init(RaftMutex &mutex);
    bool RaftMutex_lock(RaftMutex &mutex, uint32_t timeout_ms);
    void RaftMutex_unlock(RaftMutex &mutex);
    void RaftMutex_destroy(RaftMutex &mutex);

    // Thread handle
    static const pthread_t RAFT_THREAD_HANDLE_INVALID = nullptr;
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
