/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device Type Records
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "DeviceTypeRecord.h"
#include "BusAddrStatus.h"  // For DeviceOnlineState
#include "DeviceTypeRecordDynamic.h"
#include "DevicePollingInfo.h"
#include "RaftThreading.h"
#include "CatalogueIndex.h"
#include <atomic>
#include <memory>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @class DeviceTypeRecords
/// @brief Device type records
class DeviceTypeRecords
{
public:
    DeviceTypeRecords();
    ~DeviceTypeRecords();

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device type for address
    /// @param addr device address
    /// @returns device type indexes that match the address
    /// @note the records that identify a device at the address by address - kept for existing callers (e.g. RSAO
    ///       address reservation). Use getIdentCandidates() for identification and isAddrReserved() for
    ///       reservation
    std::vector<DeviceTypeIndexType> getDeviceTypeIdxsForAddr(BusElemAddrType addr) const;

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get the catalogue index (lookups derived from all records), rebuilt if records have been added
    /// @return the index - it doesn't change, so it can be used without a lock for as long as it is held
    std::shared_ptr<const CatalogueIndex> getCatalogueIndex() const;

    /// @brief Generation of the records: increases whenever a record is added (or later replaced or its policy
    ///        changed). A caller that keeps something derived from the records rebuilds it when this changes
    uint32_t getGeneration() const
    {
        return _generation.load(std::memory_order_acquire);
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Identification candidates for an address, in the order to try them
    /// @param addr address (without slot)
    /// @return device type indexes
    std::vector<DeviceTypeIndexType> getIdentCandidates(BusElemAddrType addr) const;

    /// @brief Records known at an address (records that identify a device there by address)
    std::vector<DeviceTypeIndexType> getKnownTypesAtAddr(BusElemAddrType addr) const;

    /// @brief Check whether a record identifies a device at this address (re-addressable devices keep off it)
    bool isAddrReserved(BusElemAddrType addr) const;

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get the record with a hardware type ID (the ID a device reports when probed by an identification
    ///        hook, JSON "deviceTypeId")
    /// @param hwTypeId hardware type ID
    /// @param deviceTypeIndex (out) device type index (DEVICE_TYPE_INDEX_INVALID if not found)
    /// @return the record, or nullptr if there is none
    const DeviceTypeRecord* getRecordByHwTypeId(uint16_t hwTypeId, DeviceTypeIndexType& deviceTypeIndex) const;

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get the device type record for a device type index (no copy, no lock)
    /// @param deviceTypeIndex device type index
    /// @return the record (in flash for a base record), or nullptr if there is none. Records are never changed or
    ///         freed while the firmware runs, so the pointer stays valid
    const DeviceTypeRecord* getRecord(DeviceTypeIndexType deviceTypeIndex) const;

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get the device type record for a device type key (an extended record with the key takes
    ///        precedence over a base record with the same key)
    /// @param deviceTypeKey device type key
    /// @param deviceTypeIndex (out) device type index (DEVICE_TYPE_INDEX_INVALID if not found)
    /// @return the record, or nullptr if there is none
    const DeviceTypeRecord* getRecord(const char* deviceTypeKey, DeviceTypeIndexType& deviceTypeIndex) const;

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device type record for a device type index
    /// @param deviceTypeIndex device type index
    /// @param devTypeRec (out) device type record (a copy - its strings point into the record, which stays valid)
    /// @return true if device type found
    bool getDeviceInfo(DeviceTypeIndexType deviceTypeIndex, DeviceTypeRecord& devTypeRec) const;

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device type record for a device type key
    /// @param deviceTypeKey device type key
    /// @param devTypeRec (out) device type record
    /// @param deviceTypeIndex (out) device type index
    /// @return true if device type found
    bool getDeviceInfo(const String& deviceTypeKey, DeviceTypeRecord& devTypeRec, DeviceTypeIndexType& deviceTypeIndex) const;

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device polling info
    /// @param addr address
    /// @param pDevTypeRec device type record
    /// @param pollRequests (out) polling info
    void getPollInfo(BusElemAddrType addrAndSlot, const DeviceTypeRecord* pDevTypeRec, DevicePollingInfo& pollingInfo) const;

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device type info JSON by device type index
    /// @param deviceTypeIndex device type index
    /// @param includePlugAndPlayInfo include plug and play info
    /// @return JSON string
    String getDevTypeInfoJsonByTypeIdx(DeviceTypeIndexType deviceTypeIndex, bool includePlugAndPlayInfo) const;

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device type info JSON by device type key
    /// @param deviceTypeKey device type key
    /// @param includePlugAndPlayInfo include plug and play info
    /// @param deviceTypeIndex (out) device type index
    /// @return JSON string
    String getDevTypeInfoJsonByTypeName(const String& deviceTypeKey, bool includePlugAndPlayInfo, DeviceTypeIndexType& deviceTypeIndex) const;

    // Device detection record
    class DeviceDetectionRec
    {
    public:
        std::vector<uint8_t> writeData;
        // First value is a mask and second value is the expected value to check against
        // Result is true if any of the pairs match the read values
        std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>> checkValues;
        uint16_t pauseAfterSendMs;
    };

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get detection records
    /// @param pDevTypeRec device type record
    /// @param detectionRecs (out) detection records
    /// @return false if any detection value is invalid (detectionRecs is then empty and the record must not match)
    bool getDetectionRecs(const DeviceTypeRecord* pDevTypeRec, std::vector<DeviceDetectionRec>& detectionRecs);

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get initialisation bus requests
    /// @param addr address
    /// @param pDevTypeRec device type record
    /// @param initBusRequests (out) initialisation bus requests
    void getInitBusRequests(BusElemAddrType addr, const DeviceTypeRecord* pDevTypeRec, std::vector<BusRequestInfo>& initRequests);

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Convert poll response to JSON
    /// @param addr address
    /// @param onlineState device online state
    /// @param deviceTypeIndex device type index
    /// @param devicePollResponseData device poll response data
    static String deviceStatusToJson(BusElemAddrType addr, DeviceOnlineState onlineState, uint16_t deviceTypeIndex,
            const std::vector<uint8_t>& devicePollResponseData);

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get scan priority lists
    /// @param priorityLists (out) priority lists
    void getScanPriorityLists(std::vector<std::vector<BusElemAddrType>>& priorityLists);

    // Result of adding an extended record
    enum class AddResult
    {
        Added,              // added with a new index
        AlreadyPresent,     // a record with the same key and the same content is present - its index is returned
        Conflict,           // a record with the same key and different content is present - not added (D17)
        Full,               // the configured number of records or bytes would be exceeded, or out of memory
        Invalid,            // the record has no key
    };
    static const char* getAddResultStr(AddResult result);

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Add an extended device type record
    /// @param devTypeRec device type record
    /// @param deviceTypeIndex (out) device type index (DEVICE_TYPE_INDEX_INVALID unless Added or AlreadyPresent)
    /// @return result
    AddResult addRecord(const DeviceTypeRecordDynamic& devTypeRec, DeviceTypeIndexType& deviceTypeIndex);

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Add extended device type records
    /// @param devTypeRec device type record
    /// @param deviceTypeIndex (out) device type index (DEVICE_TYPE_INDEX_INVALID if not added or present)
    /// @return true if added (false if already present - deviceTypeIndex is then its index - or not added)
    /// @note see addRecord() for the reason a record is not added
    bool addExtendedDeviceTypeRecord(const DeviceTypeRecordDynamic& devTypeRec, uint16_t& deviceTypeIndex);

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Set the limits for extended records (only before the first extended record is added)
    /// @param maxRecords maximum number of extended records (0 for the default)
    /// @param maxBytes maximum bytes used by extended records (0 for the default)
    /// @return true if set (false if records have already been added)
    /// @note defaults: 256 records and 256 KB on boards with PSRAM, 32 records and 20 KB without
    bool setExtendedRecordLimits(uint32_t maxRecords, uint32_t maxBytes);

    // Extended record usage
    struct ExtendedRecordStats
    {
        uint32_t count = 0;
        uint32_t maxRecords = 0;
        uint32_t bytesUsed = 0;
        uint32_t maxBytes = 0;
    };
    ExtendedRecordStats getExtendedRecordStats() const;

    /// @brief Number of base records
    static uint32_t getNumBaseRecords();

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device poll decode function
    /// @param deviceTypeIndex device type index
    /// @return poll decode function
    DeviceTypeRecordDecodeFn getPollDecodeFn(uint16_t deviceTypeIndex) const;

private:
    // Extended records are added at runtime. Each is one packed allocation (DeviceTypeRecordPacked) that never
    // changes or moves, so pointers to it stay valid. _extRecords is a fixed array of pointers (allocated when
    // the first record is added, sized by the limits); a record is written to it before _extCount is
    // incremented (release), so reading a record by index needs no lock. Adding and key lookups take the mutex
    mutable RaftMutex _extRecordsMutex;
    const DeviceTypeRecord** _extRecords = nullptr;
    std::atomic<uint32_t> _extCount{0};
    uint32_t _extMaxRecords = 0;
    uint32_t _extMaxBytes = 0;
    uint32_t _extBytesUsed = 0;

    // Positions of the extended records sorted by key (for binary search), maintained under the mutex
    std::vector<uint16_t> _extPosByKey;

    // Generation (incremented when a record is added) and the catalogue index built for it (replaced, never
    // changed, when rebuilt; held under the mutex)
    std::atomic<uint32_t> _generation{1};
    mutable std::shared_ptr<const CatalogueIndex> _catalogueIndex;

    // Default limits (D3)
    static constexpr uint32_t EXT_MAX_RECORDS_DEFAULT = 32;
    static constexpr uint32_t EXT_MAX_BYTES_DEFAULT = 20 * 1024;     // holds 32 typical records (538 B each, measured)
    static constexpr uint32_t EXT_MAX_RECORDS_DEFAULT_PSRAM = 256;
    static constexpr uint32_t EXT_MAX_BYTES_DEFAULT_PSRAM = 256 * 1024;

    // Helpers for extended records (assume the mutex is held)
    void applyDefaultExtendedRecordLimits();
    int32_t findExtendedPosByKey(const char* deviceTypeKey) const;

    // Helpers
    static bool extractBufferDataFromHexStr(const String& writeStr, std::vector<uint8_t>& writeData);
    static bool extractMaskAndDataFromHexStr(const String& readStr, std::vector<uint8_t>& readDataMask, 
                std::vector<uint8_t>& readDataCheck, bool maskToZeros, uint32_t& pauseAfterSendMs);
    static bool extractCheckInfoFromHexStr(const String& readStr, std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>>& checkValues,
                bool maskToZeros);
    static uint32_t extractReadDataSize(const String& readStr);
    static uint32_t extractBarAccessMs(const String& readStr);

    // Debug
    static constexpr const char* MODULE_PREFIX = "DevTypeRecs";
};

// Access to single instance
extern DeviceTypeRecords deviceTypeRecords;
