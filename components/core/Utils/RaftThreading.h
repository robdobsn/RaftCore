/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftUtils
//
// Rob Dobson 2012-2023
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

#include <stdbool.h>

// Platform-independent thread handle and mutex definitions
#if defined(MICROPY_PY_THREAD)

    // MicroPython platform
    #include "py/mpthread.h"

    typedef struct {
        mp_thread_mutex_t mutex RAFT_THREAD_CPP_INIT;
    } RaftMutex;

    typedef struct {
        TaskHandle_t task_handle RAFT_THREAD_CPP_INIT;
        const char* pTaskName RAFT_THREAD_CPP_INIT;
        size_t stackSize RAFT_THREAD_CPP_INIT;
        int taskPriority RAFT_THREAD_CPP_INIT;
        int taskCore RAFT_THREAD_CPP_INIT;
        bool pinToCore RAFT_THREAD_CPP_INIT;
        void *pArg RAFT_THREAD_CPP_INIT;    // Argument to pass to the thread
    } RaftThreadConfig;

    typedef struct {
        void *stack RAFT_THREAD_CPP_INIT; // stack size
        size_t stack_size RAFT_THREAD_CPP_INIT;
        void *arg RAFT_THREAD_CPP_INIT;  // Argument to pass to the thread
    } RaftThreadConfig;

    // Mutex functions
    void RaftMutex_init(RaftMutex *mutex)
    {
        if (mutex)
            mp_thread_mutex_init(&mutex->mutex);
    }
    bool RaftMutex_lock(RaftMutex *mutex, uint16_t timeout_ms)
    {
        if (mutex)
            return mp_thread_mutex_lock(&mutex->mutex, timeout_ms != 0) != 0;
        return false;
    }
    void RaftMutex_unlock(RaftMutex *mutex)
    {
        if (mutex)
            mp_thread_mutex_unlock(&mutex->mutex);
    }
    void RaftMutex_destroy(RaftMutex *mutex)
    {
    }

    // Thread functions
    bool RaftThread_start(void (*thread_func)(void *), RaftThreadConfig *config)
    {
        return mp_thread_create(thread_func, config->arg, config->stack_size, config->stack);
    }
    void RaftThread_sleep(uint32_t ms)
    {
        mp_thread_sleep(ms);
    }

#elif defined(FREERTOS_CONFIG_H) || defined(FREERTOS_H) || defined(ESP_PLATFORM)

    // FreeRTOS platform
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"
    #include "freertos/semphr.h"

    typedef struct {
        SemaphoreHandle_t mutex RAFT_THREAD_CPP_INIT;
    } RaftMutex;

    typedef struct {
        TaskHandle_t task_handle RAFT_THREAD_CPP_INIT;
        const char* pTaskName RAFT_THREAD_CPP_INIT;
        size_t stackSize RAFT_THREAD_CPP_INIT;
        int taskPriority RAFT_THREAD_CPP_INIT;
        int taskCore RAFT_THREAD_CPP_INIT;
        bool pinToCore RAFT_THREAD_CPP_INIT;
        void *pArg RAFT_THREAD_CPP_INIT;    // Argument to pass to the thread
    } RaftThreadConfig;

    // Mutex functions
    void RaftMutex_init(RaftMutex *mutex)
    {
        if (mutex)
            mutex->mutex = xSemaphoreCreateMutex();
    }
    bool RaftMutex_lock(RaftMutex *mutex, uint16_t timeout_ms)
    {
        if (mutex && mutex->mutex)
            return xSemaphoreTake(mutex->mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
        return false;
    }
    void RaftMutex_unlock(RaftMutex *mutex)
    {
        if (mutex && mutex->mutex)
            xSemaphoreGive(mutex->mutex);
    }
    void RaftMutex_destroy(RaftMutex *mutex)
    {
        if (mutex && mutex->mutex)
            vSemaphoreDelete(mutex->mutex);
    }

    // Thread functions
    bool RaftThread_start(void (*pThreadFunc)(void *), RaftThreadConfig *pConfig)
    {
        // TODO - shouldn't store the handle in the config
        //       should return it from the create function probably
        //       or store it in a separate struct
        if (!pConfig)
            return false;
        if (pConfig->pinToCore)
            return xTaskCreatePinnedToCore(pThreadFunc, 
                pConfig->pTaskName, 
                pConfig->stackSize, 
                pConfig->pArg, 
                pConfig->pTaskPriority, 
                &pConfig->task_handle,
                pConfig->taskCore) == pdPASS;
        else
            return xTaskCreate(pThreadFunc, 
                pConfig->pTaskName, 
                pConfig->stackSize, 
                pConfig->pArg, 
                pConfig->pTaskPriority, 
                &pConfig->task_handle) == pdPASS;

    }
    void RaftThread_sleep(uint32_t ms)
    {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }

#endif

#ifdef __cplusplus
}
#endif
