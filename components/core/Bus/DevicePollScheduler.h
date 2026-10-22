/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// DevicePollScheduler.h
//
// Chooses which device to poll next: of the devices whose poll (or next poll step) is due, the one that has
// been due longest - earliest deadline first. Devices due at the same time take turns, because the search
// starts after the device chosen last. Each device's due time comes from its DeviceStatus, which advances it
// by the poll interval when a poll starts (see DeviceStatus::getPendingIdentPollInfo()).
//
// The records can be any random access container; a function gets the DeviceStatus from a record (or nullptr
// if the record isn't to be polled now, e.g. the device is offline), so the same scheduler serves any bus's
// record type. Not thread safe - the caller holds its records' lock.
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include "DeviceStatus.h"

class DevicePollScheduler
{
public:
    static constexpr int32_t NONE_DUE = -1;

    /// @brief Find the record whose poll is due with the earliest due time
    /// @param records container of records
    /// @param getDeviceStatus function returning a DeviceStatus* for a record (nullptr to skip the record)
    /// @param timeNowUs time now in us
    /// @return index of the record to poll, or NONE_DUE
    /// @note call markPolled() with the index when the poll is taken, so ties go round robin
    template <typename Records, typename GetDeviceStatus>
    int32_t findEarliestDue(Records& records, GetDeviceStatus getDeviceStatus, uint64_t timeNowUs) const
    {
        const uint32_t numRecs = records.size();
        int32_t earliestIdx = NONE_DUE;
        uint64_t earliestDueUs = 0;
        for (uint32_t i = 0; i < numRecs; i++)
        {
            uint32_t recIdx = (_searchStartIdx + i) % numRecs;
            uint64_t dueUs = 0;
            DeviceStatus* pDeviceStatus = getDeviceStatus(records[recIdx]);
            if (!pDeviceStatus || !pDeviceStatus->getNextPollDueUs(timeNowUs, dueUs) || (dueUs > timeNowUs))
                continue;
            if ((earliestIdx == NONE_DUE) || (dueUs < earliestDueUs))
            {
                earliestIdx = recIdx;
                earliestDueUs = dueUs;
            }
        }
        return earliestIdx;
    }

    /// @brief Find the earliest time any record's poll (or poll step) is due
    /// @param records container of records
    /// @param getDeviceStatus function returning a DeviceStatus* for a record (nullptr to skip the record)
    /// @param timeNowUs time now in us
    /// @param dueUs (out) earliest due time (may be in the past)
    /// @return false if no record is polled
    template <typename Records, typename GetDeviceStatus>
    static bool getEarliestDueUs(Records& records, GetDeviceStatus getDeviceStatus, uint64_t timeNowUs,
                uint64_t& dueUs)
    {
        bool anyPolled = false;
        for (auto& record : records)
        {
            uint64_t recDueUs = 0;
            DeviceStatus* pDeviceStatus = getDeviceStatus(record);
            if (!pDeviceStatus || !pDeviceStatus->getNextPollDueUs(timeNowUs, recDueUs))
                continue;
            if (!anyPolled || (recDueUs < dueUs))
                dueUs = recDueUs;
            anyPolled = true;
        }
        return anyPolled;
    }

    /// @brief Record that the poll of a record was taken (the next search starts after it)
    void markPolled(int32_t recIdx)
    {
        if (recIdx >= 0)
            _searchStartIdx = recIdx + 1;
    }

private:
    uint32_t _searchStartIdx = 0;
};
