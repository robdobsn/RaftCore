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
        pollBusHz = 0;
        pollBusHzSlotMask = 0;
        pollResultSizeIncTimestamp = 0;
        pollCrcType = POLL_CRC_NONE;
        pollCrcTrailerLen = 0;
        pollCrcRetries = 0;
        pollCrcRereadCmd.clear();
        pollCrcRecoverCmd.clear();
        pollCrcRecoverAfter = 0;
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

    // Bus frequency Hz for this device's polls (0 = use bus default)
    uint32_t pollBusHz = 0;

    // Bitmask of slots where pollBusHz applies (0 = all slots)
    uint64_t pollBusHzSlotMask = 0;

    // Num poll results to store
    uint32_t numPollResultsToStore = 1;

    // Size of poll result (including timestamp)
    uint32_t pollResultSizeIncTimestamp = 0;

    // -- Poll response integrity (CRC + trailer) ------------------------------
    // Some devices append a trailer of [seq][crc...] after a declared response
    // length L, so a corrupted read can be detected rather than silently decoded.
    // The device zero-pads its response up to L, so the trailer is always at the
    // same offset and the CRC covers bytes 0..L inclusive (data + padding + seq).
    // The poll therefore reads L + trailerLen bytes; the trailer is stripped after
    // validation so the decode and the record's resp.b are unaffected.
    //
    // On a CRC failure a device that can re-send its previous response (needed when
    // the read is destructive, e.g. a FIFO pop) names a re-read command; otherwise
    // the burst is simply dropped. See RaftI2C
    // devdocs/i2c-poll-data-integrity-crc-plan.md.
    enum PollCrcType
    {
        POLL_CRC_NONE = 0,
        POLL_CRC_16_CCITT = 1,      // CRC-16/CCITT-FALSE, poly 0x1021, init 0xFFFF
    };
    PollCrcType pollCrcType = POLL_CRC_NONE;

    // Bytes of trailer appended after the declared length (3 for [seq][crcHi][crcLo])
    uint32_t pollCrcTrailerLen = 0;

    // Re-read attempts before dropping the response (0 = detect and drop)
    uint32_t pollCrcRetries = 0;

    // Bytes to write to request a re-send of the previous response (empty = none)
    std::vector<uint8_t> pollCrcRereadCmd;

    // Recovery backstop: bytes to write after pollCrcRecoverAfter consecutive dropped
    // responses, to nudge a device that has stopped producing valid data back into a
    // working state. Device-agnostic here - the record supplies the bytes.
    //
    // The motivating case: an RSAO that resets stays in its bootloader indefinitely,
    // because the master only sends START_APP during detection. The bootloader answers
    // polls from shared code but knows no application opcodes and emits no trailer, so
    // every poll fails CRC forever. Writing START_APP recovers it.
    std::vector<uint8_t> pollCrcRecoverCmd;
    uint32_t pollCrcRecoverAfter = 0;   // 0 = disabled

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
    static const uint32_t POLL_RESULT_WRAP_VALUE = (1 << (POLL_RESULT_TIMESTAMP_SIZE * 8));
    static const uint32_t POLL_RESULT_RESOLUTION_US = 100;
};
