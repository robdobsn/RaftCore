/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// DevicePollTimingStats.h
//
// How closely a device's polls keep to its configured poll interval.
//
// recordPollStart() is called when a full poll of the device starts (not for the later steps of a poll that
// is split by a pause), so the interval measured is from one poll start to the next. The difference between
// that interval and the configured interval is recorded in a histogram, so percentiles can be estimated
// without storing samples (bucket counts saturate at 65535). The bus time each poll takes (all its steps) is
// also recorded, so the share of the bus a device's polling uses can be worked out from measurements. Kept
// small (about 90 bytes) so it can always be enabled.
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include "RaftArduino.h"

class DevicePollTimingStats
{
public:
    // Histogram of (actual interval - configured interval). Bucket i holds errors in
    // [ERR_BUCKET_LOWER_US[i], ERR_BUCKET_LOWER_US[i+1]), the last bucket is unbounded above
    static constexpr uint32_t NUM_ERR_BUCKETS = 13;
    static constexpr int32_t ERR_BUCKET_LOWER_US[NUM_ERR_BUCKETS] = {
        INT32_MIN, -2000, -1000, -500, 0, 500, 1000, 2000, 4000, 8000, 16000, 32000, 64000 };

    /// @brief Record the start of a full poll
    /// @param timeNowUs time now
    /// @param configuredIntervalUs the poll interval the device is configured for
    void recordPollStart(uint64_t timeNowUs, uint32_t configuredIntervalUs)
    {
        _pollStartCount++;
        if (_started)
        {
            uint64_t intervalUs = timeNowUs - _lastStartUs;
            if (intervalUs > UINT32_MAX)
                intervalUs = UINT32_MAX;
            _intervalCount++;
            _sumIntervalUs += intervalUs;
            if ((_minIntervalUs == 0) || (intervalUs < _minIntervalUs))
                _minIntervalUs = intervalUs;
            if (intervalUs > _maxIntervalUs)
                _maxIntervalUs = intervalUs;
            int64_t errUs = (int64_t)intervalUs - (int64_t)configuredIntervalUs;
            uint16_t& bucketCount = _errHist[bucketFor(errUs)];
            if (bucketCount < UINT16_MAX)
                bucketCount++;
            // A whole poll period was missed
            if ((configuredIntervalUs > 0) && (intervalUs >= 2ULL * configuredIntervalUs))
                _missedPeriods++;
        }
        else
        {
            _firstStartUs = timeNowUs;
            _started = true;
        }
        _lastStartUs = timeNowUs;
        _configuredIntervalUs = configuredIntervalUs;
    }

    /// @brief Record the bus time taken by a poll, or by one step of a poll split by a pause
    /// @param busTimeUs time the poll (or step) occupied the bus, including slot and bus speed changes
    void recordBusTimeUs(uint32_t busTimeUs)
    {
        _sumBusTimeUs += busTimeUs;
    }

    /// @brief Mean bus time per poll (all its steps)
    /// @return mean in us, 0 if no poll has been measured
    uint32_t getMeanBusTimePerPollUs() const
    {
        return _pollStartCount ? (uint32_t)(_sumBusTimeUs / _pollStartCount) : 0;
    }

    /// @brief Reset the statistics (the next poll start begins a new measurement)
    void reset()
    {
        *this = DevicePollTimingStats();
    }

    /// @brief Number of intervals measured
    uint32_t getIntervalCount() const
    {
        return _intervalCount;
    }

    /// @brief Get statistics as a JSON object
    /// @return JSON e.g. {"cfgUs":50000,"n":200,"meanUs":51020,"minUs":50100,"maxUs":55300,"p95ErrUs":2000,
    ///         "missed":0,"rateHz":19.6,"busUs":1650,"errHist":[...]}. p95ErrUs is the upper bound of the
    ///         histogram bucket containing the 95th percentile of (actual - configured) interval; busUs is the
    ///         mean bus time per poll
    String getJson() const
    {
        double meanUs = _intervalCount ? (double)_sumIntervalUs / _intervalCount : 0;
        double spanUs = (double)(_lastStartUs - _firstStartUs);
        double rateHz = (spanUs > 0) ? (_intervalCount * 1000000.0 / spanUs) : 0;
        char buf[220];
        snprintf(buf, sizeof(buf),
                 R"({"cfgUs":%u,"n":%u,"meanUs":%.0f,"minUs":%u,"maxUs":%u,"p95ErrUs":%ld,"missed":%u,"rateHz":%.2f,"busUs":%u,"errHist":[)",
                 (unsigned)_configuredIntervalUs, (unsigned)_intervalCount, meanUs, (unsigned)_minIntervalUs,
                 (unsigned)_maxIntervalUs, (long)percentileErrUpperUs(95), (unsigned)_missedPeriods, rateHz,
                 (unsigned)getMeanBusTimePerPollUs());
        String json = buf;
        for (uint32_t i = 0; i < NUM_ERR_BUCKETS; i++)
        {
            if (i > 0)
                json += ",";
            json += String(_errHist[i]);
        }
        json += "]}";
        return json;
    }

    /// @brief Upper bound of the error bucket containing a percentile of the measured intervals
    /// @param percentile e.g. 95
    /// @return upper bound in us (the last bucket reports its lower bound)
    int32_t percentileErrUpperUs(uint32_t percentile) const
    {
        if (_intervalCount == 0)
            return 0;
        uint64_t target = ((uint64_t)_intervalCount * percentile + 99) / 100;
        uint64_t cumul = 0;
        for (uint32_t i = 0; i < NUM_ERR_BUCKETS; i++)
        {
            cumul += _errHist[i];
            if (cumul >= target)
                return (i + 1 < NUM_ERR_BUCKETS) ? ERR_BUCKET_LOWER_US[i + 1] : ERR_BUCKET_LOWER_US[i];
        }
        return ERR_BUCKET_LOWER_US[NUM_ERR_BUCKETS - 1];
    }

    /// @brief Histogram bucket for an interval error
    static uint32_t bucketFor(int64_t errUs)
    {
        uint32_t bucket = 0;
        for (uint32_t i = 1; i < NUM_ERR_BUCKETS; i++)
        {
            if (errUs >= ERR_BUCKET_LOWER_US[i])
                bucket = i;
        }
        return bucket;
    }

private:
    uint64_t _firstStartUs = 0;
    uint64_t _lastStartUs = 0;
    uint64_t _sumIntervalUs = 0;
    uint32_t _configuredIntervalUs = 0;
    uint32_t _intervalCount = 0;
    uint32_t _minIntervalUs = 0;
    uint32_t _maxIntervalUs = 0;
    uint32_t _missedPeriods = 0;
    uint64_t _sumBusTimeUs = 0;
    uint32_t _pollStartCount = 0;
    uint16_t _errHist[NUM_ERR_BUCKETS] = {};     // saturating counts
    bool _started = false;
};
