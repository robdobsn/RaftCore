/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device Interface
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftJson.h"

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

};
