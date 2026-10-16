/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// DeviceTypeFileLoader.h
//
// Applies a device type file to the device type records: adding types the firmware doesn't have, overriding
// ones it has (an override replaces the compiled record, at its index - D2) and disabling ones that must not be
// identified.
//
// The file has the shape of DeviceTypeRecords.json - {"devTypes":{"<key>":{record}}} - so a compiled record can
// be copied into it unchanged. An entry with "disable": true (or "claim") sets the policy of a record that is
// already present, and the rest of the entry is ignored (so a type can be disabled by copying its record and
// adding one field); with a full record and a key that isn't present, the record is added already disabled.
// Other top-level keys, and "_notes" in a record, are ignored. An empty "devTypes" means no custom types.
//
// Records in a file are not trusted the way compiled ones are, so each is checked here before it is added: the
// key and "deviceType" agree, the addresses, identification fields and command strings are valid (the same
// grammar the firmware uses), reads and the poll ring are within limits, and devInfoJson.resp.b matches what the
// poll reads produce. A bad record is rejected on its own, and the first error is kept (D22).
//
// This is the device library format of the feature/device-library branch, with the behaviour changes listed in
// the device identification plan (§10.4).
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include "RaftArduino.h"

class DeviceTypeRecords;

class DeviceTypeFileLoader
{
public:
    // Limits for records from a file (compiled and code-registered records are not limited)
    struct Limits
    {
        uint32_t maxFileBytes = 65536;
        uint32_t maxReadBytesPerCommand = 1024;
        uint32_t maxSamples = 256;
        uint32_t maxRecordRingBytes = 65536;    // poll result size (with timestamp) x samples
    };

    // What was done with a file
    struct Result
    {
        uint32_t added = 0;         // records added with a new index
        uint32_t replaced = 0;      // records that replaced a compiled record
        uint32_t unchanged = 0;     // records identical to one already present
        uint32_t disabled = 0;      // types disabled (including records added already disabled)
        uint32_t rejected = 0;      // entries not applied
        uint32_t warnings = 0;      // problems that don't stop a record being used
        String firstError;          // "<key>: <why>" for the first entry rejected (or why the file was)
        String firstWarning;
        bool fileValid = false;     // the file had a "devTypes" object

        bool anyApplied() const
        {
            return (added + replaced + unchanged + disabled) > 0;
        }
    };

    /// @brief Apply a device type file
    /// @param table device type records
    /// @param pJson file contents (null-terminated)
    /// @param limits limits for the file's records
    /// @param result (out) what was done
    static void applyJson(DeviceTypeRecords& table, const char* pJson, const Limits& limits, Result& result);

    /// @brief CRC16-CCITT of the file contents (the algorithm of the filehash endpoint, so the two can be compared)
    static uint16_t crc16(const uint8_t* pData, uint32_t len);

    /// @brief JSON text without whitespace outside strings
    static String minifyJson(const char* pStart, const char* pEnd);

    /// @brief Check an addresses string: comma-separated "0xHH" or "0xHH-0xHH" (as the generator requires), up to
    ///        maxAddr
    /// @return empty if valid, otherwise the problem
    static String checkAddresses(const char* pAddresses, uint32_t maxAddr);

private:
    static void applyRecord(DeviceTypeRecords& table, const String& key, const char* pStart, const char* pEnd,
                const Limits& limits, uint8_t initialPolicy, Result& result);
    static void applyPolicy(DeviceTypeRecords& table, const String& key, uint8_t policy, Result& result);
    static void reject(Result& result, const String& key, const String& why);
    static void warn(Result& result, const String& key, const String& why);
    static constexpr const char* MODULE_PREFIX = "DevTypeFile";
};
