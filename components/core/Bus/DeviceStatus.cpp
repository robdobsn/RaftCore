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
/// @brief Get the time the next poll (or the next step of a split poll) is due
/// @param timeNowUs time in us (passed in to aid testing)
/// @param dueUs (out) time the next poll or step is due (may be in the past)
/// @return false if the device isn't polled
bool DeviceStatus::getNextPollDueUs(uint64_t timeNowUs, uint64_t& dueUs)
{
    DevicePollingInfo& polling = deviceIdentPolling;
    if (getPollReqs().size() == 0)
        return false;

    // The first poll is due one interval after the device is first checked
    if (!polling.pollScheduleStarted)
    {
        polling.nextPollDueUs = timeNowUs + DevicePollingInfo::clampPollIntervalUs(polling.pollIntervalUs);
        polling.pollScheduleStarted = true;
    }

    // A poll split by a pause continues when the pause has elapsed
    dueUs = (polling.partialPollNextReqIdx == 0) ? polling.nextPollDueUs : polling.nextStepDueUs;
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Advance the poll schedule if a poll (or a step of a split poll) is due
/// @param timeNowUs time in us (passed in to aid testing)
/// @return true if a poll is due
bool DeviceStatus::takeDuePoll(uint64_t timeNowUs)
{
    // Check if a full poll or the next step of a split poll is due
    uint64_t dueUs = 0;
    if (!getNextPollDueUs(timeNowUs, dueUs) || (timeNowUs < dueUs))
        return false;

    DevicePollingInfo& polling = deviceIdentPolling;
    if (polling.partialPollNextReqIdx == 0)
    {
        // Start of a full poll
        uint32_t intervalUs = DevicePollingInfo::clampPollIntervalUs(polling.pollIntervalUs);
        pollTimingStats.recordPollStart(timeNowUs, intervalUs);

        // The next poll is due one interval after this one was due, so lateness doesn't accumulate. If this
        // poll is a whole interval or more late, start again from now rather than polling in a burst to
        // catch up (the missed period is counted in the poll timing statistics)
        polling.nextPollDueUs += intervalUs;
        if (polling.nextPollDueUs <= timeNowUs)
            polling.nextPollDueUs = timeNowUs + intervalUs;
    }
    else
    {
        // Next step of a split poll. If the step fails it is retried after the same pause (a successful step
        // sets the due time for the step after it)
        polling.nextStepDueUs = timeNowUs + (uint64_t)polling.partialPollPauseAfterSendMs * 1000;
    }
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get the poll (or the next step of a split poll) that is due, if one is
/// @param timeNowUs time in us (passed in to aid testing)
/// @param job (out) the device's poll plan (shared, not copied), the first request to send and the bus speed
/// @return true if a poll is due
bool DeviceStatus::getPendingPollJob(uint64_t timeNowUs, DevicePollJob& job)
{
    if (!takeDuePoll(timeNowUs))
        return false;

    // Requests filled in since the plan was made (or with no plan yet) become the plan
    if (!pPollPlan || (deviceIdentPolling.pollReqs.size() > 0))
        buildPollPlan();
    job.pPlan = pPollPlan;
    job.nextReqIdx = deviceIdentPolling.partialPollNextReqIdx;
    job.pollBusHz = deviceIdentPolling.pollBusHz;
    job.pollBusHzSlotMask = deviceIdentPolling.pollBusHzSlotMask;
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get pending ident poll requests - as getPendingPollJob, with a copy of the polling info
/// @param timeNowUs time in us (passed in to aid testing)
/// @param pollInfo (out) polling info
/// @return true if there is a pending request
bool DeviceStatus::getPendingIdentPollInfo(uint64_t timeNowUs, DevicePollingInfo& pollInfo)
{
    if (!takeDuePoll(timeNowUs))
        return false;

    // Copy polling info (with the requests, which may be in the plan)
    pollInfo = deviceIdentPolling;
    if (pollInfo.pollReqs.size() == 0 && pPollPlan)
        pollInfo.pollReqs = pPollPlan->pollReqs;
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set the poll interval
/// @param pollIntervalUs poll interval in us (clamped to the minimum allowed)
/// @param timeNowUs time in us (passed in to aid testing)
void DeviceStatus::setPollIntervalUs(uint32_t pollIntervalUs, uint64_t timeNowUs)
{
    DevicePollingInfo& polling = deviceIdentPolling;
    polling.pollIntervalUs = DevicePollingInfo::clampPollIntervalUs(pollIntervalUs);

    // Bring the next poll forward if the new interval is shorter than the time left to it
    uint64_t dueWithNewIntervalUs = timeNowUs + polling.pollIntervalUs;
    if (polling.pollScheduleStarted && (polling.nextPollDueUs > dueWithNewIntervalUs))
        polling.nextPollDueUs = dueWithNewIntervalUs;
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
