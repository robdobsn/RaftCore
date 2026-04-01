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
#include "PollDataAggregatorIF.h"
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
        if (pDataAggregator)
            pDataAggregator->clear();
    }

    bool isValid() const
    {
        return deviceTypeIndex != DEVICE_TYPE_INDEX_INVALID;
    }

    /// @brief Get pending ident poll info
    /// @param timeNowUs time in us (passed in to aid testing)
    /// @param pollInfo (out) polling info
    /// @return true if there is a pending request
    bool getPendingIdentPollInfo(uint64_t timeNowUs, DevicePollingInfo& pollInfo);

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
        return deviceIdentPolling.pollReqs.size();
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

    /// @brief Get and increment the per-device publish sequence counter
    /// @return current sequence counter value (pre-increment)
    uint8_t getAndIncrementSeqCounter() { return publishSeqCounter++; }

    /// @brief Set the data aggregator (shared ownership to allow safe copies of DeviceStatus)
    /// @param pAggregator 
    void setAndOwnPollDataAggregator(std::shared_ptr<PollDataAggregatorIF> pAggregator)
    {
        pDataAggregator = pAggregator;
    }

    // Debug FIFO overrun counter (per-device, cleared on read)
    void debugIncFifoOverrunCount(uint32_t inc = 1)
    {
        _debugFifoOverrunCount += inc;
    }

    uint32_t debugGetAndClearFifoOverrunCount()
    {
        uint32_t count = _debugFifoOverrunCount;
        _debugFifoOverrunCount = 0;
        return count;
    }

    // Debug FIFO full counter (per-device, cleared on read)
    void debugIncFifoFullCount(uint32_t inc = 1)
    {
        _debugFifoFullCount += inc;
    }

    uint32_t debugGetAndClearFifoFullCount()
    {
        uint32_t count = _debugFifoFullCount;
        _debugFifoFullCount = 0;
        return count;
    }

    // Device type index
    DeviceTypeIndexType deviceTypeIndex = DEVICE_TYPE_INDEX_INVALID;

    // Device ident polling - polling related to the device type
    DevicePollingInfo deviceIdentPolling;

    // Data aggregator
    std::shared_ptr<PollDataAggregatorIF> pDataAggregator;

    // Per-device publish sequence counter (wrapping uint8)
    uint8_t publishSeqCounter = 0;

    // Debug FIFO overrun counter
    uint32_t _debugFifoOverrunCount = 0;

    // Debug FIFO full counter
    uint32_t _debugFifoFullCount = 0;

    // Debug
    static constexpr const char* MODULE_PREFIX = "RaftI2CDevStat";    
};
