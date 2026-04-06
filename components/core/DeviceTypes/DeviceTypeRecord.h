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
/// @enum AttrType
/// @brief Attribute field type for decoded poll structs
enum class AttrType : uint8_t { Float, Int32, Uint32, Int16, Uint16, Int8, Uint8, Bool };

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @struct AttrFieldDesc
/// @brief Describes one field in a generated poll struct — used for dynamic field access by name
/// @note Uses offsetof()-based offsets computed at compile time by the code generator
struct AttrFieldDesc
{
    const char* name;       // Attribute name, e.g. "ax"
    uint16_t offset;        // offsetof(poll_XXX, field) — compiler-guaranteed
    AttrType type;          // Field type enum
    const char* fmtStr;     // Format string from resp.a[].f, e.g. ".2f"
    float divisor;          // Divisor from resp.a[].d (1.0 = no division)
    float addend;           // Addend from resp.a[].a (applied after division)
};

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
    const AttrFieldDesc* pollFieldDescs = nullptr;
    uint16_t pollFieldCount = 0;
    uint16_t pollStructSize = 0;

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
