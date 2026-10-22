/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// PollPlan
//
// What a device's poll sends and how its responses are checked - the bus transactions and the response CRC
// settings - built once when the device is identified and then shared, unchanged, by every poll. The poll worker
// takes a DevicePollJob (the plan and the poll's progress), so a poll doesn't copy the device's polling info
// (device identification plan Stage 4, review §5.1).
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <memory>
#include <vector>
#include "DevicePollingInfo.h"

class PollPlan
{
public:
    /// @brief Make a plan from a device's polling info, moving its requests and response check commands into the
    ///        plan (so they aren't held twice)
    static std::shared_ptr<const PollPlan> takeFrom(DevicePollingInfo& pollingInfo)
    {
        auto pPlan = std::make_shared<PollPlan>();
        pPlan->pollReqs = std::move(pollingInfo.pollReqs);
        pollingInfo.pollReqs.clear();
        pPlan->pollCrcType = pollingInfo.pollCrcType;
        pPlan->pollCrcTrailerLen = pollingInfo.pollCrcTrailerLen;
        pPlan->pollCrcRetries = pollingInfo.pollCrcRetries;
        pPlan->pollCrcRereadCmd = pollingInfo.pollCrcRereadCmd;
        pPlan->pollCrcRecoverCmd = pollingInfo.pollCrcRecoverCmd;
        pPlan->pollCrcRecoverAfter = pollingInfo.pollCrcRecoverAfter;
        return pPlan;
    }

    // Bus transactions of a poll, in order
    std::vector<BusRequestInfo> pollReqs;

    // Response integrity (see DevicePollingInfo)
    DevicePollingInfo::PollCrcType pollCrcType = DevicePollingInfo::POLL_CRC_NONE;
    uint32_t pollCrcTrailerLen = 0;
    uint32_t pollCrcRetries = 0;
    std::vector<uint8_t> pollCrcRereadCmd;
    std::vector<uint8_t> pollCrcRecoverCmd;
    uint32_t pollCrcRecoverAfter = 0;
};

// One poll (or the next step of a poll split by a pause) for the poll worker
struct DevicePollJob
{
    // The device's plan (shared - it stays valid for the poll even if the device is identified again meanwhile)
    std::shared_ptr<const PollPlan> pPlan;

    // Index of the first request to send (0 for the start of a poll)
    uint32_t nextReqIdx = 0;

    // Bus frequency for the poll (0 for the bus default) and the slots it applies to (0 for all)
    uint32_t pollBusHz = 0;
    uint64_t pollBusHzSlotMask = 0;
};
