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
#include <vector>
#include "RaftArduino.h"

class DeviceTypeRecords;
class DeviceTypeRecord;

class DeviceTypeFileLoader
{
public:
    // Version of the record format this loader understands - the formatVersion of a source's raftdevtypes.json
    // manifest (AssembleDeviceTypes.py SOURCE_FORMAT_VERSION), reported by devman/typefile as recordFormat
    static constexpr int RECORD_FORMAT_VERSION = 1;

    // Default size limit for a type file: the whole file is read into one buffer (in PSRAM where there is any) and
    // parsed in place, so boards with PSRAM take larger files (AssembleDeviceTypes.py has the same defaults)
    static constexpr uint32_t MAX_FILE_BYTES_DEFAULT = 65536;
    static constexpr uint32_t MAX_FILE_BYTES_DEFAULT_PSRAM = 262144;

    // Limits for records from a file (compiled and code-registered records are not limited)
    struct Limits
    {
        uint32_t maxFileBytes = MAX_FILE_BYTES_DEFAULT;
        uint32_t maxReadBytesPerCommand = 1024;
        uint32_t maxSamples = 256;
        uint32_t maxRecordRingBytes = 65536;    // poll result size (with timestamp) x samples
    };

    // What happened to one entry of a file
    enum class Outcome : uint8_t
    {
        Added,          // added with a new index
        Replaced,       // replaced a compiled record (keeping its index)
        Unchanged,      // identical to a record already present
        Disabled,       // a policy entry for a type that is present (or set its policy)
        Rejected,       // not applied (error says why)
    };
    struct RecordOutcome
    {
        String key;
        Outcome outcome = Outcome::Rejected;
        String error;               // why it was rejected
        String warnings;            // problems that don't stop it being used ("; " between them)
        bool noDecoder = false;     // a record with data but no on-device decoder (DataLogger, RaftROS and named
                                    // values don't decode it; clients that decode, such as raftjs, do)
        String decoder;             // where its on-device decoder comes from: "compiled", "runtime" or "none" (records
                                    // with data that were added or replaced)
    };
    static const char* getOutcomeStr(Outcome outcome);

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
        bool keepRecordOutcomes = false;        // set to fill records
        std::vector<RecordOutcome> records;     // what happened to each entry, in file order

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

    /// @brief Check a device type file: what applying it would do at the next startup, without changing anything.
    ///        The file is applied to a scratch table holding what the device's table holds before its type file is
    ///        loaded - the compiled records, records registered by code, protected types, the data sources of ext
    ///        records and the same limits
    /// @param live the device's device type records
    /// @param liveFileKeys keys of the records the device's current type file added or replaced (left out of the
    ///        scratch table, as the file being checked replaces it)
    /// @param pJson file contents (null-terminated)
    /// @param limits limits for the file's records
    /// @param result (out) what applying the file would do (keepRecordOutcomes is set, so records is filled)
    static void checkJson(const DeviceTypeRecords& live, const std::vector<String>& liveFileKeys, const char* pJson,
                const Limits& limits, Result& result);

    /// @brief CRC16-CCITT of the file contents (the algorithm of the filehash endpoint, so the two can be compared)
    static uint16_t crc16(const uint8_t* pData, uint32_t len);

    /// @brief JSON text without whitespace outside strings
    static String minifyJson(const char* pStart, const char* pEnd);

    /// @brief Check an addresses string: comma-separated "0xHH" or "0xHH-0xHH" (as the generator requires), up to
    ///        maxAddr
    /// @return empty if valid, otherwise the problem
    static String checkAddresses(const char* pAddresses, uint32_t maxAddr);

    /// @brief A record as a type file entry - "<key>":{...} with the fields a type file uses (addresses, detection,
    ///        init and poll strings, devInfoJson, scanPriority, identPriority, hwTypeId, excludeFromAddrMap,
    ///        scanWhilePolled and disable for a policy that isn't enabled) - so it can be copied into a type file and
    ///        loads as the same record
    /// @param rec record
    /// @param policy the record's policy
    static String recordToFileEntry(const DeviceTypeRecord& rec, uint8_t policy);

    /// @brief Set a type's policy in type file text: "disable": true or "claim" is set in the type's entry (an entry
    ///        is added if there is none); for enabled, "disable" is removed (and an entry that only set a policy is
    ///        removed). Everything else in the file is kept as it is
    /// @param pFileJson the file (nullptr or empty: no file)
    /// @param key device type key
    /// @param policy DeviceTypeRecord::POLICY_...
    /// @param changed (out) whether the file changed
    /// @return the file text
    static String setPolicyInFile(const char* pFileJson, const char* key, uint8_t policy, bool& changed);

private:
    static bool compiledInitValueFor(const DeviceTypeRecords& table, const String& key, const char* pName);
    static void applyRecord(DeviceTypeRecords& table, const String& key, const char* pStart, const char* pEnd,
                const Limits& limits, uint8_t initialPolicy, Result& result);
    static void applyPolicy(DeviceTypeRecords& table, const String& key, uint8_t policy, Result& result);
    static void reject(Result& result, const String& key, const String& why);
    static void warn(Result& result, const String& key, const String& why);
    static RecordOutcome* outcomeFor(Result& result, const String& key);
    static constexpr const char* MODULE_PREFIX = "DevTypeFile";
};
