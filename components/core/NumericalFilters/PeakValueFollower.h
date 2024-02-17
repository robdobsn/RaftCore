#pragma once/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
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
// This uses a linear leaky bucket to track the peak values of a signal over a period of time
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
        // Calculate time since last sample
        float timeSinceLastSampleSecs = (timeStampUs - _prevSampleTimeUs) / 1000000.0;

        // Handle positive peaks
        if (input > _positivePeakValue)
        {
            // Update the positive peak value
            _positivePeakValue = input;
            _positivePeakTimeUs = timeStampUs;
        }
        else
        {
            // Calculate the linear leaky bucket value - it tracks towards the input value
            _positivePeakValue = timeSinceLastSampleSecs < _timeFor100PercentDecayUs ?
                    _positivePeakValue - (_positivePeakValue - input) * (1.0 - timeSinceLastSampleSecs / _timeFor100PercentDecayUs) : 
                    input;
        }

        // Handle negative peaks
        if (input < _negativePeakValue)
        {
            // Update the negative peak value
            _negativePeakValue = input;
            _negativePeakTimeUs = timeStampUs;
        }
        else
        {
            // Calculate the linear leaky bucket value - it tracks towards the input value
            _negativePeakValue = timeSinceLastSampleSecs < _timeFor100PercentDecayUs ?
                    _negativePeakValue + (_negativePeakValue - input) * (1.0 - timeSinceLastSampleSecs / _timeFor100PercentDecayUs) : 
                    input;
        }

        // Store the previous sample
        _prevSampleValue = input;
        _prevSampleTimeUs = timeStampUs;

        // Return the positive peak value
        return _positivePeakValue;
    }

    INPUT_TYPE getPositivePeakValue() const
    {
        return _positivePeakValue;
    }

    TIME_STAMP_TYPE getPositivePeakTimeUs() const
    {
        return _positivePeakTimeUs;
    }

    INPUT_TYPE getNegativePeakValue() const
    {
        return _negativePeakValue;
    }

    TIME_STAMP_TYPE getNegativePeakTimeUs() const
    {
        return _negativePeakTimeUs;
    }

private:
    // Leaky bucket linear time constant
    TIME_STAMP_TYPE _timeFor100PercentDecayUs = 0;

    // Previous value
    INPUT_TYPE _prevSampleValue = 0;
    TIME_STAMP_TYPE _prevSampleTimeUs = 0;

    // Leaky bucket values for positive and negative peaks
    INPUT_TYPE _positivePeakValue = 0;
    TIME_STAMP_TYPE _positivePeakTimeUs = 0;
    INPUT_TYPE _negativePeakValue = 0;
    TIME_STAMP_TYPE _negativePeakTimeUs = 0;
};
