/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Peak value follower
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <cmath>

// Follow the peak values (negative and positive) of a signal
// This uses a linear leaky bucket to track the peak values (max and min) of a signal over a period of time
// If you are tracking a periodic signal then set the timeFor100PercentDecayUs to be several times the period
// of the signal (e.g. 10x the period)
template<typename INPUT_TYPE, typename TIME_STAMP_TYPE>
class PeakValueFollower
{
public:
    void setup(TIME_STAMP_TYPE timeFor100PercentDecayUs)
    {
        _timeFor100PercentDecayUs = timeFor100PercentDecayUs;
    }

    float sample(INPUT_TYPE input, TIME_STAMP_TYPE timeStampUs)
    {
        // First sample
        if (isFirstSample)
        {
            isFirstSample = false;
            _prevSampleValue = input;
            _prevSampleTimeUs = timeStampUs;
            _maxValueTracked = input;
            _minValueTracked = input;
            return input;
        }

        // Calculate time since last sample
        float timeSinceLastSampleUs = timeStampUs - _prevSampleTimeUs;

        // Handle max value tracking
        if (input > _maxValueTracked)
        {
            // Update the max value
            _maxValueTracked = input;
            _maxValueTimeUs = timeStampUs;
        }
        else
        {
            // Calculate the linear leaky bucket value - it tracks towards the input value
            float newPeak = timeSinceLastSampleUs < _timeFor100PercentDecayUs ?
                    _maxValueTracked - (_maxValueTracked - input) * (timeSinceLastSampleUs / _timeFor100PercentDecayUs) : 
                    input;
            _maxValueTracked = newPeak;
        }

        // Handle min values
        if (input < _minValueTracked)
        {
            // Update the negative peak value
            _minValueTracked = input;
            _minValueTimeUs = timeStampUs;
        }
        else
        {
            // Calculate the linear leaky bucket value - it tracks towards the input value
            _minValueTracked = timeSinceLastSampleUs < _timeFor100PercentDecayUs ?
                    _minValueTracked - (_minValueTracked - input) * (timeSinceLastSampleUs / _timeFor100PercentDecayUs) : 
                    input;
        }

        // Store the previous sample
        _prevSampleValue = input;
        _prevSampleTimeUs = timeStampUs;

        // Return the positive peak value
        return _maxValueTracked;
    }

    INPUT_TYPE getPositivePeakValue() const
    {
        return _maxValueTracked;
    }

    TIME_STAMP_TYPE getPositivePeakTimeUs() const
    {
        return _maxValueTimeUs;
    }

    INPUT_TYPE getNegativePeakValue() const
    {
        return _minValueTracked;
    }

    TIME_STAMP_TYPE getNegativePeakTimeUs() const
    {
        return _minValueTimeUs;
    }

private:
    // Leaky bucket linear time constant
    TIME_STAMP_TYPE _timeFor100PercentDecayUs = 1000;

    // Previous value
    bool isFirstSample = true;
    INPUT_TYPE _prevSampleValue = 0;
    TIME_STAMP_TYPE _prevSampleTimeUs = 0;

    // Leaky bucket values for min and max
    INPUT_TYPE _maxValueTracked = 0;
    TIME_STAMP_TYPE _maxValueTimeUs = 0;
    INPUT_TYPE _minValueTracked = 0;
    TIME_STAMP_TYPE _minValueTimeUs = 0;
};
