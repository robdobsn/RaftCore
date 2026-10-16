/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// BusIdentState.h
//
// Identification state of a bus element, the identity-change events reported to consumers, and the retry policy
// of the identification scheduler (Stage 7 of the device identification plan).
//
// Each element is identified on its own schedule: a new element at once, then, if that doesn't identify it, again
// after a delay that depends on the outcome:
// - Deferred (an identification hook asked to be asked again): every RETRY_DEFERRED_MS, a fixed interval, as hooks
//   may debounce on being re-asked (e.g. Robotical RSAO commissioning)
// - NoMatch (no record matched, and any hook said "not mine"): backing off from RETRY_NO_MATCH_MIN_MS to
//   RETRY_NO_MATCH_MAX_MS - and at once when the device type records change or a hook is registered
// - BusError (detection was interrupted by a bus error) and InitFailed (a bus error during initialisation): backing
//   off from RETRY_BUS_ERROR_MIN_MS to RETRY_BUS_ERROR_MAX_MS
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include "RaftDeviceConsts.h"

// Identification state of a bus element
enum class BusIdentState : uint8_t
{
    Pending = 0,        // to be identified (new, re-identification requested, or identification in progress)
    Identified = 1,
    Deferred = 2,       // an identification hook asked to be asked again
    NoMatch = 3,        // no record matched (and any hook said it isn't its device)
    BusError = 4,       // a bus error interrupted identification
    InitFailed = 5,     // identified, but a bus error interrupted initialisation (identification is repeated)
};

// Identity change reported to consumers (BusAddrStatus::identEvent)
enum class BusIdentEvent : uint8_t
{
    None = 0,
    Identified = 1,     // identified (prevDeviceTypeIndex is the type it had before, if it was re-identified)
    TypeChanged = 2,    // re-identified as a different type, while online
    Lost = 3,           // no longer identified (re-identification found nothing, or the device has gone)
};

class BusIdentSchedule
{
public:
    static constexpr uint32_t RETRY_DEFERRED_MS = 3000;
    static constexpr uint32_t RETRY_NO_MATCH_MIN_MS = 3000;
    static constexpr uint32_t RETRY_NO_MATCH_MAX_MS = 60000;
    static constexpr uint32_t RETRY_BUS_ERROR_MIN_MS = 100;
    static constexpr uint32_t RETRY_BUS_ERROR_MAX_MS = 10000;

    /// @brief Delay before identifying again after an outcome
    /// @param state outcome
    /// @param attempts number of consecutive attempts with this outcome (1 for the first)
    /// @return delay in ms (0 for Identified and Pending: nothing to retry, or at once)
    static uint32_t retryDelayMs(BusIdentState state, uint32_t attempts)
    {
        switch (state)
        {
            case BusIdentState::Deferred:
                return RETRY_DEFERRED_MS;
            case BusIdentState::NoMatch:
                return backoff(RETRY_NO_MATCH_MIN_MS, RETRY_NO_MATCH_MAX_MS, attempts);
            case BusIdentState::BusError:
            case BusIdentState::InitFailed:
                return backoff(RETRY_BUS_ERROR_MIN_MS, RETRY_BUS_ERROR_MAX_MS, attempts);
            default:
                return 0;
        }
    }

    /// @brief Check whether an element in a state needs identifying
    /// @param state state
    /// @param nextIdentMs time it is due (for a retry)
    /// @param timeNowMs time now
    /// @param catalogueChanged the device type records (or the identification hook) have changed since its last
    ///        attempt - a NoMatch element is then due at once
    static bool isDue(BusIdentState state, uint32_t nextIdentMs, uint32_t timeNowMs, bool catalogueChanged)
    {
        switch (state)
        {
            case BusIdentState::Identified:
                return false;
            case BusIdentState::NoMatch:
                return catalogueChanged || timeReached(timeNowMs, nextIdentMs);
            default:
                return timeReached(timeNowMs, nextIdentMs);
        }
    }

    /// @brief The identity change to report, from the type last reported and the type now
    /// @param reportedTypeIndex type last reported to consumers (DEVICE_TYPE_INDEX_INVALID if none)
    /// @param currentTypeIndex type now (DEVICE_TYPE_INDEX_INVALID if not identified)
    /// @return the event (None if nothing to report)
    static BusIdentEvent deriveEvent(DeviceTypeIndexType reportedTypeIndex, DeviceTypeIndexType currentTypeIndex)
    {
        bool wasIdentified = reportedTypeIndex != DEVICE_TYPE_INDEX_INVALID;
        bool isIdentified = currentTypeIndex != DEVICE_TYPE_INDEX_INVALID;
        if (isIdentified)
            return (wasIdentified && (reportedTypeIndex != currentTypeIndex)) ? BusIdentEvent::TypeChanged : BusIdentEvent::Identified;
        return wasIdentified ? BusIdentEvent::Lost : BusIdentEvent::None;
    }

    /// @brief Check whether a time (in ms, wrapping) has been reached
    static bool timeReached(uint32_t timeNowMs, uint32_t dueMs)
    {
        return (int32_t)(timeNowMs - dueMs) >= 0;
    }

    static const char* getStateStr(BusIdentState state)
    {
        switch (state)
        {
            case BusIdentState::Pending: return "pending";
            case BusIdentState::Identified: return "identified";
            case BusIdentState::Deferred: return "deferred";
            case BusIdentState::NoMatch: return "noMatch";
            case BusIdentState::BusError: return "busError";
            case BusIdentState::InitFailed: return "initFailed";
        }
        return "unknown";
    }

    static const char* getEventStr(BusIdentEvent event)
    {
        switch (event)
        {
            case BusIdentEvent::None: return "none";
            case BusIdentEvent::Identified: return "identified";
            case BusIdentEvent::TypeChanged: return "typeChanged";
            case BusIdentEvent::Lost: return "lost";
        }
        return "unknown";
    }

private:
    static uint32_t backoff(uint32_t minMs, uint32_t maxMs, uint32_t attempts)
    {
        uint32_t delayMs = minMs;
        for (uint32_t i = 1; (i < attempts) && (delayMs < maxMs); i++)
            delayMs *= 2;
        return delayMs < maxMs ? delayMs : maxMs;
    }
};
