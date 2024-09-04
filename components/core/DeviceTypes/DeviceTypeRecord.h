/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftCore Device Type Record
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include "RaftArduino.h"
#include "RaftBus.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get decoded data record
/// @param pPollBuf buffer containing data
/// @param pollBufLen length of buffer
/// @param pStructOut pointer to structure to fill
/// @param structOutSize size of structure
/// @param maxRecCount maximum number of records to decode
/// @param decodeState decode state (used for stateful decoding including timestamp wrap-around handling)
/// @return number of records decoded
typedef uint32_t (*DeviceTypeRecordDecodeFn)(const uint8_t* pPollBuf, uint32_t pollBufLen, void* pStructOut, uint32_t structOutSize, 
            uint16_t maxRecCount, RaftBusDeviceDecodeState& decodeState);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @class DeviceTypeRecord
/// @brief Device Type Record
/// @note This must be a POD type as it is used to describe the device type records in the flash memory
class DeviceTypeRecord
{
public:
    const char* deviceType = nullptr;
    const char* addresses = nullptr;
    const char* detectionValues = nullptr;
    const char* initValues = nullptr;
    const char* pollInfo = nullptr;
    uint16_t pollDataSizeBytes = 0;
    const char* devInfoJson = nullptr;
    DeviceTypeRecordDecodeFn pollResultDecodeFn = nullptr;

    String getJson(bool includePlugAndPlayInfo) const
    {
        // Check if plug and play info required
        if (!includePlugAndPlayInfo)
        {
            return devInfoJson ? String(devInfoJson) : "{}";
        }

        // Form JSON string
        String devTypeInfo = "{";
        if (deviceType)
            devTypeInfo += "\"type\":\"" + String(deviceType) + "\",";
        if (addresses)
            devTypeInfo += "\"addr\":\"" + String(addresses) + "\",";
        if (detectionValues)
            devTypeInfo += "\"det\":\"" + String(detectionValues) + "\",";
        if (initValues)
            devTypeInfo += "\"init\":\"" + String(initValues) + "\",";
        if (pollInfo)
            devTypeInfo += "\"poll\":\"" + String(pollInfo) + "\",";
        if (devInfoJson)
            devTypeInfo += "\"info\":" + String(devInfoJson) + ",";
        devTypeInfo += "\"pollSize\":" + String(pollDataSizeBytes);
        devTypeInfo += "}";
        return devTypeInfo;
    }
};
