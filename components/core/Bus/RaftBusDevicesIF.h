/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Bus Devices Interface
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <vector>
#include "RaftArduino.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Device decode state
/// @class BusDeviceDecodeState
class BusDeviceDecodeState
{
public:
    uint64_t lastReportTimestampUs = 0;
    uint64_t reportTimestampOffsetUs = 0;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Bus Devices Interface
/// @class RaftBusDevicesIF
class RaftBusDevicesIF
{
public:

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get list of device addresses attached to the bus
    /// @param pAddrList pointer to array to receive addresses
    /// @param onlyAddressesWithIdentPollResponses true to only return addresses with ident poll responses
    virtual void getDeviceAddresses(std::vector<uint32_t>& addresses, bool onlyAddressesWithIdentPollResponses) const = 0;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device type information by address
    /// @param address address of device to get information for
    /// @param includePlugAndPlayInfo true to include plug and play information
    /// @return JSON string
    virtual String getDevTypeInfoJsonByAddr(uint32_t address, bool includePlugAndPlayInfo) const = 0;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device type information by device type name
    /// @param deviceType - device type name
    /// @param includePlugAndPlayInfo - true to include plug and play information
    /// @return JSON string
    virtual String getDevTypeInfoJsonByTypeName(const String& deviceType, bool includePlugAndPlayInfo) const = 0;

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get poll responses json
    /// @return JSON string
    virtual String getPollResponsesJson() const = 0;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get decoded poll responses
    /// @param address address of device to get data from
    /// @param pStructOut pointer to structure (or array of structures) to receive decoded data
    /// @param structOutSize size of structure (in bytes) to receive decoded data
    /// @param maxRecCount maximum number of records to decode
    /// @param decodeState decode state for this device
    /// @return number of records decoded
    /// @note the pStructOut should generally point to structures of the correct type for the device data and the
    ///       decodeState should be maintained between calls for the same device
    virtual uint32_t getDecodedPollResponses(uint32_t address, 
                    void* pStructOut, uint32_t structOutSize, 
                    uint16_t maxRecCount, BusDeviceDecodeState& decodeState) const = 0;
};
