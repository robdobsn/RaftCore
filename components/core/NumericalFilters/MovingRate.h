/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Moving Rate
//
// Rob Dobson 2022
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <RaftArduino.h>
#include <RaftUtils.h>

/*
 * Moving Rate
 *
 * Calculate the rate of a value like bytes per second
 * The rate is calculated over a moving window of N samples
 * Uses Arduino-style millis() to get the time
 * 
 * Template N is the window size
 * Template input_t is the type of the input value
 * Template sum_t is the type of the sum
 * 
 */
template <uint32_t N, class input_t = uint32_t, class sum_t = uint64_t>
class MovingRate
{
public:
    MovingRate()
    {
        clear();
    }

    // sample
    void sample(input_t input) 
    {
        // Store sample
        _values[_headIdx] = input;
        _timestampsMs[_headIdx] = millis();

        // Circular buffer so increment head index
        if (++_headIdx == N)
            _headIdx = 0;

        // Increment used slot count
        if (_usedSlots < N)
            _usedSlots++;
    }

    // get rate
    double getRatePerSec()
    {
        // Check empty
        if (_usedSlots == 0)
            return 0;

        // Use modulo arithmetic to get the oldest and newest index
        uint32_t newestIdx = (_headIdx + N - 1) % N;
        uint32_t oldestIdx = (_usedSlots == N ? _headIdx : 0);

        // Time delta (using Raft::timeElapsed to handle millis() wraparound
        // This assumes that the real time delta is less than 2^31 ms
        uint32_t timeDeltaMs = Raft::timeElapsed(_timestampsMs[newestIdx], _timestampsMs[oldestIdx]);
        if (timeDeltaMs == 0)
            return 0;

        // Calculate rate
        return (_values[newestIdx] - _values[oldestIdx]) * 1000.0 / timeDeltaMs;
    }

    void clear()
    {
        _headIdx = 0;
        _usedSlots = 0;
    }

private:
    uint32_t _headIdx = 0;
    uint32_t _usedSlots = 0;
    input_t _values[N] = {};
    unsigned long _timestampsMs[N] = {};
};

