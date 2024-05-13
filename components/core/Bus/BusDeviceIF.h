/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device Interface
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftJson.h"


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
/// @brief Device Interface
/// @class BusDeviceIF
class BusDeviceIF
{
public:

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device type information by address
    /// @param address - address of device to get information for
    /// @param includePlugAndPlayInfo - true to include plug and play information
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
    /// @brief Decode one or more poll responses for a device
    /// @param deviceTypeIndex index of device type
    /// @param pPollBuf buffer containing poll responses
    /// @param pollBufLen length of poll response buffer
    /// @param pStructOut pointer to structure (or array of structures) to receive decoded data
    /// @param structOutSize size of structure (in bytes) to receive decoded data
    /// @param maxRecCount maximum number of records to decode
    /// @return number of records decoded
    virtual uint32_t decodePollResponses(uint16_t deviceTypeIndex, 
                    const uint8_t* pPollBuf, uint32_t pollBufLen, 
                    void* pStructOut, uint32_t structOutSize, 
                    uint16_t maxRecCount, BusDeviceDecodeState& decodeState) = 0;
};
