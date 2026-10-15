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
#include <string.h>
#include <ctype.h>
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

    // All devices on any bus (used for callbacks that apply to all devices)
    static const uint32_t ALL_DEVICES_ANY_BUS = UINT32_MAX;

    // Special value for an "invalid ID" (e.g. for uninitialized devices)
    static const uint32_t INVALID = UINT32_MAX - 1;

    /// @brief Constructor
    RaftDeviceID(BusNumType busNum = RaftDeviceID::INVALID, BusElemAddrType address = 0) : 
        busNum(busNum), address(address)
    {
    }

    /// @brief Check if this is a valid ID
    /// @return true if valid    
    bool isValid() const
    {
        return busNum != RaftDeviceID::INVALID;
    }

    /// @brief Check if this is an "any device" ID
    /// @return true if so
    bool isAnyDevice() const
    {
        return (busNum == RaftDeviceID::ALL_DEVICES_ANY_BUS);
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
    /// @note Accepted forms are "<busNum>_<address>", "<address>" (bus 0, i.e. directly connected) and "ANY".
    ///       busNum is decimal, address is hex (1 to 8 digits, optional 0x prefix). Anything else (e.g. a
    ///       device name, a non-numeric bus or trailing characters) gives an invalid ID
    static RaftDeviceID fromString(const String& str)
    {
        if (str.equalsIgnoreCase("ANY"))
        {
            return RaftDeviceID(RaftDeviceID::ALL_DEVICES_ANY_BUS, 0);
        }
        const char* pStr = str.c_str();
        BusNumType busNum = RaftDeviceID::BUS_NUM_DIRECT_CONN;
        const char* pUnderscore = strchr(pStr, '_');
        if (pUnderscore)
        {
            // Bus number: decimal digits only, and not one of the special values
            if (pUnderscore == pStr)
                return RaftDeviceID();
            uint64_t busNumVal = 0;
            for (const char* p = pStr; p < pUnderscore; p++)
            {
                if ((*p < '0') || (*p > '9'))
                    return RaftDeviceID();
                busNumVal = busNumVal * 10 + (*p - '0');
                if (busNumVal >= RaftDeviceID::INVALID)
                    return RaftDeviceID();
            }
            busNum = (BusNumType)busNumVal;
            pStr = pUnderscore + 1;
        }

        // Address: optional 0x prefix then 1 to 8 hex digits and nothing else
        if ((pStr[0] == '0') && ((pStr[1] == 'x') || (pStr[1] == 'X')))
            pStr += 2;
        size_t numDigits = strlen(pStr);
        if ((numDigits == 0) || (numDigits > 2 * sizeof(BusElemAddrType)))
            return RaftDeviceID();
        for (size_t i = 0; i < numDigits; i++)
        {
            if (!isxdigit((unsigned char)pStr[i]))
                return RaftDeviceID();
        }
        BusElemAddrType address = strtoul(pStr, nullptr, 16);
        return RaftDeviceID(busNum, address);
    }

    /// @brief Get bus number
    /// @return Bus number
    BusNumType getBusNum() const
    {
        return busNum;
    }

    /// @brief Get bus number string
    /// @return Bus number as a string    
    String getBusNumStr() const
    {
        return String(busNum);
    }

    /// @brief Get address
    /// @return Address
    BusElemAddrType getAddress() const
    {
        return address;
    }

    /// @brief Get address as string
    /// @return Address as a string
    String getAddressStr() const
    {
        char addrHex[16];
        snprintf(addrHex, sizeof(addrHex), "%x", (unsigned int)address);
        return String(addrHex);
    }

private:
    BusNumType busNum = RaftDeviceID::INVALID;
    BusElemAddrType address = 0;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Callback type for device data change
/// @note TASK CONTEXT: for bus devices this callback is made on the bus worker task (e.g. the I2C worker) and
///       NOT on the main task - so it may run in parallel with the subscriber's loop() (on another core).
///       The callback must be short, must not block and must hand data off to the main task in a thread-safe
///       way (e.g. ThreadSafeQueue or atomics) rather than touching state owned by the main task.
typedef std::function<void(uint16_t deviceTypeIdx, std::vector<uint8_t> data, const void* pCallbackInfo)> RaftDeviceDataChangeCB;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Callback type for device status change
/// @param device Device whose status has changed
/// @param addrStatus Address and status of the bus element that changed
/// @note TASK CONTEXT: this callback is made on the main task (from the bus's loop())
typedef std::function<void(RaftDevice& device, const BusAddrStatus& addrStatus)> RaftDeviceStatusChangeCB;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Callback type for device event
typedef std::function<void(RaftDevice& device, const char* eventName, const char* eventData)> RaftDeviceEventCB;