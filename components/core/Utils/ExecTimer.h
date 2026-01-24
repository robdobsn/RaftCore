/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// ExecTimer
// Timer for an execution path
//
// Rob Dobson 2018-2022
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <stddef.h>
#include "RaftUtils.h"
#include "RaftArduino.h"

// #define EXEC_TIMER_INCLUDE_CPU_TIME

#if defined(EXEC_TIMER_INCLUDE_CPU_TIME) && defined(ESP_PLATFORM)
#include <xtensa/hal.h>
#include "esp_private/esp_clk.h"
#endif

class ExecTimer
{
public:
    ExecTimer()
    {
        clear();
#if defined(EXEC_TIMER_INCLUDE_CPU_TIME) && defined(ESP_PLATFORM)
        // Get CPU speed
        _cpuSpeedMHz = esp_clk_cpu_freq() / 1000000;
#endif
    };

    void clear()
    {
        _execStartTimeUs = 0;
        _execMaxTimeUs = 0;
#if defined(EXEC_TIMER_INCLUDE_CPU_TIME) && defined(ESP_PLATFORM)
        _execStartCycles = 0;
        _execMaxCpuUs = 0;
#endif
    }

    // Record that execution has started
    inline void started()
    {
        _execStartTimeUs = micros();
#if defined(EXEC_TIMER_INCLUDE_CPU_TIME) && defined(ESP_PLATFORM)
        _execStartCycles = xthal_get_ccount();
#endif
    }

    // Record that execution has ended
    inline void ended()
    {
        unsigned long durUs = Raft::timeElapsed(micros(), _execStartTimeUs);
        if (_execMaxTimeUs < durUs)
            _execMaxTimeUs = durUs;
            
#if defined(EXEC_TIMER_INCLUDE_CPU_TIME) && defined(ESP_PLATFORM)
        uint32_t endCycles = xthal_get_ccount();
        uint32_t elapsedCycles = endCycles - _execStartCycles;
        uint32_t cpuTimeUs = elapsedCycles / (_cpuSpeedMHz);
        if (_execMaxCpuUs < cpuTimeUs)
            _execMaxCpuUs = cpuTimeUs;
#endif
    }

    // Valid?
    bool valid() const
    {
        return _execMaxTimeUs != 0;
    }

    // Get max elapsed time
    uint64_t getMaxUs() const
    {
        return _execMaxTimeUs;
    }

#if defined(EXEC_TIMER_INCLUDE_CPU_TIME) && defined(ESP_PLATFORM)
    // Get max CPU time
    uint64_t getMaxCpuUs() const
    {
        return _execMaxCpuUs;
    } 
#endif

    // Vars
    uint32_t _cpuSpeedMHz = 160;
    uint64_t _execStartTimeUs;
    uint64_t _execMaxTimeUs;
#if defined(EXEC_TIMER_INCLUDE_CPU_TIME) && defined(ESP_PLATFORM)
    uint32_t _execStartCycles;
    uint64_t _execMaxCpuUs;
#endif
};
