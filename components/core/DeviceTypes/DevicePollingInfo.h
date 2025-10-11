/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device Polling Info
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <list>
#include "BusRequestInfo.h"

class DevicePollingInfo 
{
public:
    DevicePollingInfo()
    {
    }

    void clear()
    {
        lastPollTimeUs = 0;
        pollIntervalUs = 0;
        pollResultSizeIncTimestamp = 0;
        pollReqs.clear();
    }

    void recordPartialPollResult(uint32_t nextReqIdx, uint64_t timeNowUs, const std::vector<uint8_t>& pollResult, uint32_t pauseAfterSendMs)
    {
        partialPollNextReqIdx = nextReqIdx;
        partialPollPauseAfterSendMs = pauseAfterSendMs;
        // Check length of poll result is within limits
        if (_pollDataResult.size() + pollResult.size() <= pollResultSizeIncTimestamp)
        {
            _pollDataResult.insert(_pollDataResult.end(), pollResult.begin(), pollResult.end());
        }
    }

    bool getPartialPollResultsAndClear(std::vector<uint8_t>& pollResult)
    {
        pollResult = _pollDataResult;
        _pollDataResult.clear();
        partialPollNextReqIdx = 0;
        partialPollPauseAfterSendMs = 0;
        return true;
    }

    // cmdId used for ident-polling
    static const uint32_t DEV_IDENT_POLL_CMD_ID = UINT32_MAX;

    // Last poll time
    uint64_t lastPollTimeUs = 0;

    // Poll interval
    uint32_t pollIntervalUs = 0;

    // Num poll results to store
    uint32_t numPollResultsToStore = 1;

    // Size of poll result (including timestamp)
    uint32_t pollResultSizeIncTimestamp = 0;

    // Poll request rec
    std::vector<BusRequestInfo> pollReqs;

    // Poll data result
    std::vector<uint8_t> _pollDataResult;

    // Partially completed poll next request index
    // 0 = not in partial poll state
    // 1+ = index of next request to send
    uint32_t partialPollNextReqIdx = 0;

    // Partial poll pause after send ms
    uint32_t partialPollPauseAfterSendMs = 0;

    // Poll result timestamp size
    static const uint32_t POLL_RESULT_TIMESTAMP_SIZE = 2;
    static const uint32_t POLL_RESULT_WRAP_VALUE = 2^(POLL_RESULT_TIMESTAMP_SIZE*8);
    static const uint32_t POLL_RESULT_RESOLUTION_US = 1000;
};
