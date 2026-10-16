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
#include "RaftJson.h"

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
    uint16_t count;         // Elements in the field (>1 for array attributes; 0 treated as 1)
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @class DeviceTypeRecord
/// @brief Device Type Record
/// @note This must be a POD type as it is used to describe the device type records in the flash memory
class DeviceTypeRecord
{
public:
    const char* deviceType = nullptr;       // Device type key
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

    // Scan priority of the record's addresses: the first address goes in scan list 0 and the others in list 1
    // for high (0); all go in list 1 for medium and list 2 for low. NONE: addresses are scanned only in the last
    // list (JSON "scanPriority": "high"/"medium"/"low" or 1-3)
    static constexpr uint8_t SCAN_PRIORITY_HIGH = 0;
    static constexpr uint8_t SCAN_PRIORITY_NONE = 0xff;
    uint8_t scanPriority = SCAN_PRIORITY_NONE;

    // Flags
    static constexpr uint8_t FLAG_PROBE_ONLY = 0x01;        // not identified by address: adopted by an identification
                                                            // hook, and not reserved or prioritised for scanning
                                                            // (JSON "excludeFromAddrMap" or "probeOnly")
    static constexpr uint8_t FLAG_HAS_HW_TYPE_ID = 0x02;    // hwTypeId is set
    uint8_t flags = 0;

    // Candidates for an address are tried in descending identPriority, then in record order (JSON "identPriority")
    int8_t identPriority = 0;

    // Hardware type ID: the ID the device reports when probed by an identification hook, in that hook's own
    // numbering (e.g. the RSAO WHOAMI response). JSON "deviceTypeId". Valid if FLAG_HAS_HW_TYPE_ID is set
    uint16_t hwTypeId = 0;

    // Policy: whether the record is used for identification. Only identification is affected - typeinfo,
    // decoding and subscriptions are the same for every policy
    //   ENABLED         an identification candidate
    //   DISABLED        not a candidate: the next candidate, or the identification hook, gets the device
    //   DISABLED_CLAIM  not a candidate, but the address still counts as claimed: it stays reserved and the
    //                   identification hook isn't run there
    // initialPolicy is the policy the record starts with (JSON "disable": true or "claim"); it can be changed at
    // runtime (DeviceTypeRecords::setPolicy)
    static constexpr uint8_t POLICY_ENABLED = 0;
    static constexpr uint8_t POLICY_DISABLED = 1;
    static constexpr uint8_t POLICY_DISABLED_CLAIM = 2;
    uint8_t initialPolicy = POLICY_ENABLED;

    static const char* getPolicyStr(uint8_t policy)
    {
        return policy == POLICY_DISABLED ? "disabled" : (policy == POLICY_DISABLED_CLAIM ? "disabledClaim" : "enabled");
    }

    bool isProbeOnly() const
    {
        return (flags & FLAG_PROBE_ONLY) != 0;
    }
    bool hasHwTypeId() const
    {
        return (flags & FLAG_HAS_HW_TYPE_ID) != 0;
    }

    String getJson(bool includePlugAndPlayInfo) const
    {
        // Check if plug and play info required
        if (!includePlugAndPlayInfo)
        {
            return devInfoJson ? String(devInfoJson) : "{}";
        }

        // Form JSON string — pre-reserve to avoid repeated heap reallocation
        size_t reserveLen = 60; // fixed overhead: braces, key names, punctuation
        if (deviceType)      reserveLen += strlen(deviceType);
        if (addresses)       reserveLen += strlen(addresses);
        if (detectionValues) reserveLen += strlen(detectionValues);
        if (initValues)      reserveLen += strlen(initValues);
        if (pollInfo)        reserveLen += strlen(pollInfo);
        if (devInfoJson)     reserveLen += strlen(devInfoJson);
        String devTypeInfo = "{";
        devTypeInfo.reserve(reserveLen);
        RaftJson::appendStringField(devTypeInfo, "type", deviceType);
        RaftJson::appendStringField(devTypeInfo, "addr", addresses);
        RaftJson::appendStringField(devTypeInfo, "det", detectionValues);
        RaftJson::appendStringField(devTypeInfo, "init", initValues);
        RaftJson::appendRawField(devTypeInfo, "poll", pollInfo);
        RaftJson::appendRawField(devTypeInfo, "info", devInfoJson);
        RaftJson::appendRawField(devTypeInfo, "pollSize", String(pollDataSizeBytes).c_str());
        devTypeInfo += "}";
        return devTypeInfo;
    }
};
