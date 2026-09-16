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
    std::vector<DeviceTypeIndexType> getDeviceTypeIdxsForAddr(BusElemAddrType addr) const;

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device type record for a device type index
    /// @param deviceTypeIdx device type index
    /// @param devTypeRec (out) device type record
    /// @return true if device type found
    bool getDeviceInfo(DeviceTypeIndexType deviceTypeIdx, DeviceTypeRecord& devTypeRec) const;

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device type record for a device type name
    /// @param deviceType device type name
    /// @param devTypeRec (out) device type record
    /// @param deviceTypeIdx (out) device type index
    /// @return true if device type found
    bool getDeviceInfo(const String& deviceTypeName, DeviceTypeRecord& devTypeRec, DeviceTypeIndexType& deviceTypeIdx) const;

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device polling info
    /// @param addr address
    /// @param pDevTypeRec device type record
    /// @param pollRequests (out) polling info
    void getPollInfo(BusElemAddrType addrAndSlot, const DeviceTypeRecord* pDevTypeRec, DevicePollingInfo& pollingInfo) const;

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device type info JSON by device type index
    /// @param deviceTypeIdx device type index
    /// @param includePlugAndPlayInfo include plug and play info
    /// @return JSON string
    String getDevTypeInfoJsonByTypeIdx(DeviceTypeIndexType deviceTypeIdx, bool includePlugAndPlayInfo) const;

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device type info JSON by device type name
    /// @param deviceType device type name
    /// @param includePlugAndPlayInfo include plug and play info
    /// @param deviceTypeIndex (out) device type index
    /// @return JSON string
    String getDevTypeInfoJsonByTypeName(const String& deviceType, bool includePlugAndPlayInfo, DeviceTypeIndexType& deviceTypeIndex) const;

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
    void getDetectionRecs(const DeviceTypeRecord* pDevTypeRec, std::vector<DeviceDetectionRec>& detectionRecs);

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

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Add extended device type records
    /// @param devTypeRec device type record
    /// @param deviceTypeIndex (out) device type index
    /// @return true if added
    bool addExtendedDeviceTypeRecord(const DeviceTypeRecordDynamic& devTypeRec, uint16_t& deviceTypeIndex);

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device poll decode function
    /// @param deviceTypeIdx device type index
    /// @return poll decode function
    DeviceTypeRecordDecodeFn getPollDecodeFn(uint16_t deviceTypeIdx) const;

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Suppress a device type by name, so it is never matched during identification
    /// @param deviceTypeName name as it appears in the compiled records, e.g. "VCNL4040"
    /// @return true if suppressed (false if the name is empty or the limit is reached)
    /// @note Suppression hides the type from lookup by name and from the address->type map. It does
    ///       NOT unbind a device already identified as that type - identification is where the
    ///       decision is made, so clear the device's identification to have this take effect on
    ///       something already found (RaftBusDevicesIF::reIdentifyDevices).
    ///
    ///       Suppressing a name that an extended record also defines suppresses both: the point is
    ///       "this type must not be detected", whoever supplied it.
    bool addSuppressedDeviceType(const String& deviceTypeName);

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Remove all suppressions
    void clearSuppressedDeviceTypes();

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Check if a device type name is suppressed
    /// @param deviceTypeName device type name
    /// @return true if suppressed
    bool isDeviceTypeSuppressed(const char* deviceTypeName) const;

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get the number of extended device type records this build will hold
    /// @return maximum record count
    /// @note Exposed so a caller supplying records can say up front how many will fit, rather than
    ///       discovering the limit by having records silently refused partway through a file.
    static constexpr uint32_t getMaxExtendedDeviceTypeRecords() { return MAX_EXTENDED_DEV_TYPE_RECORDS; }

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get the number of device types that may be suppressed
    /// @return maximum suppression count
    static constexpr uint32_t getMaxSuppressedDeviceTypes() { return MAX_SUPPRESSED_DEV_TYPES; }

private:
    // Mutex for access to extended device type records (mutable to allow locking in const methods)
    mutable RaftMutex _extDeviceTypeRecordsMutex;

    // Flag indicating if any extended records have been added - since this is set only once and
    // never cleared it is used to avoid taking the mutex in the common case where no extended records
    // have been added
    RaftAtomicBool _extendedRecordsAdded;

    // Maximum number of added device type records (see note below about absolute pointers since this space will be reserved)
    // This costs sizeof(DeviceTypeRecordDynamic) (~112 bytes) of internal RAM per slot, reserved at
    // construction whether used or not, so it is a real cost on every unit rather than a cap that is
    // free until reached. It bounds ADDITIONS AND OVERRIDES only - suppression is a separate list
    // below and does not consume these slots.
    static constexpr uint32_t MAX_EXTENDED_DEV_TYPE_RECORDS = 32;

    // Extended device type records
    // This list MUST only ever be extended and the absolute pointers to 
    // the DeviceTypeRecordDynamic instances must not change so storage MUST
    // be allocated in a way that does not move the instances
    std::vector<DeviceTypeRecordDynamic> _extendedDevTypeRecords;

    // Device type names that must never be matched during identification
    //
    // Deliberately NOT a flag on DeviceTypeRecordDynamic, which is the obvious place for it. Two
    // reasons, both about the array above rather than about style:
    //
    //  - A suppression carries one piece of information (a name) but would occupy a whole ~112-byte
    //    record, and those records are reserved up front. Allowing "suppress everything in the base
    //    library" would mean reserving a slot per base type on every unit, used or not.
    //  - The reserve exists because DeviceTypeRecord hands out c_str() pointers INTO those records,
    //    and String stores anything under 12 characters inline (SSO), so a reallocation dangles them.
    //    Nothing takes a pointer into this list - it is only ever compared by name - so it has no
    //    such constraint and can grow on demand.
    //
    // The result is that suppression costs nothing on a unit that does not use it, and the bound
    // below exists only to stop an untrusted file growing the list without limit.
    // Largest single read a device type record may declare (the NNN in "rNNN"). The value sizes a
    // heap allocation taken straight from a record, so it needs a ceiling now that records can come
    // from a file. Chosen well above any real device: the largest compiled profile reads 23 bytes.
    static constexpr uint32_t MAX_DEVICE_READ_BYTES = 1024;

    // A mutex of its own rather than sharing the one above: getDeviceTypeIdxsForAddr walks the
    // extended records and then filters the base records by suppression, so one non-recursive mutex
    // covering both would have to be taken twice in that path.
    static constexpr uint32_t MAX_SUPPRESSED_DEV_TYPES = 128;
    mutable RaftMutex _suppressedDevTypesMutex;
    std::vector<String> _suppressedDevTypeNames;

    // Flag indicating if any device types have been suppressed - same purpose as
    // _extendedRecordsAdded above: keep the mutex out of the common case
    RaftAtomicBool _anyDevTypesSuppressed;

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
