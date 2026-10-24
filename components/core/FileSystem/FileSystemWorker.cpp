/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// FileSystemWorker
//
// See FileSystemWorker.h
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "FileSystemWorker.h"
#include "Logger.h"
#include "RaftUtils.h"

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#endif

#define WARN_ON_WORKER_START_FAILED

// The worker
FileSystemWorker fileSystemWorker;

// The core the main task doesn't use (the worker runs there)
#ifdef ESP_PLATFORM
#if defined(CONFIG_FREERTOS_UNICORE) || !defined(CONFIG_ESP_MAIN_TASK_AFFINITY_CPU0)
static const int WORKER_CORE = 0;
#else
static const int WORKER_CORE = 1;
#endif
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor / Destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FileSystemWorker::FileSystemWorker()
{
    RaftMutex_init(_startMutex);
}

FileSystemWorker::~FileSystemWorker()
{
    // The task (if started) lives for the life of the program
    RaftMutex_destroy(_startMutex);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Submit a job
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool FileSystemWorker::submit(Job job)
{
    if (!job)
        return false;
#ifdef ESP_PLATFORM
    if (!ensureStarted())
        return false;
    Job* pJob = new Job(std::move(job));
    _jobsOutstanding++;
    if (xQueueSend((QueueHandle_t)_jobQueue, &pJob, 0) != pdTRUE)
    {
        _jobsOutstanding--;
        delete pJob;
        return false;
    }
    return true;
#else
    Job* pJob = new Job(std::move(job));
    _jobsOutstanding++;
    if (_runAtOnce)
    {
        runJob(pJob);
        return true;
    }
    if (_pendingJobs.size() >= QUEUE_LEN)
    {
        _jobsOutstanding--;
        delete pJob;
        return false;
    }
    _pendingJobs.push_back(pJob);
    return true;
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Run a job
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FileSystemWorker::runJob(Job* pJob)
{
    uint64_t startUs = micros();
    (*pJob)();
    delete pJob;
    uint32_t elapsedUs = (uint32_t)(micros() - startUs);
    if (elapsedUs > _jobMaxUs.load())
        _jobMaxUs = elapsedUs;
    _jobsRun++;
    _jobsOutstanding--;
}

#ifndef ESP_PLATFORM
uint32_t FileSystemWorker::runPending()
{
    uint32_t numRun = 0;
    // Jobs may submit more jobs, which then run too
    while (!_pendingJobs.empty())
    {
        Job* pJob = _pendingJobs.front();
        _pendingJobs.erase(_pendingJobs.begin());
        runJob(pJob);
        numRun++;
    }
    return numRun;
}
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Start the task
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool FileSystemWorker::ensureStarted()
{
#ifdef ESP_PLATFORM
    if (_taskStarted)
        return true;
    if (!RaftMutex_lock(_startMutex, RAFT_MUTEX_WAIT_FOREVER))
        return false;
    if (!_taskStarted)
    {
        if (!_jobQueue)
            _jobQueue = xQueueCreate(QUEUE_LEN, sizeof(Job*));
        // The stack is in internal RAM (xTaskCreatePinnedToCore) - a task writing to flash can't use PSRAM
        TaskHandle_t taskHandle = nullptr;
        if (_jobQueue && (xTaskCreatePinnedToCore(taskFn, "FSWorker", TASK_STACK_BYTES, this,
                    TASK_PRIORITY, &taskHandle, WORKER_CORE) == pdPASS))
        {
            _taskHandle = taskHandle;
            _taskStarted = true;
        }
#ifdef WARN_ON_WORKER_START_FAILED
        else
        {
            LOG_W(MODULE_PREFIX, "ensureStarted failed to create task (stack %d bytes)", (int)TASK_STACK_BYTES);
        }
#endif
    }
    RaftMutex_unlock(_startMutex);
    return _taskStarted;
#else
    return true;
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Task
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FileSystemWorker::taskFn(void* pArg)
{
#ifdef ESP_PLATFORM
    FileSystemWorker* pWorker = (FileSystemWorker*)pArg;
    while (true)
    {
        Job* pJob = nullptr;
        if (xQueueReceive((QueueHandle_t)pWorker->_jobQueue, &pJob, portMAX_DELAY) == pdTRUE && pJob)
            pWorker->runJob(pJob);
    }
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Debug
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String FileSystemWorker::getDebugJSON() const
{
    uint32_t stackFree = 0;
#ifdef ESP_PLATFORM
    if (_taskStarted)
        stackFree = uxTaskGetStackHighWaterMark((TaskHandle_t)_taskHandle);
#endif
    char json[120];
    snprintf(json, sizeof(json), R"({"started":%d,"jobs":%u,"pending":%u,"maxUs":%u,"stackFree":%u})",
                _taskStarted ? 1 : 0, (unsigned)_jobsRun.load(), (unsigned)_jobsOutstanding.load(),
                (unsigned)_jobMaxUs.load(), (unsigned)stackFree);
    return json;
}
