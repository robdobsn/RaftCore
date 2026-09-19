/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftMainTask
// Check that code which is owned by the main task (the task running SysManager::loop()) is called from it
//
// Rob Dobson 2026
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Logger.h"
#include "RaftArduino.h"
#include "RaftThreading.h"

// Most framework state (SysMods, comms channels, web connections, config, etc) is owned by the main task
// and is not protected by locks. Other tasks must hand-off to the main task via a queue or an atomic.
// Place RAFT_CHECK_MAIN_TASK at entry points which are main-task-only to detect violations.
//
// By default a violation logs an error (throttled)
// Define RAFT_MAIN_TASK_CHECK_ABORT to abort() on violation (recommended for debug builds)
// Define RAFT_MAIN_TASK_CHECK_DISABLE to remove the checks

#if defined(RAFT_MAIN_TASK_CHECK_DISABLE)

#define RAFT_CHECK_MAIN_TASK(modulePrefix, fnName)

#else

#ifdef RAFT_MAIN_TASK_CHECK_ABORT
#include <stdlib.h>
#define RAFT_MAIN_TASK_CHECK_ACTION() abort()
#else
#define RAFT_MAIN_TASK_CHECK_ACTION()
#endif

#define RAFT_CHECK_MAIN_TASK(modulePrefix, fnName)                                              \
    do {                                                                                        \
        if (!RaftThread_isMainTask())                                                           \
        {                                                                                       \
            static uint32_t raftMainTaskCheckLastMs = 0;                                        \
            uint32_t raftMainTaskCheckNowMs = millis();                                         \
            if ((raftMainTaskCheckLastMs == 0) ||                                               \
                        (raftMainTaskCheckNowMs - raftMainTaskCheckLastMs > 5000))              \
            {                                                                                   \
                raftMainTaskCheckLastMs = raftMainTaskCheckNowMs;                               \
                LOG_E(modulePrefix, "%s called from task other than main - NOT THREAD SAFE", fnName); \
            }                                                                                   \
            RAFT_MAIN_TASK_CHECK_ACTION();                                                      \
        }                                                                                       \
    } while (0)

#endif
