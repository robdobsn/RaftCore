/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Bus Address Record
//
// Rob Dobson 2025
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftDeviceConsts.h"
#include "DeviceStatus.h"
#include "RaftBusConsts.h"
#include "BusAddrStatus.h"
#include "BusIdentState.h"
#include "Logger.h"

/// @brief Full address record for internal bus status management
/// This is used to track the status of a bus address and to determine when to report changes in status
/// Contains full device status, response counting, access barring, and data change callbacks
class BusAddrRecord
{
public:
    /// @brief Default constructor
    BusAddrRecord() = default;

    /// @brief Constructor
    /// @param address 
    /// @param onlineState (ONLINE, OFFLINE, INITIAL) 
    /// @param isChange 
    /// @param isNewlyIdentified 
    BusAddrRecord(BusElemAddrType address, DeviceOnlineState onlineState, bool isChange, bool isNewlyIdentified, DeviceTypeIndexType deviceTypeIndex = DEVICE_TYPE_INDEX_INVALID) : 
        address(address), onlineState(onlineState), isChange(isChange), isNewlyIdentified(isNewlyIdentified)
    {
        deviceStatus.deviceTypeIndex = deviceTypeIndex;
    }

    // Address and slot
    BusElemAddrType address = 0;

    // Online/offline count
    int8_t count = 0;

    // State
    DeviceOnlineState onlineState : 3 = DeviceOnlineState::INITIAL;  // 3 bits for 4 states
    bool isChange : 1 = false;
    bool slotResolved : 1 = false;
    bool isNewlyIdentified : 1 = false;

    // Access barring
    uint32_t barStartMs = 0;
    uint16_t barDurationMs = 0;

    // Min between data change callbacks
    uint32_t minTimeBetweenReportsMs = 0;
    uint32_t lastDataChangeReportTimeMs = 0;

    // Device status
    DeviceStatus deviceStatus;

    // Identification (see BusIdentState.h): state, consecutive attempts with that outcome, when it is next due, the
    // device type records generation and hook epoch of the last attempt, and a re-verification request (after a poll
    // failure that the device recovered from)
    BusIdentState identState = BusIdentState::Pending;
    bool reverifyRequested : 1 = false;
    bool identChangePending : 1 = false;    // the identity may have changed since it was last reported
    bool reidentRequested : 1 = false;      // identify again (keeping the type until the result is known)
    uint8_t identAttempts = 0;
    uint8_t reidentAttempts = 0;
    uint32_t nextIdentMs = 0;
    uint32_t identGeneration = 0;
    uint32_t identHookEpoch = 0;

    // Type last reported to consumers (for identity-change events)
    DeviceTypeIndexType reportedTypeIndex = DEVICE_TYPE_INDEX_INVALID;

    // Device data change callback and info
    RaftDeviceDataChangeCB dataChangeCB = nullptr;
    const void* pCallbackInfo = nullptr;

    // Handle responding
    bool handleResponding(bool isResponding, bool &flagSpuriousRecord, 
            uint32_t okMax = BusAddrStatus::ADDR_RESP_COUNT_OK_MAX_DEFAULT, 
            uint32_t failMax = BusAddrStatus::ADDR_RESP_COUNT_FAIL_MAX_DEFAULT);
    
    // Register for data change
    /// @return false if this replaced a different subscriber's callback (identified by pCallbackInfo).
    /// A bus address holds ONE data callback: register through DeviceManager::registerForDeviceData,
    /// which owns this slot and fans data out to any number of subscribers.
    bool registerForDataChange(RaftDeviceDataChangeCB dataChangeCB, uint32_t minTimeBetweenReportsMs, const void* pCallbackInfo)
    {
        const bool displaced = dataChangeCB && this->dataChangeCB && (this->pCallbackInfo != pCallbackInfo);
        if (displaced)
            LOG_W("BusAddrRecord", "address 0x%x: data callback (info %p) replaced by another subscriber (info %p) - "
                  "register through DeviceManager::registerForDeviceData to share a device",
                  (unsigned)address, this->pCallbackInfo, pCallbackInfo);
        this->dataChangeCB = dataChangeCB;
        this->pCallbackInfo = pCallbackInfo;
        this->minTimeBetweenReportsMs = minTimeBetweenReportsMs;
        return !displaced;
    }

    // Get device data change callback
    RaftDeviceDataChangeCB getDataChangeCB() const
    {
        return dataChangeCB;
    }

    // Get device data change callback info
    const void* getCallbackInfo() const
    {
        return pCallbackInfo;
    }

    /// @brief Create lightweight status change for callbacks
    /// @return BusAddrStatus with essential fields only
    BusAddrStatus toStatusChange() const
    {
        return BusAddrStatus(address, onlineState, isChange, isNewlyIdentified, 
                             deviceStatus.deviceTypeIndex);
    }

    /// @brief Create the status change to report, with any identity change since the last report (which is then
    ///        the reported type)
    BusAddrStatus takeStatusChange()
    {
        bool isGone = (onlineState == DeviceOnlineState::OFFLINE) || (onlineState == DeviceOnlineState::PENDING_DELETION);
        DeviceTypeIndexType typeNow = isGone ? DEVICE_TYPE_INDEX_INVALID : deviceStatus.deviceTypeIndex;
        BusIdentEvent event = BusIdentEvent::None;
        DeviceTypeIndexType prevType = reportedTypeIndex;
        if (identChangePending || isNewlyIdentified || isGone)
            event = BusIdentSchedule::deriveEvent(reportedTypeIndex, typeNow);
        if (event != BusIdentEvent::None)
            reportedTypeIndex = typeNow;
        bool newlyIdentified = isNewlyIdentified && (typeNow != DEVICE_TYPE_INDEX_INVALID);
        BusAddrStatus status(address, onlineState, isChange, newlyIdentified, deviceStatus.deviceTypeIndex);
        status.identEvent = event;
        status.prevDeviceTypeIndex = (event == BusIdentEvent::None) ? DEVICE_TYPE_INDEX_INVALID : prevType;
        isChange = false;
        isNewlyIdentified = false;
        identChangePending = false;
        return status;
    }

    /// @brief Check whether there is a change to report
    bool hasStatusChange() const
    {
        return isChange || isNewlyIdentified || identChangePending;
    }

    // Get JSON for device status
    String getJson() const;

    // Debug
    static constexpr const char* MODULE_PREFIX = "BusAddrRecord";
};
