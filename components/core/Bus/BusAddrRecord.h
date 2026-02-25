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

    // Device data change callback and info
    RaftDeviceDataChangeCB dataChangeCB = nullptr;
    const void* pCallbackInfo = nullptr;

    // Handle responding
    bool handleResponding(bool isResponding, bool &flagSpuriousRecord, 
            uint32_t okMax = BusAddrStatus::ADDR_RESP_COUNT_OK_MAX_DEFAULT, 
            uint32_t failMax = BusAddrStatus::ADDR_RESP_COUNT_FAIL_MAX_DEFAULT);
    
    // Register for data change
    void registerForDataChange(RaftDeviceDataChangeCB dataChangeCB, uint32_t minTimeBetweenReportsMs, const void* pCallbackInfo)
    {
        this->dataChangeCB = dataChangeCB;
        this->pCallbackInfo = pCallbackInfo;
        this->minTimeBetweenReportsMs = minTimeBetweenReportsMs;
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

    // Get JSON for device status
    String getJson() const;

    // Debug
    static constexpr const char* MODULE_PREFIX = "BusAddrRecord";
};
