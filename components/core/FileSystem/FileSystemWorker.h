/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// FileSystemWorker
//
// One task, shared by everything that does slow file system work (uploads, file hashing), so that the work
// doesn't block the main loop. Jobs run one at a time in the order they are submitted. The submitter checks for
// completion from the main task (e.g. a flag set by the job) - jobs must not call into code that expects the main
// task.
//
// The task is created on first use and kept (creating and deleting tasks fragments internal RAM). Its stack is in
// internal RAM: a task that writes to flash can't have its stack in PSRAM. It runs on the core the main task
// doesn't use.
//
// The main loop still stops while the flash is erased or written unless the app runs from PSRAM
// (CONFIG_SPIRAM_XIP_FROM_PSRAM) or the flash supports auto-suspend - see devdocs/flash-write-blocking-plan.md.
//
// Off the ESP32 (unit tests) jobs run when runPending() is called, or at once if setRunAtOnce(true).
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <functional>
#include <atomic>
#include <vector>
#include "RaftArduino.h"
#include "RaftThreading.h"

class FileSystemWorker
{
public:
    using Job = std::function<void()>;

    FileSystemWorker();
    ~FileSystemWorker();

    /// @brief Submit a job to run on the worker task
    /// @param job The job (it runs on the worker task)
    /// @return false if the job can't be queued (queue full or the task can't be created) - the caller must then
    ///         do the work itself or fail
    bool submit(Job job);

    /// @brief Jobs submitted and not yet finished
    uint32_t jobsOutstanding() const
    {
        return _jobsOutstanding.load();
    }

    /// @brief Debug JSON (jobs run, longest job, stack left)
    String getDebugJSON() const;

#ifndef ESP_PLATFORM
    // Unit tests: run the jobs queued so far (returns the number run)
    uint32_t runPending();
    void setRunAtOnce(bool runAtOnce)
    {
        _runAtOnce = runAtOnce;
    }
#endif

    // Stack (internal RAM), priority and queue length
    static const uint32_t TASK_STACK_BYTES = 4096;
    static const uint32_t TASK_PRIORITY = 1;
    static const uint32_t QUEUE_LEN = 8;

private:
    // Queue of jobs (each heap-allocated, deleted after it runs)
    void* _jobQueue = nullptr;
    RaftThreadHandle _taskHandle = 0;
    bool _taskStarted = false;
    RaftMutex _startMutex;

    // Stats
    std::atomic<uint32_t> _jobsOutstanding{0};
    std::atomic<uint32_t> _jobsRun{0};
    std::atomic<uint32_t> _jobMaxUs{0};

    // Start the task if not started
    bool ensureStarted();

    // Run one job (deleting it) and update stats
    void runJob(Job* pJob);

    // Task
    static void taskFn(void* pArg);

#ifndef ESP_PLATFORM
    std::vector<Job*> _pendingJobs;
    bool _runAtOnce = false;
#endif

    // Debug
    static constexpr const char* MODULE_PREFIX = "FSWorker";
};

extern FileSystemWorker fileSystemWorker;
