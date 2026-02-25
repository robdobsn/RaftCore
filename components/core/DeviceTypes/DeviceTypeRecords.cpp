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

    // Initialize atomic bool
    RaftAtomicBool_init(_extendedRecordsAdded, false);

    // Reserve space for extended device type records (to avoid changing absolute pointer values)
    _extendedDevTypeRecords.reserve(MAX_EXTENDED_DEV_TYPE_RECORDS);
}

DeviceTypeRecords::~DeviceTypeRecords()
{
    // Delete mutex
    RaftMutex_destroy(_extDeviceTypeRecordsMutex);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief get device types (strings) for an address
/// @param addr address
/// @returns device type indexes (into generated baseDevTypeRecords array) that match the address
std::vector<DeviceTypeIndexType> DeviceTypeRecords::getDeviceTypeIdxsForAddr(BusElemAddrType addr) const
{
#ifdef DEBUG_DEVICE_INFO_PERFORMANCE
    uint64_t startTimeUs = micros();
#endif

    // Return value
    std::vector<DeviceTypeIndexType> devTypeIdxsForAddr;

    // Check if any of the extended device type records match
    if (RaftAtomicBool_get(_extendedRecordsAdded) && RaftMutex_lock(_extDeviceTypeRecordsMutex, RAFT_MUTEX_WAIT_FOREVER))
    {
        // Ext devType indices continue on from the base devType indices
        DeviceTypeIndexType devTypeIdx = BASE_DEV_TYPE_ARRAY_SIZE;
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
bool DeviceTypeRecords::getDeviceInfo(DeviceTypeIndexType deviceTypeIdx, DeviceTypeRecord& devTypeRec) const
{
    // Check if in range
    bool isValid = false;
    if (deviceTypeIdx >= BASE_DEV_TYPE_ARRAY_SIZE)
    {
        // Check extended device type records
        if (RaftAtomicBool_get(_extendedRecordsAdded) && RaftMutex_lock(_extDeviceTypeRecordsMutex, RAFT_MUTEX_WAIT_FOREVER))
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
bool DeviceTypeRecords::getDeviceInfo(const String& deviceTypeName, DeviceTypeRecord& devTypeRec, DeviceTypeIndexType& deviceTypeIdx) const
{
    // Iterate the extended device types first - so that device type names can be overridden
    bool isValid = false;
    uint32_t typeIdx = BASE_DEV_TYPE_ARRAY_SIZE;
    if (RaftAtomicBool_get(_extendedRecordsAdded) && RaftMutex_lock(_extDeviceTypeRecordsMutex, RAFT_MUTEX_WAIT_FOREVER))
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
        // Compute length
        uint32_t lenBytes = (readStrLC.length() - hexIdx - 2 + 1) / 2;
        // Extract the read data
        readDataMask.resize(lenBytes);
        readDataCheck.resize(lenBytes);
        Raft::getBytesFromHexStr(readStrLC.c_str() + hexIdx + 2, readDataCheck.data(), readDataCheck.size());
        for (int i = 0; i < readDataMask.size(); i++)
        {
            readDataMask[i] = maskToZeros ? 0xff : 0;
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
        if (!extractCheckInfoFromHexStr(detectionNameValue.value, detectionRec.checkValues, true))
            continue;
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
/// @param onlineState device online state
/// @param deviceTypeIndex device type index
/// @param devicePollResponseData device poll response data
String DeviceTypeRecords::deviceStatusToJson(BusElemAddrType addr, DeviceOnlineState onlineState,  DeviceTypeIndexType deviceTypeIndex, 
        const std::vector<uint8_t>& devicePollResponseData)
{
    // Form a hex buffer
    String hexOut;
    hexOut.reserve(hexOut.length() + 50);
    Raft::getHexStrFromBytes(devicePollResponseData.data(), devicePollResponseData.size(), hexOut);
    uint32_t publishValueForOnlineState = onlineState == DeviceOnlineState::ONLINE ? 1 : (onlineState == DeviceOnlineState::PENDING_DELETION ? 2 : 0);
    return "\"" + String(addr, 16) + "\":{\"x\":\"" + hexOut + "\",\"_o\":" + String(publishValueForOnlineState) + ",\"_i\":" + String(deviceTypeIndex) + "}";
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get device type info JSON by device type index
/// @param deviceTypeIdx device type index
/// @param includePlugAndPlayInfo include plug and play info
/// @return JSON string
String DeviceTypeRecords::getDevTypeInfoJsonByTypeIdx(DeviceTypeIndexType deviceTypeIdx, bool includePlugAndPlayInfo) const
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
/// @param deviceTypeIdx (out) device type index
/// @return JSON string
String DeviceTypeRecords::getDevTypeInfoJsonByTypeName(const String& deviceTypeName, bool includePlugAndPlayInfo, DeviceTypeIndexType& deviceTypeIdx) const
{
    // Get the device type info
    DeviceTypeRecord devTypeRec;
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
    if (RaftAtomicBool_get(_extendedRecordsAdded) && RaftMutex_lock(_extDeviceTypeRecordsMutex, RAFT_MUTEX_WAIT_FOREVER))
    {
        for (const auto& extDevTypeRec : _extendedDevTypeRecords)
        {
            std::vector<int> addressList;
            Raft::parseIntList(extDevTypeRec.addresses.c_str(), addressList, ",");
            for (int devAddr : addressList)
            {
                if (priorityLists.size() == 0)
                    priorityLists.push_back(std::vector<BusElemAddrType>());
                priorityLists[0].push_back(devAddr);
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
bool DeviceTypeRecords::addExtendedDeviceTypeRecord(const DeviceTypeRecordDynamic& devTypeRec, DeviceTypeIndexType& deviceTypeIndex)
{
    // Lock
    if (!RaftMutex_lock(_extDeviceTypeRecordsMutex, RAFT_MUTEX_WAIT_FOREVER))
        return false;

    // Check if max number of records reached
    if (_extendedDevTypeRecords.size() >= MAX_EXTENDED_DEV_TYPE_RECORDS)
    {
        RaftMutex_unlock(_extDeviceTypeRecordsMutex);
#ifdef DEBUG_ADD_EXTENDED_DEVICE_TYPE_RECORD
        LOG_W(MODULE_PREFIX, "addExtendedDeviceTypeRecord MAX_EXTENDED_DEV_TYPE_RECORDS reached");
#endif
        return false;
    }

    // Check if already added
    bool recFound = false;
    for (uint16_t i = 0; i < _extendedDevTypeRecords.size(); i++)
    {
        if (_extendedDevTypeRecords[i].nameMatches(devTypeRec))
        {
            recFound = true;
            deviceTypeIndex = i + BASE_DEV_TYPE_ARRAY_SIZE;
            break;
        }
    }

    // Add to list
    if (!recFound)
    {
        _extendedDevTypeRecords.push_back(devTypeRec);
        RaftAtomicBool_set(_extendedRecordsAdded, true);
        deviceTypeIndex = _extendedDevTypeRecords.size() - 1 + BASE_DEV_TYPE_ARRAY_SIZE;
    }

    // Unlock
    RaftMutex_unlock(_extDeviceTypeRecordsMutex);

#ifdef DEBUG_ADD_EXTENDED_DEVICE_TYPE_RECORD
    LOG_I(MODULE_PREFIX, "addExtendedDeviceTypeRecord %s type %s devTypeIdx %d addrs %s detVals %s initVals %s pollInfo %s",
                recFound ? "ALREADY PRESENT" : "ADDED OK",
                devTypeRec.deviceTypeName_.c_str(), 
                deviceTypeIndex,
                devTypeRec.addresses_.c_str(), devTypeRec.detectionValues_.c_str(),
                devTypeRec.initValues_.c_str(), devTypeRec.pollInfo_.c_str());
#endif
    return !recFound;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get device poll decode function
/// @param deviceTypeIdx device type index
/// @return poll decode function
DeviceTypeRecordDecodeFn DeviceTypeRecords::getPollDecodeFn(DeviceTypeIndexType deviceTypeIdx) const
{

    // TODO - see if copying can be avoided

    // Get device type record
    DeviceTypeRecord devTypeRec;
    if (!getDeviceInfo(deviceTypeIdx, devTypeRec))
        return nullptr;
    return devTypeRec.pollResultDecodeFn;
}
