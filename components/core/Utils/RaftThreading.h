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

    typedef mp_uint_t RaftThreadHandle;

    // Thread functions
    bool RaftThread_start(
        RaftThreadHandle& taskHandle,
        void (*pThreadFn)(void *), 
        void* pArg,
        size_t stackSize = 0, 
        const char* pTaskName = nullptr, 
        int taskPriority = 0, 
        int taskCore = 0, 
        bool pinToCore = false)
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

    // FreeRTOS platform
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"
    #include "freertos/semphr.h"
    typedef struct {
        SemaphoreHandle_t mutex RAFT_THREAD_CPP_INIT;
    } RaftMutex;

    // Mutex functions
    void RaftMutex_init(RaftMutex &mutex)
    {
        mutex.mutex = xSemaphoreCreateMutex();
    }
    bool RaftMutex_lock(RaftMutex &mutex, uint32_t timeout_ms)
    {
        return xSemaphoreTake(mutex.mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
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

    typedef TaskHandle_t RaftThreadHandle;
    
    // Thread functions
    bool RaftThread_start(
        RaftThreadHandle& taskHandle,
        void (*pThreadFn)(void *), 
        void* pArg,
        size_t stackSize = 0, 
        const char* pTaskName = nullptr, 
        int taskPriority = 0, 
        int taskCore = 0, 
        bool pinToCore = false
    {
        if (pinToCore)
        {
            return (xTaskCreatePinnedToCore(pThreadFn, 
                pTaskName, 
                stackSize, 
                pArg, 
                taskPriority, 
                &taskHandle,
                taskCore) == pdPASS;
        }
        return (xTaskCreate(pThreadFn, 
            pTaskName, 
            stackSize, 
            pArg, 
            taskPriority, 
            &taskHandle) == pdPASS;
    }
    void RaftThread_sleep(uint32_t ms)
    {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }

#elif defined(__linux__)

    // Linux platform using pthread
    #include <pthread.h>
    #include <semaphore.h>
    #include <time.h>
    #include <unistd.h>


    typedef struct {
        pthread_mutex_t mutex RAFT_THREAD_CPP_INIT;
    } RaftMutex;

    // Mutex functions
    void RaftMutex_init(RaftMutex &mutex)
    {
        pthread_mutex_init(&mutex.mutex, NULL);
    }
    bool RaftMutex_lock(RaftMutex &mutex, uint32_t timeout_ms)
    {
        if (timeout_ms == 0) {
            return pthread_mutex_trylock(&mutex.mutex) == 0;
        } else if (timeout_ms == UINT16_MAX) {
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

    typedef pthread_t RaftThreadHandle;

    // Thread functions
    bool RaftThread_start(
        RaftThreadHandle& taskHandle,
        void (*pThreadFn)(void *), 
        void* pArg,
        size_t stackSize = 0, 
        const char* pTaskName = nullptr, 
        int taskPriority = 0, 
        int taskCore = 0, 
        bool pinToCore = false
    )
    {
        return pthread_create(&taskHandle, NULL, (void *(*)(void *))pThreadFn, pArg) == 0;
    }
    void RaftThread_sleep(uint32_t ms)
    {
        usleep(ms * 1000); // Convert milliseconds to microseconds
    }

#else

    #error "Unsupported platform for RaftThreading"

#endif


#ifdef __cplusplus
}
#endif
