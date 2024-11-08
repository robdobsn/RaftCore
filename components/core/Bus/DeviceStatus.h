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
#include "PollDataAggregator.h"

class DeviceStatus
{
public:
    static const uint16_t DEVICE_TYPE_INDEX_INVALID = USHRT_MAX;

    DeviceStatus()
    {
    }

    void clear()
    {
        deviceTypeIndex = DEVICE_TYPE_INDEX_INVALID;
        deviceIdentPolling.clear();
        dataAggregator.clear();
    }

    bool isValid() const
    {
        return deviceTypeIndex != DEVICE_TYPE_INDEX_INVALID;
    }

    // Get pending ident poll info
    bool getPendingIdentPollInfo(uint64_t timeNowUs, DevicePollingInfo& pollInfo);

    /// @brief Store poll results
    /// @param timeNowUs time in us (passed in to aid testing)
    /// @param pollResult poll result data
    /// @param pPollInfo pointer to device polling info (maybe nullptr)
    /// @return true if result stored
    bool storePollResults(uint64_t timeNowUs, const std::vector<uint8_t>& pollResult, const DevicePollingInfo* pPollInfo)
    {
        return dataAggregator.put(timeNowUs, pollResult);
    }

    // Get device type index
    uint16_t getDeviceTypeIndex() const
    {
        return deviceTypeIndex;
    }

    // Device type index
    uint16_t deviceTypeIndex = DEVICE_TYPE_INDEX_INVALID;

    // Device ident polling - polling related to the device type
    DevicePollingInfo deviceIdentPolling;

    // Data aggregator
    PollDataAggregator dataAggregator;

    // Debug
    static constexpr const char* MODULE_PREFIX = "RaftI2CDevStat";    
};
