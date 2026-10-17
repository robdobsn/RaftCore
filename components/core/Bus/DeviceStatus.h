/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device status
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once 

#include "limits.h"
#include "RaftUtils.h"
#include "DevicePollingInfo.h"
#include "PollPlan.h"
#include "PollDataAggregatorIF.h"
#include "DevicePollTimingStats.h"
#include "RaftDeviceConsts.h"
#include <memory>

class DeviceStatus
{
public:
    DeviceStatus()
    {
    }

    ~DeviceStatus() = default;

    void clear()
    {
        deviceTypeIndex = DEVICE_TYPE_INDEX_INVALID;
        deviceIdentPolling.clear();
        pPollPlan.reset();
        pollTimingStats.reset();
        if (pDataAggregator)
            pDataAggregator->clear();
    }

    bool isValid() const
    {
        return deviceTypeIndex != DEVICE_TYPE_INDEX_INVALID;
    }

    /// @brief Get the time the next poll (or the next step of a split poll) is due
    /// @param timeNowUs time in us (passed in to aid testing)
    /// @param dueUs (out) time the next poll or step is due (may be in the past)
    /// @return false if the device isn't polled
    bool getNextPollDueUs(uint64_t timeNowUs, uint64_t& dueUs);

    /// @brief Set the poll interval
    /// @param pollIntervalUs poll interval in us (clamped to the minimum allowed)
    /// @param timeNowUs time in us (passed in to aid testing)
    void setPollIntervalUs(uint32_t pollIntervalUs, uint64_t timeNowUs);

    /// @brief Get the poll (or the next step of a split poll) that is due, if one is
    /// @param timeNowUs time in us (passed in to aid testing)
    /// @param job (out) the device's poll plan (shared, not copied), the first request to send and the bus speed
    /// @return true if a poll is due
    bool getPendingPollJob(uint64_t timeNowUs, DevicePollJob& job);

    /// @brief Get pending ident poll info - as getPendingPollJob, with a copy of the polling info (including the
    ///        requests)
    /// @param timeNowUs time in us (passed in to aid testing)
    /// @param pollInfo (out) polling info
    /// @return true if there is a pending request
    bool getPendingIdentPollInfo(uint64_t timeNowUs, DevicePollingInfo& pollInfo);

    /// @brief Make the poll plan from the polling info (its requests move into the plan). Done when the device
    ///        is identified; also done at the first poll after the polling info is filled in some other way
    void buildPollPlan()
    {
        pPollPlan = PollPlan::takeFrom(deviceIdentPolling);
    }

    /// @brief The poll requests (from the plan, or from the polling info if it has been filled since the plan was
    ///        made)
    const std::vector<BusRequestInfo>& getPollReqs() const
    {
        if (deviceIdentPolling.pollReqs.size() > 0 || !pPollPlan)
            return deviceIdentPolling.pollReqs;
        return pPollPlan->pollReqs;
    }

    /// @brief Store poll results
    /// @param nextReqIdx index of next request to store (0 = full poll, 1+ = partial poll)
    /// @param timeNowUs time in us (passed in to aid testing)
    /// @param pollResult poll result data
    /// @param pPollInfo pointer to device polling info (maybe nullptr)
    /// @param pauseAfterSendMs pause after send in ms
    /// @return true if result stored
    bool storePollResults(uint32_t nextReqIdx, uint64_t timeNowUs, const std::vector<uint8_t>& pollResult, 
        const DevicePollingInfo* pPollInfo, uint32_t pauseAfterSendMs);

    // Get device type index
    DeviceTypeIndexType getDeviceTypeIndex() const
    {
        return deviceTypeIndex;
    }

    // Get number of poll requests
    uint32_t getNumPollRequests() const
    {
        return getPollReqs().size();
    }

    /// @brief Get number of available poll responses
    uint32_t getPollRespCount() const
    {
        if (pDataAggregator)
            return pDataAggregator->count();
        return 0;
    }

    /// @brief Get poll responses
    /// @param devicePollResponseData (output) vector to store the device poll response data
    /// @param responseSize (output) size of the response data
    /// @param maxResponsesToReturn maximum number of responses to return (0 for no limit)
    /// @return number of responses returned
    uint32_t getPollResponses(std::vector<uint8_t>& devicePollResponseData, uint32_t& responseSize, uint32_t maxResponsesToReturn) const
    {
        if (pDataAggregator)            
            return pDataAggregator->get(devicePollResponseData, responseSize, maxResponsesToReturn);
        return 0;
    }

    /// @brief Get poll responses with per-sample actual lengths
    /// @param data (output) concatenated sample data (trimmed to actual lengths)
    /// @param lengths (output) actual length of each sample
    /// @param maxResponsesToReturn maximum number (0 = all available)
    /// @return number of samples returned
    uint32_t getPollResponsesWithLengths(std::vector<uint8_t>& data,
                                         std::vector<uint16_t>& lengths,
                                         uint32_t maxResponsesToReturn) const
    {
        if (pDataAggregator)
            return pDataAggregator->getWithLengths(data, lengths, maxResponsesToReturn);
        return 0;
    }

    /// @brief Get the latest poll response if it is new since the previous call (does not drain
    /// the stored results, but each value is reported once - see PollDataAggregatorIF)
    /// @param dataTimeUs (output) timestamp of the latest data in microseconds
    /// @param data (output) latest poll response data
    /// @return true if new data was available
    bool getLatestPollResponse(uint64_t& dataTimeUs, std::vector<uint8_t>& data) const
    {
        if (pDataAggregator)
            return pDataAggregator->getLatestValue(dataTimeUs, data);
        return false;
    }

    /// @brief Get the latest poll response whether or not it has been read before
    /// @param dataTimeUs (output) timestamp of the latest data in microseconds
    /// @param data (output) latest poll response data
    /// @return true if there is a latest response
    bool peekLatestPollResponse(uint64_t& dataTimeUs, std::vector<uint8_t>& data) const
    {
        if (pDataAggregator)
            return pDataAggregator->peekLatestValue(dataTimeUs, data);
        return false;
    }

    /// @brief Get and increment the per-device publish sequence counter
    /// @return current sequence counter value (pre-increment)
    uint8_t getAndIncrementSeqCounter() { return publishSeqCounter++; }

    /// @brief Set the data aggregator (shared ownership to allow safe copies of DeviceStatus)
    /// @param pAggregator 
    void setAndOwnPollDataAggregator(std::shared_ptr<PollDataAggregatorIF> pAggregator)
    {
        pDataAggregator = pAggregator;
    }

    // Device type index
    DeviceTypeIndexType deviceTypeIndex = DEVICE_TYPE_INDEX_INVALID;

    // Device ident polling - polling related to the device type (its requests are moved into pPollPlan)
    DevicePollingInfo deviceIdentPolling;

    // What a poll sends and how responses are checked - shared by every poll, never changed (a new plan is made
    // if the device is identified again)
    std::shared_ptr<const PollPlan> pPollPlan;

    // How closely polls keep to the configured interval (from one full poll start to the next)
    DevicePollTimingStats pollTimingStats;

    // Data aggregator
    std::shared_ptr<PollDataAggregatorIF> pDataAggregator;

    // Per-device publish sequence counter (wrapping uint8)
    uint8_t publishSeqCounter = 0;

    // Debug
    static constexpr const char* MODULE_PREFIX = "RaftI2CDevStat";

private:
    // Advance the poll schedule if a poll (or a step of a split poll) is due
    bool takeDuePoll(uint64_t timeNowUs);
};
