/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device Type Records
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftUtils.h"
#include "RaftJson.h"
#include "RaftBusConsts.h"
#include "DeviceTypeRecord.h"
#include "DevicePollingInfo.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @class DeviceTypeRecords
/// @brief Device type records
class DeviceTypeRecords
{
public:
    DeviceTypeRecords();

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device type for address
    /// @param addr device address
    /// @returns device type indexes that match the address
    std::vector<uint16_t> getDeviceTypeIdxsForAddr(BusElemAddrType addr) const;

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device type record for a device type index
    /// @param deviceTypeIdx device type index
    /// @return pointer to info record if device type found, nullptr if not
    const DeviceTypeRecord* getDeviceInfo(uint16_t deviceTypeIdx) const;

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device type record for a device type name
    /// @param deviceType device type name
    /// @return pointer to info record if device type found, nullptr if not
    const DeviceTypeRecord* getDeviceInfo(const String& deviceType) const;

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
    String getDevTypeInfoJsonByTypeIdx(uint16_t deviceTypeIdx, bool includePlugAndPlayInfo) const;

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device type info JSON by device type name
    /// @param deviceType device type name
    /// @param includePlugAndPlayInfo include plug and play info
    /// @return JSON string
    String getDevTypeInfoJsonByTypeName(const String& deviceType, bool includePlugAndPlayInfo) const;

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
    /// @param isOnline true if device is online
    /// @param pDevTypeRec pointer to device type record
    /// @param devicePollResponseData device poll response data
    String deviceStatusToJson(BusElemAddrType addr, bool isOnline, const DeviceTypeRecord* pDevTypeRec, 
            const std::vector<uint8_t>& devicePollResponseData) const;

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get scan priority lists
    /// @param priorityLists (out) priority lists
    static void getScanPriorityLists(std::vector<std::vector<BusElemAddrType>>& priorityLists);

private:
    // Helpers
    static bool extractBufferDataFromHexStr(const String& writeStr, std::vector<uint8_t>& writeData);
    static bool extractMaskAndDataFromHexStr(const String& readStr, std::vector<uint8_t>& readDataMask, 
                std::vector<uint8_t>& readDataCheck, bool maskToZeros);
    static bool extractCheckInfoFromHexStr(const String& readStr, std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>>& checkValues,
                bool maskToZeros);
    static uint32_t extractReadDataSize(const String& readStr);
    static uint32_t extractBarAccessMs(const String& readStr);
};

// Access to single instance
extern DeviceTypeRecords deviceTypeRecords;
