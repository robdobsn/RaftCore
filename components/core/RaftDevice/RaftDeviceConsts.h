/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device Consts
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <limits.h>
#include <vector>
#include <functional>
#include "RaftArduino.h"

class RaftDevice;
class BusAddrStatus;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Type used to identify an address
typedef uint32_t BusElemAddrType;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Type used to identify a bus
typedef uint32_t BusNumType;

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
typedef uint16_t DeviceTypeIndexType;
static const DeviceTypeIndexType DEVICE_TYPE_INDEX_INVALID = USHRT_MAX;

/// @brief Devide identification type
class RaftDeviceID
{
public:

    // Special value for bus number (for devices not on a bus)
    static const uint32_t BUS_NUM_DIRECT_CONN = 0;

    // Starting number for regular buses (for devices on a bus)
    static const uint32_t BUS_NUM_FIRST_BUS = 1;

    // Special value for bus number (for devices on any bus)
    static const uint32_t BUS_NUM_ALL_DEVICES_ANY_BUS = UINT32_MAX;

    // Special value for an "invalid" bus number (used for invalid IDs)
    static const uint32_t BUS_NUM_INVALID = UINT32_MAX - 1;

    /// @brief Constructor
    RaftDeviceID(BusNumType busNum = BUS_NUM_INVALID, BusElemAddrType address = 0) : 
        busNum(busNum), address(address)
    {
    }

    /// @brief Check if this is a valid ID
    /// @return true if valid    
    bool isValid() const
    {
        return busNum != BUS_NUM_INVALID;
    }

    /// @brief Check if this is an "any device" ID
    /// @return true if so
    bool isAnyDevice() const
    {
        return (busNum == BUS_NUM_ALL_DEVICES_ANY_BUS);
    }

    /// @brief Equality operator
    /// @param other Other DeviceIDType to compare against
    /// @return true if equal
    bool operator==(const RaftDeviceID& other) const
    {
        return (busNum == other.busNum) && (address == other.address);
    }

    /// @brief Inequality operator
    /// @param other Other DeviceIDType to compare against
    /// @return true if not equal
    bool operator!=(const RaftDeviceID& other) const
    {
        return !(*this == other);
    }

    /// @brief Convert to string
    /// @return String representation of the DeviceIDType
    String toString() const
    {
        if (isAnyDevice())
            return "ANY";
        else if (busNum == BUS_NUM_DIRECT_CONN)
            return "0_" + String(address, 16);
        else
            return String(busNum) + "_" + String(address, 16);
    }

    /// @brief Convert from string
    /// @param str String representation of the DeviceIDType
    /// @return DeviceIDType object
    static RaftDeviceID fromString(const String& str)
    {
        if (str.equalsIgnoreCase("ANY"))
        {
            return RaftDeviceID(BUS_NUM_ALL_DEVICES_ANY_BUS, 0);
        }
        String addressStr = str;
        BusNumType busNum = BUS_NUM_DIRECT_CONN;
        int underscoreIndex = str.indexOf('_');
        if (underscoreIndex > 0)
        {                
            String busNumStr = str.substring(0, underscoreIndex);
            addressStr = str.substring(underscoreIndex + 1);
            busNum = busNumStr.toInt();
        }

        // Check for leading "0x" in address and remove it if present
        if (addressStr.startsWith("0x") || addressStr.startsWith("0X"))
        {
            addressStr = addressStr.substring(2);
        }   
        BusElemAddrType address = strtoul(addressStr.c_str(), nullptr, 16);
        return RaftDeviceID(busNum, address);
    }

    /// @brief Get bus number
    /// @return Bus number
    BusNumType getBusNum() const
    {
        return busNum;
    }

    /// @brief Get address
    /// @return Address
    BusElemAddrType getAddress() const
    {
        return address;
    }

private:
    BusNumType busNum = BUS_NUM_INVALID;
    BusElemAddrType address = 0;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Callback type for device data change
typedef std::function<void(uint16_t deviceTypeIdx, std::vector<uint8_t> data, const void* pCallbackInfo)> RaftDeviceDataChangeCB;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Callback type for device status change
/// @param device Device whose status has changed
/// @param addrStatus Address and status of the bus element that changed
typedef std::function<void(RaftDevice& device, const BusAddrStatus& addrStatus)> RaftDeviceStatusChangeCB;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Callback type for device event
typedef std::function<void(RaftDevice& device, const char* eventName, const char* eventData)> RaftDeviceEventCB;