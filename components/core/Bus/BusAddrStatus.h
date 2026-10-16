/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Bus Address Status - Lightweight status change notification
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftBusConsts.h"
#include "RaftDeviceConsts.h"
#include "BusIdentState.h"

// Device online state
enum class DeviceOnlineState : uint8_t
{
    INITIAL = 0,          // Never confirmed online (might be spurious)
    ONLINE = 1,           // Currently responding
    OFFLINE = 2,          // Was online before, now offline
    PENDING_DELETION = 3  // Offline and marked for removal (will not return)
};

/// @brief Lightweight status change notification for bus element callbacks
/// Contains only the essential fields needed to communicate status changes
/// The full device record is maintained in BusAddrRecord
class BusAddrStatus
{
public:
    /// @brief Default constructor
    BusAddrStatus() = default;

    /// @brief Constructor
    /// @param address 
    /// @param onlineState (ONLINE, OFFLINE, INITIAL) 
    /// @param isChange 
    /// @param isNewlyIdentified 
    /// @param deviceTypeIndex 
    /// @param identEvent identity change (None: Identified if isNewlyIdentified with a valid type, as from a
    ///        producer that doesn't report identity changes)
    /// @param prevDeviceTypeIndex type before the change (for Identified after re-identification, TypeChanged and Lost)
    BusAddrStatus(BusElemAddrType address, DeviceOnlineState onlineState, bool isChange, bool isNewlyIdentified,
                DeviceTypeIndexType deviceTypeIndex = DEVICE_TYPE_INDEX_INVALID,
                BusIdentEvent identEvent = BusIdentEvent::None,
                DeviceTypeIndexType prevDeviceTypeIndex = DEVICE_TYPE_INDEX_INVALID) : 
        address(address), onlineState(onlineState), isChange(isChange), isNewlyIdentified(isNewlyIdentified),
        deviceTypeIndex(deviceTypeIndex), identEvent(identEvent), prevDeviceTypeIndex(prevDeviceTypeIndex)
    {
        if ((identEvent == BusIdentEvent::None) && isNewlyIdentified && (deviceTypeIndex != DEVICE_TYPE_INDEX_INVALID))
            this->identEvent = BusIdentEvent::Identified;
    }

    // Address
    BusElemAddrType address = 0;

    // State
    DeviceOnlineState onlineState : 3 = DeviceOnlineState::INITIAL;  // 3 bits for 4 states
    bool isChange : 1 = false;
    bool isNewlyIdentified : 1 = false;

    // Device type index (flat field for efficiency)
    DeviceTypeIndexType deviceTypeIndex = DEVICE_TYPE_INDEX_INVALID;

    // Identity change (Identified, TypeChanged or Lost - None if the identity hasn't changed) and the type before it.
    // isNewlyIdentified is also set for Identified and TypeChanged. A device going offline is reported as Lost if it
    // was identified
    BusIdentEvent identEvent = BusIdentEvent::None;
    DeviceTypeIndexType prevDeviceTypeIndex = DEVICE_TYPE_INDEX_INVALID;

    // Max failures before declaring a bus element offline (kept for API/test compatibility)
    static const uint32_t ADDR_RESP_COUNT_FAIL_MAX_DEFAULT = 3;

    // Max successes before declaring a bus element online (kept for API/test compatibility)
    static const uint32_t ADDR_RESP_COUNT_OK_MAX_DEFAULT = 2;

    // Get JSON for device status
    String getJson() const;

    // Get string for online state
    static const char* getOnlineStateStr(DeviceOnlineState onlineState)
    {
        switch (onlineState)
        {            
            case DeviceOnlineState::INITIAL:
                return "initial";
            case DeviceOnlineState::ONLINE:
                return "online";
            case DeviceOnlineState::OFFLINE:
                return "offline";
            case DeviceOnlineState::PENDING_DELETION:
                return "pending_deletion";
            default:
                return "unknown";
        }
    }

    // Debug
    static constexpr const char* MODULE_PREFIX = "BusAddrStatus";
};
