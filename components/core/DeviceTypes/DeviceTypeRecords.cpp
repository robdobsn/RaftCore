/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device type records
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "DeviceTypeRecords.h"
#include "DeviceTypeRecordPacked.h"
#include "PlatformUtils.h"
#include "BusRequestInfo.h"
#include "PollReadLenExpr.h"
#include "RaftJson.h"
#include <cctype>
#include <cstring>
#include <algorithm>

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
    RaftMutex_init(_extRecordsMutex);
}

DeviceTypeRecords::~DeviceTypeRecords()
{
    // Free extended records
    uint32_t count = _extCount.load(std::memory_order_acquire);
    for (uint32_t i = 0; i < count; i++)
        DeviceTypeRecordPacked::destroy(_extRecords[i]);
    delete[] _extRecords;

    // Delete mutex
    RaftMutex_destroy(_extRecordsMutex);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Number of base records
uint32_t DeviceTypeRecords::getNumBaseRecords()
{
    return BASE_DEV_TYPE_ARRAY_SIZE;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief get device types (strings) for an address
/// @param addr address
/// @returns device type indexes (into generated baseDevTypeRecords array) that match the address
std::vector<DeviceTypeIndexType> DeviceTypeRecords::getDeviceTypeIdxsForAddr(BusElemAddrType addr) const
{
    return getKnownTypesAtAddr(addr);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get the catalogue index, rebuilt if records have been added
std::shared_ptr<const CatalogueIndex> DeviceTypeRecords::getCatalogueIndex() const
{
    if (!RaftMutex_lock(_extRecordsMutex, RAFT_MUTEX_WAIT_FOREVER))
        return std::make_shared<const CatalogueIndex>();
    uint32_t generation = _generation.load(std::memory_order_acquire);
    if (!_catalogueIndex || (_catalogueIndex->getGeneration() != generation))
    {
#ifdef DEBUG_DEVICE_INFO_PERFORMANCE
        uint64_t startUs = micros();
#endif
        auto pIndex = std::make_shared<CatalogueIndex>();
        pIndex->build(generation, BASE_DEV_TYPE_ARRAY_SIZE, _extCount.load(std::memory_order_acquire),
                [this](DeviceTypeIndexType idx) { return getRecord(idx); });
        _catalogueIndex = pIndex;
#ifdef DEBUG_DEVICE_INFO_PERFORMANCE
        LOG_I(MODULE_PREFIX, "getCatalogueIndex built generation %d in %d us, %d bytes", (int)generation,
                    (int)(micros() - startUs), (int)pIndex->getBytesUsed());
#endif
    }
    std::shared_ptr<const CatalogueIndex> pIndex = _catalogueIndex;
    RaftMutex_unlock(_extRecordsMutex);
    return pIndex;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Identification candidates for an address, in the order to try them
std::vector<DeviceTypeIndexType> DeviceTypeRecords::getIdentCandidates(BusElemAddrType addr) const
{
    std::shared_ptr<const CatalogueIndex> pIndex = getCatalogueIndex();
    uint32_t count = 0;
    const DeviceTypeIndexType* pCands = pIndex->getIdentCandidates(addr, count);
    return pCands ? std::vector<DeviceTypeIndexType>(pCands, pCands + count) : std::vector<DeviceTypeIndexType>();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Records known at an address
std::vector<DeviceTypeIndexType> DeviceTypeRecords::getKnownTypesAtAddr(BusElemAddrType addr) const
{
    // The same records as the identification candidates until records can be disabled (a disabled record is
    // still known, so its address stays reserved, but it isn't a candidate)
    return getIdentCandidates(addr);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check whether a record identifies a device at this address
bool DeviceTypeRecords::isAddrReserved(BusElemAddrType addr) const
{
    return getCatalogueIndex()->isAddrReserved(addr);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get the record with a hardware type ID
const DeviceTypeRecord* DeviceTypeRecords::getRecordByHwTypeId(uint16_t hwTypeId, DeviceTypeIndexType& deviceTypeIndex) const
{
    deviceTypeIndex = DEVICE_TYPE_INDEX_INVALID;
    if (!getCatalogueIndex()->findByHwTypeId(hwTypeId, deviceTypeIndex))
        return nullptr;
    return getRecord(deviceTypeIndex);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get the device type record for a device type index (no copy, no lock)
/// @param deviceTypeIndex device type index
/// @return the record, or nullptr if there is none
const DeviceTypeRecord* DeviceTypeRecords::getRecord(DeviceTypeIndexType deviceTypeIndex) const
{
    if (deviceTypeIndex < BASE_DEV_TYPE_ARRAY_SIZE)
        return &baseDevTypeRecords[deviceTypeIndex];
    uint32_t extPos = deviceTypeIndex - BASE_DEV_TYPE_ARRAY_SIZE;
    if (extPos < _extCount.load(std::memory_order_acquire))
        return _extRecords[extPos];
    return nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get the device type record for a device type key (extended records take precedence)
/// @param deviceTypeKey device type key
/// @param deviceTypeIndex (out) device type index (DEVICE_TYPE_INDEX_INVALID if not found)
/// @return the record, or nullptr if there is none
const DeviceTypeRecord* DeviceTypeRecords::getRecord(const char* deviceTypeKey, DeviceTypeIndexType& deviceTypeIndex) const
{
    deviceTypeIndex = DEVICE_TYPE_INDEX_INVALID;
    if (!deviceTypeKey)
        return nullptr;

    // Extended records first, so a key can be overridden
    if ((_extCount.load(std::memory_order_acquire) > 0) && RaftMutex_lock(_extRecordsMutex, RAFT_MUTEX_WAIT_FOREVER))
    {
        int32_t extPos = findExtendedPosByKey(deviceTypeKey);
        const DeviceTypeRecord* pRec = (extPos >= 0) ? _extRecords[extPos] : nullptr;
        RaftMutex_unlock(_extRecordsMutex);
        if (pRec)
        {
            deviceTypeIndex = BASE_DEV_TYPE_ARRAY_SIZE + extPos;
            return pRec;
        }
    }

    // Base records - binary search of the generated index sorted by key
    int32_t lo = 0;
    int32_t hi = (int32_t)BASE_DEV_TYPE_ARRAY_SIZE - 1;
    while (lo <= hi)
    {
        int32_t mid = (lo + hi) / 2;
        uint16_t recIdx = baseDevTypeIndexByKey[mid];
        int cmp = strcmp(baseDevTypeRecords[recIdx].deviceType, deviceTypeKey);
        if (cmp == 0)
        {
            deviceTypeIndex = recIdx;
            return &baseDevTypeRecords[recIdx];
        }
        if (cmp < 0)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get device type for a device type index
/// @param deviceTypeIndex device type index
/// @param devTypeRec (out) device type record
/// @return true if device type found
bool DeviceTypeRecords::getDeviceInfo(DeviceTypeIndexType deviceTypeIndex, DeviceTypeRecord& devTypeRec) const
{
    const DeviceTypeRecord* pRec = getRecord(deviceTypeIndex);
#ifdef DEBUG_LOOKUP_DEVICE_TYPE_BY_INDEX
    LOG_I(MODULE_PREFIX, "getDeviceInfo %d %s %s", deviceTypeIndex, pRec ? "OK" : "NOT FOUND",
                pRec ? (pRec->deviceType ? pRec->deviceType : "NO NAME") : "INVALID");
#endif
    if (!pRec)
        return false;
    devTypeRec = *pRec;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get device type for a device type key
/// @param deviceTypeKey device type key
/// @param devTypeRec (out) device type record
/// @param deviceTypeIndex (out) device type index
/// @return true if device type found
bool DeviceTypeRecords::getDeviceInfo(const String& deviceTypeKey, DeviceTypeRecord& devTypeRec, DeviceTypeIndexType& deviceTypeIndex) const
{
    DeviceTypeIndexType foundIndex = DEVICE_TYPE_INDEX_INVALID;
    const DeviceTypeRecord* pRec = getRecord(deviceTypeKey.c_str(), foundIndex);
#ifdef DEBUG_LOOKUP_DEVICE_TYPE_BY_NAME
    LOG_I(MODULE_PREFIX, "getDeviceInfo %s %s idx %d", deviceTypeKey.c_str(), pRec ? "OK" : "NOT FOUND", foundIndex);
#endif
    if (!pRec)
        return false;
    devTypeRec = *pRec;
    deviceTypeIndex = foundIndex;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Number of poll results to store, from a record's poll info ("s", default 1, at least 1)
static uint32_t getNumPollResultsToStore(const RaftJson& pollInfo)
{
    long numResults = pollInfo.getLong("s", 1);
    return numResults < 1 ? 1 : (uint32_t)numResults;
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
    {
        // Externally supplied data ("ext": 1) - the device is not polled; its samples are
        // pushed in by other code via handlePollResult(). Size the result store from the
        // record's response size (resp.b) so those samples can be held and published.
        if (pollInfo.getLong("ext", 0) != 0)
        {
            pollingInfo.numPollResultsToStore = getNumPollResultsToStore(pollInfo);
            pollingInfo.pollResultSizeIncTimestamp = pDevTypeRec->pollDataSizeBytes + DevicePollingInfo::POLL_RESULT_TIMESTAMP_SIZE;
        }
        return;
    }

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

        // Check for dynamic read length expression r{...}
        String valueStr = pollWriteReadPair.value;
        int dynReadStart = valueStr.indexOf("r{");
        if (dynReadStart >= 0)
        {
            int dynReadEnd = valueStr.indexOf("}", dynReadStart + 2);
            if (dynReadEnd < 0)
            {
#ifdef DEBUG_POLL_REQUEST_REQS
                LOG_I(MODULE_PREFIX, "getPollInfo FAIL unmatched r{ in %s", valueStr.c_str());
#endif
                continue;
            }

            // Extract the expression between r{ and }
            String exprStr = valueStr.substring(dynReadStart + 2, dynReadEnd);
            auto readLenExpr = std::make_shared<PollReadLenExpr>();
            if (!readLenExpr->parse(exprStr))
            {
#ifdef DEBUG_POLL_REQUEST_REQS
                LOG_I(MODULE_PREFIX, "getPollInfo FAIL parse dynamic read expr %s", exprStr.c_str());
#endif
                continue;
            }

            // Expression must have a :max value for buffer sizing
            if (!readLenExpr->hasMaxValue())
            {
#ifdef DEBUG_POLL_REQUEST_REQS
                LOG_I(MODULE_PREFIX, "getPollInfo FAIL dynamic read expr %s has no :max", exprStr.c_str());
#endif
                continue;
            }

            uint32_t maxReadLen = readLenExpr->getMaxValue();
            uint32_t pauseAfterSendMs = extractBarAccessMs(valueStr);

            // Create the poll request with the max read length
            BusRequestInfo pollReq(BUS_REQ_TYPE_POLL,
                    addr,
                    DevicePollingInfo::DEV_IDENT_POLL_CMD_ID,
                    writeData.size(),
                    writeData.data(),
                    maxReadLen,
                    pauseAfterSendMs,
                    NULL,
                    NULL);
            pollReq.setReadLenExpr(readLenExpr);
            pollingInfo.pollReqs.push_back(pollReq);

            // Use max read length for buffer sizing
            pollResultDataSize += maxReadLen;

#ifdef DEBUG_POLL_REQUEST_REQS
            LOG_I(MODULE_PREFIX, "getPollInfo addr %04x dynReadExpr %s maxReadLen %d", addr, exprStr.c_str(), maxReadLen);
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
    pollingInfo.numPollResultsToStore = getNumPollResultsToStore(pollInfo);

    // Get polling interval (ms in the record), with a default if missing and clamped to the minimum allowed
    long pollIntervalMs = pollInfo.getLong("i", -1);
    pollingInfo.pollIntervalUs = (pollIntervalMs < 0) ? DevicePollingInfo::POLL_INTERVAL_DEFAULT_US :
                DevicePollingInfo::clampPollIntervalUs((uint64_t)pollIntervalMs * 1000);

    // Get bus frequency override for polls (0 = use bus default)
    // Accept both compact key "h" and verbose key "busHz"
    pollingInfo.pollBusHz = pollInfo.getLong("h", 0);
    if (pollingInfo.pollBusHz == 0)
        pollingInfo.pollBusHz = pollInfo.getLong("busHz", 0);

    // Get slot mask for bus frequency override (0 = all slots)
    // Accept both compact key "hSlots" and verbose key "busHzSlots"
    // Value is a JSON array of slot numbers, e.g. [4,5,6]
    pollingInfo.pollBusHzSlotMask = 0;
    String slotMaskKey = "hSlots";
    int slotsArrayLen = 0;
    if (pollInfo.getType(slotMaskKey.c_str(), slotsArrayLen) != RaftJsonIF::RAFT_JSON_ARRAY || slotsArrayLen == 0)
    {
        slotMaskKey = "busHzSlots";
        pollInfo.getType(slotMaskKey.c_str(), slotsArrayLen);
    }
    for (int slotIdx = 0; slotIdx < slotsArrayLen; slotIdx++)
    {
        String slotPath = slotMaskKey + "[" + String(slotIdx) + "]";
        int slotNum = pollInfo.getLong(slotPath.c_str(), -1);
        if (slotNum >= 0 && slotNum < 64)
            pollingInfo.pollBusHzSlotMask |= (1ULL << slotNum);
    }

    // Poll response integrity (CRC + trailer), e.g.
    //   "crc": { "t": "crc16ccitt", "trailer": 3, "reread": "0x22", "retries": 1 }
    // Absent (or an unrecognised type) leaves polling behaviour exactly as before.
    // See RaftI2C devdocs/i2c-poll-data-integrity-crc-plan.md
    String crcTypeStr = pollInfo.getString("crc/t", "");
    if (crcTypeStr.equalsIgnoreCase("crc16ccitt"))
    {
        pollingInfo.pollCrcType = DevicePollingInfo::POLL_CRC_16_CCITT;
        pollingInfo.pollCrcTrailerLen = pollInfo.getLong("crc/trailer", 3);
        pollingInfo.pollCrcRetries = pollInfo.getLong("crc/retries", 1);
        String rereadStr = pollInfo.getString("crc/reread", "");
        if (rereadStr.length() > 0)
            extractBufferDataFromHexStr(rereadStr, pollingInfo.pollCrcRereadCmd);
        // Without a re-read command there is nothing to retry with - detect and drop.
        if (pollingInfo.pollCrcRereadCmd.size() == 0)
            pollingInfo.pollCrcRetries = 0;

        // Optional recovery backstop for a device that has stopped producing valid
        // responses altogether (e.g. an RSAO that reset into its bootloader).
        String recoverStr = pollInfo.getString("crc/recover", "");
        if (recoverStr.length() > 0)
            extractBufferDataFromHexStr(recoverStr, pollingInfo.pollCrcRecoverCmd);
        pollingInfo.pollCrcRecoverAfter = pollInfo.getLong("crc/recoverAfter", 0);
        if (pollingInfo.pollCrcRecoverCmd.size() == 0)
            pollingInfo.pollCrcRecoverAfter = 0;

        // The trailer is stripped once validated, so the stored result (and hence the
        // decode and the record's resp.b) stays the size it was before the CRC was
        // added - only the read length grows.
        if (pollResultDataSize >= pollingInfo.pollCrcTrailerLen)
            pollResultDataSize -= pollingInfo.pollCrcTrailerLen;

#ifdef DEBUG_POLL_REQUEST_REQS
        LOG_I(MODULE_PREFIX, "getPollInfo addr %04x crc16ccitt trailer %d retries %d rereadLen %d storedSize %d",
                    addr, (int)pollingInfo.pollCrcTrailerLen, (int)pollingInfo.pollCrcRetries,
                    (int)pollingInfo.pollCrcRereadCmd.size(), (int)pollResultDataSize);
#endif
    }
    else if (crcTypeStr.length() > 0)
    {
        LOG_W(MODULE_PREFIX, "getPollInfo addr %04x UNKNOWN crc type %s - integrity checking disabled",
                    addr, crcTypeStr.c_str());
    }

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
        // Compute length - a zero or missing length would be an empty check that matches anything
        uint32_t lenBytes = strtol(readStrLC.c_str() + readIdx + 1, NULL, 10);
        if (lenBytes == 0)
            return false;

        // Extract the read data
        readDataMask.resize(lenBytes);
        readDataCheck.resize(lenBytes);

        // Fill the same elements as before (indices readIdx to length-2) but only those within the
        // vectors - the loop runs over the string while the vectors are sized from the number in it,
        // so e.g. "r1p50" or "0x60=r1" previously wrote past the end of the heap allocation
        const int lastIdx = (int)readStrLC.length() - 2;
        for (int i = readIdx; (i <= lastIdx) && (i < (int)lenBytes); i++)
        {
            readDataMask[i] = maskToZeros ? 0xff : 0;
            readDataCheck[i] = 0;
        }
        return true;
    }

    // The kind of value (hex or binary) is decided by its prefix, after any leading whitespace and
    // optional leading pause (e.g. "p10 0x55"). Searching for "0x" anywhere is wrong because a binary
    // value contains "0x" wherever a 0 bit is followed by a don't-care bit (e.g. "0b1010xxxx")
    int valueIdx = 0;
    while ((valueIdx < (int)readStrLC.length()) && isspace(readStrLC[valueIdx]))
        valueIdx++;
    if ((valueIdx < (int)readStrLC.length()) && (readStrLC[valueIdx] == 'p'))
    {
        valueIdx++;
        while ((valueIdx < (int)readStrLC.length()) && isdigit(readStrLC[valueIdx]))
            valueIdx++;
        while ((valueIdx < (int)readStrLC.length()) && isspace(readStrLC[valueIdx]))
            valueIdx++;
    }
    const char* pValue = readStrLC.c_str() + valueIdx;

    // Check if the value starts with 0x
    if (strncmp(pValue, "0x", 2) == 0)
    {
        int hexIdx = valueIdx;
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

    // Check if the value starts with 0b
    else if (strncmp(pValue, "0b", 2) == 0)
    {
        int binIdx = valueIdx;
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

    // Otherwise the only valid content is an optional pause (pNN), as in a write-then-pause step
    // such as "0x00FF=p25", which deliberately has nothing to read or check. Anything else (e.g.
    // "A005", a hex value missing its 0x prefix) is an error - accepting it as an empty check would
    // make detection match any device that ACKs the write
    for (uint32_t i = 0; i < readStrLC.length(); i++)
    {
        if (readStrLC[i] == 'p')
        {
            while ((i + 1 < readStrLC.length()) && isdigit(readStrLC[i + 1]))
                i++;
            continue;
        }
        if (!isspace(readStrLC[i]))
            return false;
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
/// @return false if any detection value is invalid (detectionRecs is then empty and the record must not match)
bool DeviceTypeRecords::getDetectionRecs(const DeviceTypeRecord* pDevTypeRec, std::vector<DeviceDetectionRec>& detectionRecs)
{
    // Clear initially
    detectionRecs.clear();
    if (!pDevTypeRec)
        return false;

    // Get the detection values
    String detectionValues = pDevTypeRec->detectionValues;

    // Extract name:value pairs
    std::vector<RaftJson::NameValuePair> detectionWriteReadPairs;
    RaftJson::extractNameValues(detectionValues, "=", "&", ";", detectionWriteReadPairs);

    // Convert to detection records
    // An invalid entry invalidates the whole record rather than being skipped - skipping it would
    // leave fewer checks (or none) and so make the record easier to match, not harder
    for (const auto& detectionNameValue : detectionWriteReadPairs)
    {
        DeviceDetectionRec detectionRec;
        if (!extractBufferDataFromHexStr(detectionNameValue.name, detectionRec.writeData) ||
            !extractCheckInfoFromHexStr(detectionNameValue.value, detectionRec.checkValues, true))
        {
            LOG_W(MODULE_PREFIX, "getDetectionRecs %s invalid detection value %s=%s",
                        pDevTypeRec->deviceType ? pDevTypeRec->deviceType : "?",
                        detectionNameValue.name.c_str(), detectionNameValue.value.c_str());
            detectionRecs.clear();
            return false;
        }
        detectionRec.pauseAfterSendMs = extractBarAccessMs(detectionNameValue.value);
        detectionRecs.push_back(detectionRec);
    }

#ifdef DEBUG_DEVICE_INFO_RECORDS
    LOG_I(MODULE_PREFIX, "getDetectionRecs %d recs detectionStr %s", detectionRecs.size(), detectionValues.c_str());
#endif
    return true;
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
/// @param deviceTypeIndex device type index
/// @param includePlugAndPlayInfo include plug and play info
/// @return JSON string
String DeviceTypeRecords::getDevTypeInfoJsonByTypeIdx(DeviceTypeIndexType deviceTypeIndex, bool includePlugAndPlayInfo) const
{
    // Get device type record
    DeviceTypeRecord devTypeRec;
    if (getDeviceInfo(deviceTypeIndex, devTypeRec))
        return devTypeRec.getJson(includePlugAndPlayInfo);
    return "{}";
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get device type info JSON by device type key
/// @param deviceTypeKey device type key
/// @param includePlugAndPlayInfo include plug and play info
/// @param deviceTypeIndex (out) device type index
/// @return JSON string
String DeviceTypeRecords::getDevTypeInfoJsonByTypeName(const String& deviceTypeKey, bool includePlugAndPlayInfo, DeviceTypeIndexType& deviceTypeIndex) const
{
    // Get the device type info
    DeviceTypeRecord devTypeRec;
    if (getDeviceInfo(deviceTypeKey, devTypeRec, deviceTypeIndex))
        return devTypeRec.getJson(includePlugAndPlayInfo);
    return "{}";
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get scan priority lists
/// @param priorityLists (out) priority lists
void DeviceTypeRecords::getScanPriorityLists(std::vector<std::vector<BusElemAddrType>>& priorityLists)
{
    std::shared_ptr<const CatalogueIndex> pIndex = getCatalogueIndex();
    const std::vector<std::vector<uint8_t>>& scanLists = pIndex->getScanLists();
    priorityLists.clear();
    priorityLists.resize(scanLists.size());
    for (uint32_t i = 0; i < scanLists.size(); i++)
        priorityLists[i].assign(scanLists[i].begin(), scanLists[i].end());
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Add an extended device type record
/// @param devTypeRec device type record
/// @param deviceTypeIndex (out) device type index (DEVICE_TYPE_INDEX_INVALID unless Added or AlreadyPresent)
/// @return result
DeviceTypeRecords::AddResult DeviceTypeRecords::addRecord(const DeviceTypeRecordDynamic& devTypeRec, DeviceTypeIndexType& deviceTypeIndex)
{
    deviceTypeIndex = DEVICE_TYPE_INDEX_INVALID;
    if (devTypeRec.deviceTypeName.length() == 0)
        return AddResult::Invalid;

    // Lock
    if (!RaftMutex_lock(_extRecordsMutex, RAFT_MUTEX_WAIT_FOREVER))
        return AddResult::Full;

    // A record with this key already added: the same content is the same record; different content is
    // rejected, since an index must always mean one record and a key one record (D17)
    AddResult result = AddResult::Added;
    int32_t existingPos = findExtendedPosByKey(devTypeRec.deviceTypeName.c_str());
    if (existingPos >= 0)
    {
        if (DeviceTypeRecordPacked::contentMatches(*_extRecords[existingPos], devTypeRec))
        {
            result = AddResult::AlreadyPresent;
            deviceTypeIndex = BASE_DEV_TYPE_ARRAY_SIZE + existingPos;
        }
        else
        {
            result = AddResult::Conflict;
        }
    }
    else
    {
        // Limits (the pointer array is allocated when the first record is added, so the limits are fixed then)
        if (!_extRecords)
        {
            applyDefaultExtendedRecordLimits();
            _extRecords = new (std::nothrow) const DeviceTypeRecord*[_extMaxRecords];
        }
        uint32_t count = _extCount.load(std::memory_order_relaxed);
        uint32_t recBytes = DeviceTypeRecordPacked::sizeFor(devTypeRec);
        if (!_extRecords || (count >= _extMaxRecords) || (_extBytesUsed + recBytes > _extMaxBytes) ||
                    (BASE_DEV_TYPE_ARRAY_SIZE + count >= DEVICE_TYPE_INDEX_INVALID))
        {
            result = AddResult::Full;
        }
        else
        {
            const DeviceTypeRecord* pRec = DeviceTypeRecordPacked::create(devTypeRec);
            if (!pRec)
            {
                result = AddResult::Full;
            }
            else
            {
                // Store the record, keep the key index sorted, then publish the new count
                _extRecords[count] = pRec;
                _extBytesUsed += recBytes;
                auto insertIt = std::lower_bound(_extPosByKey.begin(), _extPosByKey.end(), pRec->deviceType,
                        [this](uint16_t pos, const char* key) { return strcmp(_extRecords[pos]->deviceType, key) < 0; });
                _extPosByKey.insert(insertIt, (uint16_t)count);
                _extCount.store(count + 1, std::memory_order_release);
                _generation.fetch_add(1, std::memory_order_acq_rel);
                deviceTypeIndex = BASE_DEV_TYPE_ARRAY_SIZE + count;
            }
        }
    }

    // Unlock
    RaftMutex_unlock(_extRecordsMutex);

#ifdef DEBUG_ADD_EXTENDED_DEVICE_TYPE_RECORD
    LOG_I(MODULE_PREFIX, "addRecord %s %s devTypeIdx %d addrs %s detVals %s initVals %s pollInfo %s",
                getAddResultStr(result), devTypeRec.deviceTypeName.c_str(), deviceTypeIndex,
                devTypeRec.addresses.c_str(), devTypeRec.detectionValues.c_str(),
                devTypeRec.initValues.c_str(), devTypeRec.pollInfo.c_str());
#endif
    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Add extended device type records
/// @param devTypeRec device type record
/// @param deviceTypeIndex (out) device type index
/// @return true if added
bool DeviceTypeRecords::addExtendedDeviceTypeRecord(const DeviceTypeRecordDynamic& devTypeRec, DeviceTypeIndexType& deviceTypeIndex)
{
    return addRecord(devTypeRec, deviceTypeIndex) == AddResult::Added;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get add result as a string
const char* DeviceTypeRecords::getAddResultStr(AddResult result)
{
    switch (result)
    {
        case AddResult::Added: return "Added";
        case AddResult::AlreadyPresent: return "AlreadyPresent";
        case AddResult::Conflict: return "Conflict";
        case AddResult::Full: return "Full";
        case AddResult::Invalid: return "Invalid";
    }
    return "Unknown";
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set the limits for extended records (only before the first extended record is added)
/// @param maxRecords maximum number of extended records (0 for the default)
/// @param maxBytes maximum bytes used by extended records (0 for the default)
/// @return true if set
bool DeviceTypeRecords::setExtendedRecordLimits(uint32_t maxRecords, uint32_t maxBytes)
{
    if (!RaftMutex_lock(_extRecordsMutex, RAFT_MUTEX_WAIT_FOREVER))
        return false;
    bool isSet = false;
    if (!_extRecords)
    {
        applyDefaultExtendedRecordLimits();
        if (maxRecords > 0)
            _extMaxRecords = maxRecords;
        if (maxBytes > 0)
            _extMaxBytes = maxBytes;
        isSet = true;
    }
    RaftMutex_unlock(_extRecordsMutex);
    return isSet;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get extended record usage
DeviceTypeRecords::ExtendedRecordStats DeviceTypeRecords::getExtendedRecordStats() const
{
    ExtendedRecordStats stats;
    if (!RaftMutex_lock(_extRecordsMutex, RAFT_MUTEX_WAIT_FOREVER))
        return stats;
    stats.count = _extCount.load(std::memory_order_relaxed);
    stats.bytesUsed = _extBytesUsed;
    if (_extMaxRecords == 0)
    {
        // Limits not applied yet - report the defaults that will apply
        const_cast<DeviceTypeRecords*>(this)->applyDefaultExtendedRecordLimits();
    }
    stats.maxRecords = _extMaxRecords;
    stats.maxBytes = _extMaxBytes;
    RaftMutex_unlock(_extRecordsMutex);
    return stats;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Apply the default limits for extended records if not already set (D3). Assumes the mutex is held
void DeviceTypeRecords::applyDefaultExtendedRecordLimits()
{
    if (_extMaxRecords != 0)
        return;
    uint32_t spiramSize = utilsGetSPIRAMSize();
    bool hasPSRAM = (spiramSize != 0) && (spiramSize != UINT32_MAX);
    _extMaxRecords = hasPSRAM ? EXT_MAX_RECORDS_DEFAULT_PSRAM : EXT_MAX_RECORDS_DEFAULT;
    _extMaxBytes = hasPSRAM ? EXT_MAX_BYTES_DEFAULT_PSRAM : EXT_MAX_BYTES_DEFAULT;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Find an extended record by key (binary search). Assumes the mutex is held
/// @return position in _extRecords, or -1
int32_t DeviceTypeRecords::findExtendedPosByKey(const char* deviceTypeKey) const
{
    auto it = std::lower_bound(_extPosByKey.begin(), _extPosByKey.end(), deviceTypeKey,
            [this](uint16_t pos, const char* key) { return strcmp(_extRecords[pos]->deviceType, key) < 0; });
    if ((it != _extPosByKey.end()) && (strcmp(_extRecords[*it]->deviceType, deviceTypeKey) == 0))
        return *it;
    return -1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get device poll decode function
/// @param deviceTypeIndex device type index
/// @return poll decode function
DeviceTypeRecordDecodeFn DeviceTypeRecords::getPollDecodeFn(DeviceTypeIndexType deviceTypeIndex) const
{
    const DeviceTypeRecord* pRec = getRecord(deviceTypeIndex);
    return pRec ? pRec->pollResultDecodeFn : nullptr;
}
