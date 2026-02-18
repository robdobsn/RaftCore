/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device status
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "DeviceStatus.h"

// #define DEBUG_DEVICE_STATUS

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get pending ident poll requests 
/// @param timeNowUs time in us (passed in to aid testing)
/// @param pollInfo (out) polling info
/// @return true if there is a pending request
bool DeviceStatus::getPendingIdentPollInfo(uint64_t timeNowUs, DevicePollingInfo& pollInfo)
{
    // Check if this is the very first poll
    if (deviceIdentPolling.lastPollTimeUs == 0)
    {
        deviceIdentPolling.lastPollTimeUs = timeNowUs;
    }

    // Check if time to do full or partial poll (partial poll is when a pause after send has been requested)
    bool isStartOfPoll = (deviceIdentPolling.partialPollNextReqIdx == 0);
    uint32_t callIntervalUs = isStartOfPoll ? deviceIdentPolling.pollIntervalUs : deviceIdentPolling.partialPollPauseAfterSendMs * 1000;
    if (Raft::isTimeout(timeNowUs, deviceIdentPolling.lastPollTimeUs, callIntervalUs))
    {
        // Clear the poll result data if this is the start of the poll
        if (isStartOfPoll)
            pollInfo._pollDataResult.clear();

        // Update timestamp
        deviceIdentPolling.lastPollTimeUs = timeNowUs;

        // Check poll requests isn't empty
        if (deviceIdentPolling.pollReqs.size() == 0)
            return false;

        // Copy polling info
        pollInfo = deviceIdentPolling;

        // Return true
        return true;
    }

    // Nothing pending
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Store poll results
/// @param nextReqIdx index of next request to store (0 = full poll, 1+ = partial poll)
/// @param timeNowUs time in us (passed in to aid testing)
/// @param pollResult poll result data
/// @param pPollInfo pointer to device polling info (maybe nullptr)
/// @param pauseAfterSendMs pause after send in ms
/// @return true if result stored
bool DeviceStatus::storePollResults(uint32_t nextReqIdx, uint64_t timeNowUs, const std::vector<uint8_t>& pollResult, const DevicePollingInfo* pPollInfo, uint32_t pauseAfterSendMs)
{
    // Check we have a data aggregator to store the results
    if (!pDataAggregator)
        return false;

    // Check if this is a full or partial poll
    if (nextReqIdx != 0)
    {
        // Partial poll - store the partial poll result
        deviceIdentPolling.recordPartialPollResult(nextReqIdx, timeNowUs, pollResult, pauseAfterSendMs);
    }
    else
    {
        // Get the any partial poll results
        std::vector<uint8_t> partialPollResult;
        if (deviceIdentPolling.getPartialPollResultsAndClear(partialPollResult))
        {
            // Add the new poll result to the partial poll result
            partialPollResult.insert(partialPollResult.end(), pollResult.begin(), pollResult.end());

            // Add complete poll result to aggregator
            return pDataAggregator->put(timeNowUs, partialPollResult);
        }

        // Poll complete without partial poll - add result to aggregator
        return pDataAggregator->put(timeNowUs, pollResult);
    }
    return true;
}
