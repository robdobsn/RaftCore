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

private:
    // Mutex for access to extended device type records (mutable to allow locking in const methods)
    mutable RaftMutex _extDeviceTypeRecordsMutex;

    // Flag indicating if any extended records have been added - since this is set only once and
    // never cleared it is used to avoid taking the mutex in the common case where no extended records
    // have been added
    RaftAtomicBool _extendedRecordsAdded;

    // Maximum number of added device type records (see note below about absolute pointers since this space will be reserved)
    static constexpr uint32_t MAX_EXTENDED_DEV_TYPE_RECORDS = 20;

    // Extended device type records
    // This list MUST only ever be extended and the absolute pointers to 
    // the DeviceTypeRecordDynamic instances must not change so storage MUST
    // be allocated in a way that does not move the instances
    std::vector<DeviceTypeRecordDynamic> _extendedDevTypeRecords;

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
