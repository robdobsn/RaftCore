/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device type records
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "DeviceTypeRecords.h"
#include "BusRequestInfo.h"
#include "RaftJson.h"
#include "RaftUtils.h"

// #define DEBUG_DEVICE_INFO_RECORDS
// #define DEBUG_POLL_REQUEST_REQS
// #define DEBUG_DEVICE_INFO_PERFORMANCE
// #define DEBUG_DEVICE_INIT_REQS
// #define DEBUG_LOOKUP_DEVICE_TYPE_BY_INDEX
// #define DEBUG_LOOKUP_DEVICE_TYPE_BY_NAME
// #define DEBUG_ADD_EXTENDED_DEVICE_TYPE_RECORD

// Global object
DeviceTypeRecords deviceTypeRecords;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Generated device type records
#include "DeviceTypeRecords_generated.h"
static const uint32_t BASE_DEV_TYPE_ARRAY_SIZE = sizeof(baseDevTypeRecords) / sizeof(DeviceTypeRecord);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Constructor
DeviceTypeRecords::DeviceTypeRecords()
{
    // Create mutex
    RaftMutex_init(_extDeviceTypeRecordsMutex);

    // Reserve space for extended device type records (to avoid changing absolute pointer values)
    _extendedDevTypeRecords.reserve(MAX_EXTENDED_DEV_TYPE_RECORDS);
}

DeviceTypeRecords::~DeviceTypeRecords()
{
    // Destroy mutex
    RaftMutex_destroy(_extDeviceTypeRecordsMutex);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief get device types (strings) for an address
/// @param addr address
/// @returns device type indexes (into generated baseDevTypeRecords array) that match the address
std::vector<uint16_t> DeviceTypeRecords::getDeviceTypeIdxsForAddr(BusElemAddrType addr) const
{
#ifdef DEBUG_DEVICE_INFO_PERFORMANCE
    uint64_t startTimeUs = micros();
#endif

    // Return value
    std::vector<uint16_t> devTypeIdxsForAddr;

    // Check if any of the extended device type records match
    if (_extendedRecordsAdded && RaftMutex_lock(_extDeviceTypeRecordsMutex, UINT32_MAX))
    {
        // Ext devType indices continue on from the base devType indices
        uint16_t devTypeIdx = BASE_DEV_TYPE_ARRAY_SIZE;
        for (const auto& extDevTypeRec : _extendedDevTypeRecords)
        {
            std::vector<int> addressList;
            Raft::parseIntList(extDevTypeRec.addresses.c_str(), addressList, ",");
            for (int devAddr : addressList)
            {
                if (devAddr == addr)
                {
                    devTypeIdxsForAddr.push_back(devTypeIdx);
                    break;
                }
            }
            devTypeIdx++;
        }
        RaftMutex_unlock(_extDeviceTypeRecordsMutex);
    }

    // Check valid
    if ((addr < BASE_DEV_INDEX_BY_ARRAY_MIN_ADDR) || (addr > BASE_DEV_INDEX_BY_ARRAY_MAX_ADDR))
        return devTypeIdxsForAddr;
    uint32_t addrIdx = addr - BASE_DEV_INDEX_BY_ARRAY_MIN_ADDR;
    
    // Get number of types for this addr - if none then return
    uint32_t numTypes = baseDevTypeCountByAddr[addrIdx];
    if (numTypes == 0)
        return devTypeIdxsForAddr;

    // Iterate the types for this address
    for (uint32_t i = 0; i < numTypes; i++)
    {
        devTypeIdxsForAddr.push_back(baseDevTypeIndexByAddr[addrIdx][i]);
    }
    
#ifdef DEBUG_DEVICE_INFO_PERFORMANCE
    uint64_t endTimeUs = micros();
    LOG_I(MODULE_PREFIX, "getDeviceTypeIdxsForAddr %04x %d typeIdxs %lld us", addr, 
                    devTypeIdxsForAddr.size(), endTimeUs - startTimeUs);
#endif

#ifdef DEBUG_DEVICE_INFO_RECORDS
    LOG_I(MODULE_PREFIX, "getDeviceTypeIdxsForAddr %04x %d types", addr, devTypeIdxsForAddr.size());
#endif
    return devTypeIdxsForAddr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get device type for a device type index
/// @param deviceTypeIdx device type index
/// @param devTypeRec (out) device type record
/// @return true if device type found
bool DeviceTypeRecords::getDeviceInfo(uint16_t deviceTypeIdx, DeviceTypeRecord& devTypeRec) const
{
    // Check if in range
    bool isValid = false;
    if (deviceTypeIdx >= BASE_DEV_TYPE_ARRAY_SIZE)
    {
        // Check extended device type records
        if (_extendedRecordsAdded && RaftMutex_lock(_extDeviceTypeRecordsMutex, UINT32_MAX))
        {
            const uint32_t extDevTypeIdx = deviceTypeIdx - BASE_DEV_TYPE_ARRAY_SIZE;
            if (extDevTypeIdx < _extendedDevTypeRecords.size())
                isValid = _extendedDevTypeRecords[extDevTypeIdx].getDeviceTypeRecord(devTypeRec);
            RaftMutex_unlock(_extDeviceTypeRecordsMutex);
        }
    }
    else
    {
        devTypeRec = baseDevTypeRecords[deviceTypeIdx];
        isValid = true;
    }
#ifdef DEBUG_LOOKUP_DEVICE_TYPE_BY_INDEX
    LOG_I(MODULE_PREFIX, "getDeviceInfo %d %s %s", deviceTypeIdx, 
                isValid ? "OK" : "NOT FOUND", 
                isValid ? (devTypeRec.deviceType ? devTypeRec.deviceType : "NO NAME") : "INVALID");
#endif
    return isValid;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get device type for a device type name
/// @param deviceTypeName device type name
/// @param devTypeRec (out) device type record
/// @param deviceTypeIdx (out) device type index
/// @return true if device type found
bool DeviceTypeRecords::getDeviceInfo(const String& deviceTypeName, DeviceTypeRecord& devTypeRec, uint32_t& deviceTypeIdx) const
{
    // Iterate the extended device types first - so that device type names can be overridden
    bool isValid = false;
    uint32_t typeIdx = BASE_DEV_TYPE_ARRAY_SIZE;
    if (_extendedRecordsAdded && RaftMutex_lock(_extDeviceTypeRecordsMutex, UINT32_MAX))
    {
        for (const auto& extDevTypeRec : _extendedDevTypeRecords)
        {
            if (extDevTypeRec.deviceTypeName == deviceTypeName)
            {
                isValid = extDevTypeRec.getDeviceTypeRecord(devTypeRec);
                deviceTypeIdx = typeIdx;
                break;
            }
            typeIdx++;
        }
        RaftMutex_unlock(_extDeviceTypeRecordsMutex);
    }

    // Iterate the device types
    if (!isValid)
    {
        for (uint16_t i = 0; i < BASE_DEV_TYPE_ARRAY_SIZE; i++)
        {
            if (deviceTypeName == baseDevTypeRecords[i].deviceType)
            {
                devTypeRec = baseDevTypeRecords[i];
                deviceTypeIdx = i;
                isValid = true;
                break;
            }
        }
    }
#ifdef DEBUG_LOOKUP_DEVICE_TYPE_BY_NAME
    LOG_I(MODULE_PREFIX, "getDeviceInfo %s %s idx %d devTypeInfo %s", deviceTypeName.c_str(), 
                isValid ? "OK" : "NOT FOUND", 
                deviceTypeIdx,
                isValid ? (devTypeRec.deviceType ? devTypeRec.deviceType : "NO NAME") : "INVALID");
#endif
    return isValid;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get device polling info
/// @param addr address
/// @param pDevTypeRec device type record
/// @param pollRequests (out) polling info
void DeviceTypeRecords::getPollInfo(BusElemAddrType addr, const DeviceTypeRecord* pDevTypeRec, DevicePollingInfo& pollingInfo) const
{
    // Clear initially
    pollingInfo.clear();
    if (!pDevTypeRec)
        return;

    // Form JSON from string
    RaftJson pollInfo(pDevTypeRec->pollInfo);

    // Get polling request records
    String pollRequest = pollInfo.getString("c", "");
    if (pollRequest.length() == 0)
        return;

    // Extract polling info
    std::vector<RaftJson::NameValuePair> pollWriteReadPairs;
    RaftJson::extractNameValues(pollRequest, "=", "&", ";", pollWriteReadPairs);

    // Create a polling request for each pair
    uint16_t pollResultDataSize = 0;
    for (const auto& pollWriteReadPair : pollWriteReadPairs)
    {
        std::vector<uint8_t> writeData;
        if (!extractBufferDataFromHexStr(pollWriteReadPair.name, writeData))
        {
#ifdef DEBUG_POLL_REQUEST_REQS
            LOG_I(MODULE_PREFIX, "getPollInfo FAIL extractBufferDataFromHexStr %s (value %s)", 
                        pollWriteReadPair.name.c_str(), pollWriteReadPair.value.c_str());
#endif
            continue;
        }
        std::vector<uint8_t> readDataMask;
        std::vector<uint8_t> readData;
        uint32_t pauseAfterSendMs = 0; 
        if (!extractMaskAndDataFromHexStr(pollWriteReadPair.value, readDataMask, readData, false, pauseAfterSendMs))
        {
#ifdef DEBUG_POLL_REQUEST_REQS
            LOG_I(MODULE_PREFIX, "getPollInfo FAIL extractMaskAndDataFromHexStr %s (name %s)", 
                        pollWriteReadPair.value.c_str(), pollWriteReadPair.name.c_str());
#endif
            continue;
        }

#ifdef DEBUG_POLL_REQUEST_REQS
        String writeDataStr;
        Raft::getHexStrFromBytes(writeData.data(), writeData.size(), writeDataStr);
        String readDataMaskStr;
        Raft::getHexStrFromBytes(readDataMask.data(), readDataMask.size(), readDataMaskStr);
        String readDataStr;
        Raft::getHexStrFromBytes(readData.data(), readData.size(), readDataStr);
        LOG_I(MODULE_PREFIX, "getPollInfo addr %04x pauseAfterSendMs %d writeData %s readDataMask %s readData %s", 
                    addr, pauseAfterSendMs, writeDataStr.c_str(), readDataMaskStr.c_str(), readDataStr.c_str());
#endif

        // Create the poll request
        BusRequestInfo pollReq(BUS_REQ_TYPE_POLL, 
                addr,
                DevicePollingInfo::DEV_IDENT_POLL_CMD_ID, 
                writeData.size(),
                writeData.data(), 
                readDataMask.size(),
                pauseAfterSendMs,
                NULL, 
                NULL);
        pollingInfo.pollReqs.push_back(pollReq);

        // Keep track of poll result size
        pollResultDataSize += readData.size();
    }

    // Get number of polling results to store
    pollingInfo.numPollResultsToStore = pollInfo.getLong("s", 0);

    // Get polling interval
    pollingInfo.pollIntervalUs = pollInfo.getLong("i", 0) * 1000;

    // Set the poll result size
    pollingInfo.pollResultSizeIncTimestamp = pollResultDataSize + DevicePollingInfo::POLL_RESULT_TIMESTAMP_SIZE;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get initialisation bus requests
/// @param deviceType device type
/// @param initRequests (out) initialisation requests
void DeviceTypeRecords::getInitBusRequests(BusElemAddrType addr, const DeviceTypeRecord* pDevTypeRec, std::vector<BusRequestInfo>& initRequests)
{
    // Clear initially
    initRequests.clear();
    if (!pDevTypeRec)
        return;
        
    // Get the initialisation values
    String initValues = pDevTypeRec->initValues;

    // Extract name:value pairs
    std::vector<RaftJson::NameValuePair> initWriteReadPairs;
    RaftJson::extractNameValues(initValues, "=", "&", ";", initWriteReadPairs);

    // Form the bus requests
    for (const auto& initNameValue : initWriteReadPairs)
    {
        std::vector<uint8_t> writeData;
        if (!extractBufferDataFromHexStr(initNameValue.name, writeData))
            continue;
        uint32_t numReadDataBytes = extractReadDataSize(initNameValue.value);
        uint32_t barAccessForMs = extractBarAccessMs(initNameValue.value);

        // Create a bus request to write the initialisation value
        BusRequestInfo reqRec(BUS_REQ_TYPE_FAST_SCAN,
                    addr,
                    0, 
                    writeData.size(), 
                    writeData.data(),
                    numReadDataBytes,
                    barAccessForMs, 
                    nullptr, 
                    this);
        initRequests.push_back(reqRec);

        // Debug
#ifdef DEBUG_DEVICE_INIT_REQS
        String writeDataStr;
        Raft::getHexStrFromBytes(writeData.data(), writeData.size(), writeDataStr);
        LOG_I(MODULE_PREFIX, "getInitBusRequests addr %04x devType %s writeData %s readDataSize %d", 
                    addr, pDevTypeRec->deviceType, 
                    writeDataStr.c_str(), numReadDataBytes);
#endif
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Extract buffer data from hex string
/// @param writeStr hex string
/// @param writeData (out) buffer data
bool DeviceTypeRecords::extractBufferDataFromHexStr(const String& writeStr, std::vector<uint8_t>& writeData)
{        
    const char* pStr = writeStr.c_str();
    uint32_t inStrLen = writeStr.length();
    if (writeStr.startsWith("0x") || writeStr.startsWith("0X"))
    {
        pStr = writeStr.c_str() + 2;
        inStrLen -= 2;
    }
    // Compute length
    uint32_t writeStrLen = (inStrLen + 1) / 2;
    // Extract the write data
    writeData.resize(writeStrLen);
    Raft::getBytesFromHexStr(pStr, writeData.data(), writeData.size());
    return writeData.size() > 0 || (writeStr.length() == 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Extract mask and data from hex string
/// @param readStr hex string
/// @param readDataMask (out) mask data
/// @param readDataCheck (out) check data
/// @param maskToZeros true if mask should be set to zeros
/// @param pauseAfterSendMs (out) pause after send time in ms
/// @return true if successful
bool DeviceTypeRecords::extractMaskAndDataFromHexStr(const String& readStr, std::vector<uint8_t>& readDataMask, 
            std::vector<uint8_t>& readDataCheck, bool maskToZeros, uint32_t& pauseAfterSendMs)
{
    readDataCheck.clear();
    readDataMask.clear();
    String readStrLC = readStr;
    readStrLC.toLowerCase();

    // Check bar access time
    pauseAfterSendMs = extractBarAccessMs(readStr);

    // If readStr contains rNNNN then it is a read request
    int readIdx = readStrLC.indexOf("r");
    if (readIdx >= 0)
    {
        // Compute length
        uint32_t lenBytes = strtol(readStrLC.c_str() + readIdx + 1, NULL, 10);
        // Extract the read data
        readDataMask.resize(lenBytes);
        readDataCheck.resize(lenBytes);
        for (int i = readIdx + 1; i < readStrLC.length(); i++)
        {
            readDataMask[i - 1] = maskToZeros ? 0xff : 0;
            readDataCheck[i - 1] = 0;
        }
        return true;
    }

    // Check if readStr starts with 0x
    else if (readStrLC.indexOf("0x") >= 0)
    {
        int hexIdx = readStrLC.indexOf("0x");
        
        // Check for wildcards in hex string (X character)
        bool hasWildcards = false;
        for (int i = hexIdx + 2; i < readStrLC.length(); i++) {
            if (readStrLC[i] == 'x') {
                hasWildcards = true;
                break;
            }
        }
        
        if (hasWildcards) {
            // Handle hex string with wildcards
            // Compute length
            uint32_t lenNibbles = readStrLC.length() - hexIdx - 2;
            uint32_t lenBytes = (lenNibbles + 1) / 2;
            
            // Extract the read data
            readDataMask.resize(lenBytes);
            readDataCheck.resize(lenBytes);
            
            // Initialize arrays
            for (int i = 0; i < lenBytes; i++) {
                readDataMask[i] = maskToZeros ? 0xff : 0;
                readDataCheck[i] = 0;
            }
            
            // Process each nibble
            for (int i = 0; i < lenNibbles; i++) {
                char c = readStrLC[hexIdx + 2 + i];
                uint8_t nibbleVal = 0;
                bool isWildcard = false;
                
                if (c == 'x') {
                    isWildcard = true;
                } else if (c >= '0' && c <= '9') {
                    nibbleVal = c - '0';
                } else if (c >= 'a' && c <= 'f') {
                    nibbleVal = c - 'a' + 10;
                } else {
                    // Invalid character
                    return false;
                }
                
                // Calculate byte index and shift
                int byteIdx = i / 2;
                int shift = (i % 2 == 0) ? 4 : 0;  // High nibble or low nibble
                
                if (isWildcard) {
                    // Update mask for wildcard
                    if (maskToZeros) {
                        readDataMask[byteIdx] &= ~(0xF << shift);
                    } else {
                        readDataMask[byteIdx] |= (0xF << shift);
                    }
                } else {
                    // Set expected value
                    readDataCheck[byteIdx] |= (nibbleVal << shift);
                }
            }
        } else {
            // Standard hex string without wildcards
            // Compute length
            uint32_t lenBytes = (readStrLC.length() - hexIdx - 2 + 1) / 2;
            // Extract the read data
            readDataMask.resize(lenBytes);
            readDataCheck.resize(lenBytes);
            Raft::getBytesFromHexStr(readStrLC.c_str() + hexIdx + 2, readDataCheck.data(), readDataCheck.size());
            for (int i = 0; i < readDataMask.size(); i++) {
                readDataMask[i] = maskToZeros ? 0xff : 0;
            }
        }
        return true;
    }

    // Check if readStr starts with 0b 
    else if (readStrLC.indexOf("0b") >= 0)
    {
        int binIdx = readStrLC.indexOf("0b");
        // Compute length
        uint32_t lenBits = readStrLC.length() - binIdx - 2;
        uint32_t lenBytes = (lenBits + 7) / 8;
        // Extract the read data
        readDataMask.resize(lenBytes);
        readDataCheck.resize(lenBytes);
        uint32_t bitMask = 0x80;
        uint32_t byteIdx = 0;
        for (int i = binIdx + 2; i < readStrLC.length(); i++)
        {
            if (bitMask == 0x80)
            {
                readDataMask[byteIdx] = maskToZeros ? 0xff : 0;
                readDataCheck[byteIdx] = 0;
            }
            if (readStrLC[i] == 'x')
            {
                if (maskToZeros)
                    readDataMask[byteIdx] &= ~bitMask;
                else
                    readDataMask[byteIdx] |= bitMask;
            }
            else if (readStrLC[i] == '1')
            {
                readDataCheck[byteIdx] |= bitMask;
            }
            bitMask >>= 1;
            if (bitMask == 0)
            {
                bitMask = 0x80;
                byteIdx++;
            }
        }
        return true;
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Extract check info from hex string
/// @param readStr hex string
/// @param checkValues (out) check values
/// @param maskToZeros true if mask should be set to zeros
bool DeviceTypeRecords::extractCheckInfoFromHexStr(const String& readStr, std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>>& checkValues,
            bool maskToZeros)
{
    checkValues.clear();
    
    // Check if this is a CRC validation format
    if (readStr.indexOf("{crc:") >= 0) {
        // Handle as multi-field check with CRC validation
        std::vector<DeviceDetectionRec::FieldCheck> fieldChecks;
        if (!extractFieldChecksFromStr(readStr, fieldChecks, maskToZeros)) {
            return false;
        }
        
        // Convert field checks to traditional format for backward compatibility
        // Note: This only works for simple checks without CRC validation
        for (const auto& fieldCheck : fieldChecks) {
            if (!fieldCheck.hasCRC) {
                checkValues.push_back(std::make_pair(fieldCheck.mask, fieldCheck.expectedValue));
            }
        }
        
        // If all fields have CRC, we won't have any check values, but that's OK
        // The CRC validation will be handled separately in DeviceIdentMgr
        return true;
    }
    
    // Traditional format without CRC validation
    // Iterate over comma separated sections of string
    String readStrLC = readStr;
    readStrLC.toLowerCase();
    while (readStrLC.length() > 0)
    {
        // Find next comma
        int sectionIdx = readStrLC.indexOf(",");
        if (sectionIdx < 0)
            sectionIdx = readStrLC.length();
        // Extract the check data
        std::vector<uint8_t> readDataMask;
        std::vector<uint8_t> readDataCheck;
        uint32_t pauseAfterSendMs = 0;
        if (!extractMaskAndDataFromHexStr(readStrLC.substring(0, sectionIdx), readDataMask, readDataCheck, maskToZeros, pauseAfterSendMs))
        {
            return false;
        }
        checkValues.push_back(std::make_pair(readDataMask, readDataCheck));
        readStrLC = readStrLC.substring(sectionIdx + 1);
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Extract read data size
/// @param readStr hex string
/// @return number of bytes to read
uint32_t DeviceTypeRecords::extractReadDataSize(const String& readStr)
{
    String readStrLC = readStr;
    readStrLC.toLowerCase();
    // Check for readStr starts with rNNNN (for read NNNN bytes)
    if (readStrLC.startsWith("r"))
    {
        return strtol(readStrLC.c_str() + 1, NULL, 10);
    }
    // Check if readStr starts with 0b 
    if (readStrLC.startsWith("0b"))
    {
        return (readStrLC.length() - 2 + 7) / 8;
    }
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Extract bar access time
/// @param readStr hex string
/// @return bar access time in ms
uint32_t DeviceTypeRecords::extractBarAccessMs(const String& readStr)
{
    String readStrLC = readStr;
    readStrLC.toLowerCase();
    // Check for readStr having pNN in it
    int pauseIdx = readStrLC.indexOf("p");
    if (pauseIdx < 0)
    {
        return 0;
    }
    return strtol(readStrLC.c_str() + pauseIdx + 1, NULL, 10);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get detection records
/// @param deviceType device type
/// @param detectionRecs (out) detection records
void DeviceTypeRecords::getDetectionRecs(const DeviceTypeRecord* pDevTypeRec, std::vector<DeviceDetectionRec>& detectionRecs)
{
    // Clear initially
    detectionRecs.clear();
    if (!pDevTypeRec)
        return;

    // Get the detection values
    String detectionValues = pDevTypeRec->detectionValues;

    // Extract name:value pairs
    std::vector<RaftJson::NameValuePair> detectionWriteReadPairs;
    RaftJson::extractNameValues(detectionValues, "=", "&", ";", detectionWriteReadPairs);

    // Convert to detection records
    for (const auto& detectionNameValue : detectionWriteReadPairs)
    {
        DeviceDetectionRec detectionRec;
        if (!extractBufferDataFromHexStr(detectionNameValue.name, detectionRec.writeData))
            continue;
            
        // Check if this is a response with CRC validation
        if (detectionNameValue.value.indexOf("{crc:") >= 0) {
            // Set multi-field check flag
            detectionRec.useMultiFieldCheck = true;
            
            // Extract field checks
            if (!extractFieldChecksFromStr(detectionNameValue.value, detectionRec.fieldChecks, true))
                continue;
                
            // Also extract traditional check values for backward compatibility
            if (!extractCheckInfoFromHexStr(detectionNameValue.value, detectionRec.checkValues, true))
                continue;
        } else {
            // Traditional format without CRC validation
            if (!extractCheckInfoFromHexStr(detectionNameValue.value, detectionRec.checkValues, true))
                continue;
        }
        
        detectionRec.pauseAfterSendMs = extractBarAccessMs(detectionNameValue.value);
        detectionRecs.push_back(detectionRec);
    }

#ifdef DEBUG_DEVICE_INFO_RECORDS
    LOG_I(MODULE_PREFIX, "getDetectionRecs %d recs detectionStr %s", detectionRecs.size(), detectionValues.c_str());
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Convert poll response to JSON
/// @param addr address
/// @param isOnline true if device is online
/// @param pDevTypeRec pointer to device type record
/// @param devicePollResponseData device poll response data
String DeviceTypeRecords::deviceStatusToJson(BusElemAddrType addr, bool isOnline, const DeviceTypeRecord* pDevTypeRec, 
        const std::vector<uint8_t>& devicePollResponseData) const
{
    // Device type name
    String devTypeName = pDevTypeRec ? pDevTypeRec->deviceType : "";
    // Form a hex buffer
    String hexOut;
    Raft::getHexStrFromBytes(devicePollResponseData.data(), devicePollResponseData.size(), hexOut);
    return "\"" + String(addr, 16) + "\":{\"x\":\"" + hexOut + "\",\"_o\":" + String(isOnline ? "1" : "0") + ",\"_t\":\"" + devTypeName + "\"}";
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get device type info JSON by device type index
/// @param deviceTypeIdx device type index
/// @param includePlugAndPlayInfo include plug and play info
/// @return JSON string
String DeviceTypeRecords::getDevTypeInfoJsonByTypeIdx(uint16_t deviceTypeIdx, bool includePlugAndPlayInfo) const
{
    // Get device type record
    DeviceTypeRecord devTypeRec;
    if (getDeviceInfo(deviceTypeIdx, devTypeRec))
        return devTypeRec.getJson(includePlugAndPlayInfo);
    return "{}";
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get device type info JSON by device type name
/// @param deviceTypeName device type name
/// @param includePlugAndPlayInfo include plug and play info
/// @return JSON string
String DeviceTypeRecords::getDevTypeInfoJsonByTypeName(const String& deviceTypeName, bool includePlugAndPlayInfo) const
{
    // Get the device type info
    DeviceTypeRecord devTypeRec;
    uint32_t deviceTypeIdx = 0;
    if (getDeviceInfo(deviceTypeName, devTypeRec, deviceTypeIdx))
        return devTypeRec.getJson(includePlugAndPlayInfo);
    return "{}";
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get scan priority lists
/// @param priorityLists (out) priority lists
void DeviceTypeRecords::getScanPriorityLists(std::vector<std::vector<BusElemAddrType>>& priorityLists)
{
    // Clear initially
    priorityLists.clear();

    // Resize list
    priorityLists.resize(numScanPriorityLists);
    for (int i = 0; i < numScanPriorityLists; i++)
    {
        for (int j = 0; j < scanPriorityListLengths[i]; j++)
        {
            priorityLists[i].push_back(scanPriorityLists[i][j]);
        }
    }

    // Add any extended device type record addresses to the highest priority list
    if (_extendedRecordsAdded && RaftMutex_lock(_extDeviceTypeRecordsMutex, UINT32_MAX))
    {
        for (const auto& extDevTypeRec : _extendedDevTypeRecords)
        {
            std::vector<int> addressList;
            Raft::parseIntList(extDevTypeRec.addresses.c_str(), addressList, ",");
            for (int devAddr : addressList)
            {
                if (devAddr >= 0 && devAddr < 0x10000)
                {
                    priorityLists[0].push_back(devAddr);
                }
            }
        }
        RaftMutex_unlock(_extDeviceTypeRecordsMutex);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Add extended device type record
/// @param devTypeRec device type record
/// @param deviceTypeIndex (out) device type index
/// @return true if added
bool DeviceTypeRecords::addExtendedDeviceTypeRecord(const DeviceTypeRecordDynamic& devTypeRec, uint16_t& deviceTypeIndex)
{
    bool isAdded = false;
    if (RaftMutex_lock(_extDeviceTypeRecordsMutex, UINT32_MAX))
    {
        // Check if already exists (same device type name)
        for (const auto& extDevTypeRec : _extendedDevTypeRecords)
        {
            if (extDevTypeRec.deviceTypeName == devTypeRec.deviceTypeName)
            {
                RaftMutex_unlock(_extDeviceTypeRecordsMutex);
                return false; // Already exists
            }
        }
        // Add the record
        _extendedDevTypeRecords.push_back(devTypeRec);
        deviceTypeIndex = BASE_DEV_TYPE_ARRAY_SIZE + _extendedDevTypeRecords.size() - 1;
        isAdded = true;

        // Update flag
        _extendedRecordsAdded = true;
        LOG_I(MODULE_PREFIX, "Extended device type record added: %s (idx=%d)", devTypeRec.deviceTypeName.c_str(), deviceTypeIndex);
        
        // TODO - save to non-volatile storage
        // ...
        
        RaftMutex_unlock(_extDeviceTypeRecordsMutex);
    }
    return isAdded;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Extract CRC validation from string
/// @param crcStr CRC validation string (e.g. "{crc:crc-sensirion-8,1}")
/// @param crcValidation CRC validation structure to fill
/// @return true if successful
bool DeviceTypeRecords::extractCRCValidationFromStr(const String& crcStr, DeviceDetectionRec::CRCValidation& crcValidation)
{
    // Format: {crc:<algorithm>,<size>}
    // Default values
    crcValidation.algorithm = DeviceDetectionRec::CRCAlgorithm::NONE;
    crcValidation.size = 0;
    
    // Check for correct format
    if (crcStr.length() < 6 || !crcStr.startsWith("{crc:") || !crcStr.endsWith("}"))
        return false;
    
    // Extract algorithm and size
    String innerPart = crcStr.substring(5, crcStr.length() - 1);
    int commaPos = innerPart.indexOf(',');
    if (commaPos <= 0)
        return false;
        
    String algorithmStr = innerPart.substring(0, commaPos);
    String sizeStr = innerPart.substring(commaPos + 1);
    algorithmStr.trim();
    sizeStr.trim();
    
    // Parse algorithm
    if (algorithmStr == "crc-sensirion-8") {
        crcValidation.algorithm = DeviceDetectionRec::CRCAlgorithm::CRC_SENSIRION_8;
    } else if (algorithmStr == "crc-max30101-8") {
        crcValidation.algorithm = DeviceDetectionRec::CRCAlgorithm::CRC_MAX30101_8;
    } else {
        return false; // Unknown algorithm
    }
    
    // Parse size
    crcValidation.size = sizeStr.toInt();
    if (crcValidation.size <= 0 || crcValidation.size > 8)
        return false;
        
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Extract field checks from string
/// @param readStr String containing field checks (format: "0xADDR=XXXX{crc:...}XXXX{crc:...}")
/// @param fieldChecks Vector of field checks to fill
/// @param maskToZeros Whether X in hex should be masked to zeros
/// @return true if successful
bool DeviceTypeRecords::extractFieldChecksFromStr(const String& readStr, std::vector<DeviceDetectionRec::FieldCheck>& fieldChecks, bool maskToZeros)
{
    fieldChecks.clear();
    
    // First, check if this contains an '=' separator (address=data format)
    int equalsPos = readStr.indexOf('=');
    if (equalsPos < 0) {
        // No '=' means invalid format for CRC validation
        return false;
    }
    
    // Skip the address part (everything before '=')
    String dataStr = readStr.substring(equalsPos + 1);
    
    // Now parse the data part which contains fields and CRC markers
    int curPos = 0;
    
    while (curPos < dataStr.length()) {
        DeviceDetectionRec::FieldCheck fieldCheck;
        
        // Find the next data field (starting with XXXX or 0x or 0b)
        // Skip any leading characters
        while (curPos < dataStr.length() && dataStr.charAt(curPos) != 'X' && 
               dataStr.charAt(curPos) != 'x' && dataStr.charAt(curPos) != '0') {
            curPos++;
        }
        
        if (curPos >= dataStr.length())
            break;
        
        // Determine the data part end (either '{' for CRC or next field)
        int dataEnd = curPos;
        bool foundDataBytes = false;
        
        // Count the data bytes (X's or hex digits)
        while (dataEnd < dataStr.length() && dataStr.charAt(dataEnd) != '{') {
            char c = dataStr.charAt(dataEnd);
            if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || 
                (c >= 'a' && c <= 'f') || c == 'X' || c == 'x') {
                foundDataBytes = true;
                dataEnd++;
            } else if (c == ' ' || c == '&') {
                // Allow separators but stop at them if we've found data
                if (foundDataBytes)
                    break;
                dataEnd++;
            } else {
                break;
            }
        }
        
        if (!foundDataBytes) {
            return false;
        }
        
        // Extract the data part (just the hex/wildcard bytes)
        String dataPart = dataStr.substring(curPos, dataEnd);
        dataPart.trim();
        
        // Parse data and mask - prepend "0x" if not present
        if (!dataPart.startsWith("0x") && !dataPart.startsWith("0X") && 
            !dataPart.startsWith("0b") && !dataPart.startsWith("0B")) {
            dataPart = "0x" + dataPart;
        }
        
        std::vector<uint8_t> dataMask;
        std::vector<uint8_t> dataValue;
        uint32_t pauseMs = 0;
        if (!extractMaskAndDataFromHexStr(dataPart, dataMask, dataValue, maskToZeros, pauseMs)) {
            return false;
        }
        
        fieldCheck.dataToCheck = dataValue;  // Store the data pattern
        fieldCheck.mask = dataMask;
        fieldCheck.expectedValue = dataValue;
        
        // Check for CRC validation following this field
        if (dataEnd < dataStr.length() && dataStr.charAt(dataEnd) == '{') {
            int crcEnd = dataStr.indexOf("}", dataEnd);
            if (crcEnd < 0)
                return false;
                
            String crcPart = dataStr.substring(dataEnd, crcEnd + 1);
            fieldCheck.hasCRC = true;
            
            if (!extractCRCValidationFromStr(crcPart, fieldCheck.crcValidation)) {
                return false;
            }
            
            curPos = crcEnd + 1;
        } else {
            fieldCheck.hasCRC = false;
            curPos = dataEnd;
        }
        
        fieldChecks.push_back(fieldCheck);
    }
    
    return !fieldChecks.empty();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Calculate CRC using specified algorithm
/// @param data Pointer to data buffer
/// @param length Length of data
/// @param algorithm CRC algorithm to use
/// @return Calculated CRC value
uint8_t DeviceTypeRecords::calculateCRC(const uint8_t* data, size_t length, DeviceDetectionRec::CRCAlgorithm algorithm)
{
    switch (algorithm)
    {
        case DeviceDetectionRec::CRCAlgorithm::CRC_SENSIRION_8:
            return calculateSensirionCRC8(data, length);
        case DeviceDetectionRec::CRCAlgorithm::CRC_MAX30101_8:
            return calculateMAX30101CRC8(data, length);
        case DeviceDetectionRec::CRCAlgorithm::NONE:
        default:
            return 0;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Calculate Sensirion CRC-8
/// @param data Pointer to data buffer
/// @param length Length of data
/// @return Calculated CRC value
/// @note Algorithm: Polynomial 0x31 (x^8 + x^5 + x^4 + 1), Initial value 0xFF
///       Based on Sensirion embedded-i2c-scd4x driver implementation
///       Reference: https://github.com/Sensirion/embedded-i2c-scd4x/blob/main/sensirion_i2c.c
uint8_t DeviceTypeRecords::calculateSensirionCRC8(const uint8_t* data, size_t length)
{
    const uint8_t CRC8_POLYNOMIAL = 0x31;
    const uint8_t CRC8_INIT = 0xFF;
    
    uint8_t crc = CRC8_INIT;
    
    // Calculate 8-bit checksum with given polynomial
    for (size_t currentByte = 0; currentByte < length; ++currentByte)
    {
        crc ^= data[currentByte];
        for (uint8_t crcBit = 8; crcBit > 0; --crcBit)
        {
            if (crc & 0x80)
                crc = (crc << 1) ^ CRC8_POLYNOMIAL;
            else
                crc = (crc << 1);
        }
    }
    
    return crc;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Calculate MAX30101 CRC-8
/// @param data Pointer to data buffer
/// @param length Length of data
/// @return Calculated CRC value
/// @note This is a placeholder implementation. The MAX30101 datasheet should be consulted
///       for the specific CRC algorithm used by this sensor if different from Sensirion
uint8_t DeviceTypeRecords::calculateMAX30101CRC8(const uint8_t* data, size_t length)
{
    // For now, use the same CRC-8 algorithm as Sensirion
    // This may need to be updated based on MAX30101 datasheet specifications
    return calculateSensirionCRC8(data, length);
}
