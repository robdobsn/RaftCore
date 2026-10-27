/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftCore Device Type Record
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <string.h>
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

/// @brief Init value function - computes the values written in an init string's {name} value slots from the bytes the
///        earlier init steps read (generated from the record's initCode; devdocs/computed-init-values.md)
/// @param pReadBuf bytes read by the init steps before the first value slot, in order
/// @param readBufLen number of bytes
/// @param pValues (out) the values, in the order of the record's initValueNames
/// @param numValues number of values
/// @return false if the bytes read aren't enough (e.g. a read step failed)
typedef bool (*DeviceTypeRecordInitFn)(const uint8_t* pReadBuf, uint32_t readBufLen, int32_t* pValues, uint32_t numValues);

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
    // 0x02 was FLAG_HAS_HW_TYPE_ID (retired: a record has a hardware type ID if hwTypeId is a non-empty string)
    static constexpr uint8_t FLAG_NO_SCAN_WHILE_POLLED = 0x04;  // an online device being polled isn't probed by the
                                                            // scanner, however slowly it's polled: its presence comes
                                                            // from its polls (JSON "scanWhilePolled": false) - for a
                                                            // device that a scan probe disturbs, e.g. the SCD30
    uint8_t flags = 0;

    // Candidates for an address are tried in descending identPriority, then in record order (JSON "identPriority")
    int8_t identPriority = 0;

    // Hardware type ID: the ID the device reports when probed by an identification hook, as "<scheme>:<value>" -
    // the scheme names the hook's numbering and the value is written in that scheme's one canonical form, e.g.
    // "rsao:0x008F" (the RSAO WHOAMI type). Matched exactly; RaftCore checks only the syntax (isValidHwTypeId).
    // JSON "hwTypeId". nullptr (or empty) if the record has none
    const char* hwTypeId = nullptr;

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

    // Init values: the function computing the values for the init string's {name} value slots, and their names
    // (comma separated, in the order the function writes them) - only records with initCode, which must be compiled in
    DeviceTypeRecordInitFn initValuesFn = nullptr;
    const char* initValueNames = nullptr;

    // General call: bytes the bus sends to the I2C general call address (0x00) while scanning, for a device that
    // stays off the bus until it gets one (e.g. the Excelitas CaliPile loads its address from EEPROM on 0x04). As
    // hex, "0x04" (1 to MAX_GENERAL_CALL_BYTES bytes; see parseGeneralCall). JSON "generalCall". nullptr (or empty)
    // if the record has none
    const char* generalCall = nullptr;
    static constexpr uint32_t MAX_GENERAL_CALL_BYTES = 4;

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
        return hwTypeId && hwTypeId[0];
    }

    /// @brief Check the syntax of a hardware type ID: "<scheme>:<value>", the scheme [a-z][a-z0-9-]{0,15} and the
    ///        value one or more printable ASCII characters other than space (0x21-0x7e), 64 characters at most in all
    static bool isValidHwTypeId(const char* pId)
    {
        if (!pId || (*pId < 'a') || (*pId > 'z'))
            return false;
        uint32_t len = 1;
        while (pId[len] && (pId[len] != ':'))
        {
            char c = pId[len];
            if (!(((c >= 'a') && (c <= 'z')) || ((c >= '0') && (c <= '9')) || (c == '-')) || (len >= 16))
                return false;
            len++;
        }
        if (pId[len] != ':')
            return false;
        uint32_t valueStart = ++len;
        while (pId[len])
        {
            if ((pId[len] < 0x21) || (pId[len] > 0x7e) || (len >= MAX_HW_TYPE_ID_LEN))
                return false;
            len++;
        }
        return len > valueStart;
    }
    static constexpr uint32_t MAX_HW_TYPE_ID_LEN = 64;

    /// @brief Check the syntax of a hardware scheme name, as at the start of a hardware type ID: [a-z][a-z0-9-]{0,15}
    ///        (also the keys of a record's "hwProbeEffects", which RaftCore checks but doesn't keep)
    static bool isValidHwScheme(const char* pScheme)
    {
        if (!pScheme || (*pScheme < 'a') || (*pScheme > 'z'))
            return false;
        uint32_t len = 1;
        for (; pScheme[len]; len++)
        {
            char c = pScheme[len];
            if (!(((c >= 'a') && (c <= 'z')) || ((c >= '0') && (c <= '9')) || (c == '-')) || (len >= 16))
                return false;
        }
        return true;
    }
    /// @brief Parse a general call string: "0x" and 2 to 2 x MAX_GENERAL_CALL_BYTES hex digits. The first byte can't be
    ///        0x00 (not a general call command) or 0x06 (the general call reset: every device that supports it would
    ///        be reset each time the bus sends it)
    /// @param pStr string
    /// @param pBytes (out) bytes, MAX_GENERAL_CALL_BYTES long (may be nullptr to only check)
    /// @param numBytes (out) number of bytes
    /// @return true if valid
    static bool parseGeneralCall(const char* pStr, uint8_t* pBytes, uint32_t& numBytes)
    {
        numBytes = 0;
        if (!pStr || (pStr[0] != '0') || ((pStr[1] != 'x') && (pStr[1] != 'X')))
            return false;
        uint32_t numDigits = strlen(pStr + 2);
        if ((numDigits == 0) || (numDigits % 2 != 0) || (numDigits > 2 * MAX_GENERAL_CALL_BYTES))
            return false;
        uint8_t bytes[MAX_GENERAL_CALL_BYTES] = {};
        for (uint32_t i = 0; i < numDigits; i++)
        {
            char c = pStr[2 + i];
            int nibble = ((c >= '0') && (c <= '9')) ? c - '0' : ((c >= 'a') && (c <= 'f')) ? c - 'a' + 10 :
                        ((c >= 'A') && (c <= 'F')) ? c - 'A' + 10 : -1;
            if (nibble < 0)
                return false;
            bytes[i / 2] = (bytes[i / 2] << 4) | nibble;
        }
        if ((bytes[0] == 0x00) || (bytes[0] == 0x06))
            return false;
        numBytes = numDigits / 2;
        if (pBytes)
            memcpy(pBytes, bytes, numBytes);
        return true;
    }

    bool hasGeneralCall() const
    {
        return generalCall && generalCall[0];
    }

    bool isScannedWhilePolled() const
    {
        return (flags & FLAG_NO_SCAN_WHILE_POLLED) == 0;
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
