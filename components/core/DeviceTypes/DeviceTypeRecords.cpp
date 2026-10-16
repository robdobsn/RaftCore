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
    // Free extended records, base record overrides and retired overrides
    uint32_t count = _extCount.load(std::memory_order_acquire);
    for (uint32_t i = 0; i < count; i++)
        DeviceTypeRecordPacked::destroy(_extRecords[i]);
    delete[] _extRecords;
    if (_baseOverridesStore)
    {
        for (uint32_t i = 0; i < BASE_DEV_TYPE_ARRAY_SIZE; i++)
            DeviceTypeRecordPacked::destroy(_baseOverridesStore[i]);
        delete[] _baseOverridesStore;
    }
    for (const DeviceTypeRecord* pRec : _retiredRecords)
        DeviceTypeRecordPacked::destroy(pRec);

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
                [this](DeviceTypeIndexType idx) { return getRecord(idx); },
                [this](DeviceTypeIndexType idx) { return policyOf(idx); });
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
    // The identification candidates plus the records whose policy is DISABLED_CLAIM (they still claim the address)
    std::shared_ptr<const CatalogueIndex> pIndex = getCatalogueIndex();
    uint32_t count = 0;
    const DeviceTypeIndexType* pCands = pIndex->getIdentCandidates(addr, count);
    std::vector<DeviceTypeIndexType> known = pCands ? std::vector<DeviceTypeIndexType>(pCands, pCands + count) :
                std::vector<DeviceTypeIndexType>();
    pIndex->appendClaimOnly(addr, known);
    return known;
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
    {
        const DeviceTypeRecord* const* pOverrides = _baseOverrides.load(std::memory_order_acquire);
        if (pOverrides && pOverrides[deviceTypeIndex])
            return pOverrides[deviceTypeIndex];
        return &baseDevTypeRecords[deviceTypeIndex];
    }
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
            return getRecord(recIdx);
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
    if (!pDevTypeRec)
    {
        pollingInfo.clear();
        return;
    }
    DeviceTypeGrammar::Result compiledPoll = DeviceTypeGrammar::compile(RaftJson(pDevTypeRec->pollInfo).getString("c", "").c_str(),
                DeviceTypeGrammar::Field::Poll);
    pollInfoFromSteps(addr, pDevTypeRec, compiledPoll, pollingInfo);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get polling info for a device type index (from the compiled record)
void DeviceTypeRecords::getPollInfo(BusElemAddrType addr, DeviceTypeIndexType deviceTypeIndex, DevicePollingInfo& pollingInfo) const
{
    std::shared_ptr<const CompiledRecord> pCompiled = getCompiledRecord(deviceTypeIndex);
    if (!pCompiled)
    {
        pollingInfo.clear();
        return;
    }
    pollInfoFromSteps(addr, getRecord(deviceTypeIndex), pCompiled->poll, pollingInfo);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Build polling info from a record and its compiled poll string
void DeviceTypeRecords::pollInfoFromSteps(BusElemAddrType addr, const DeviceTypeRecord* pDevTypeRec,
            const DeviceTypeGrammar::Result& compiledPoll, DevicePollingInfo& pollingInfo)
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

    // Polling requests from the compiled poll string
    uint16_t pollResultDataSize = 0;
    for (const DeviceTypeGrammar::Step& step : compiledPoll.steps)
    {
        BusRequestInfo pollReq(BUS_REQ_TYPE_POLL,
                addr,
                DevicePollingInfo::DEV_IDENT_POLL_CMD_ID,
                step.write.size(),
                step.write.data(),
                step.readLen,
                step.pauseMs,
                NULL,
                NULL);
        if (step.readLenExpr)
            pollReq.setReadLenExpr(step.readLenExpr);
        pollingInfo.pollReqs.push_back(pollReq);
        pollResultDataSize += step.readLen;
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
/// @param addr address
/// @param pDevTypeRec device type record
/// @param initBusRequests (out) initialisation bus requests
void DeviceTypeRecords::getInitBusRequests(BusElemAddrType addr, const DeviceTypeRecord* pDevTypeRec, std::vector<BusRequestInfo>& initRequests)
{
    initRequests.clear();
    if (!pDevTypeRec)
        return;
    DeviceTypeGrammar::Result compiled = DeviceTypeGrammar::compile(pDevTypeRec->initValues, DeviceTypeGrammar::Field::Init);
    initRequestsFromSteps(addr, compiled, initRequests);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get initialisation bus requests for a device type index (from the compiled record)
void DeviceTypeRecords::getInitBusRequests(BusElemAddrType addr, DeviceTypeIndexType deviceTypeIndex, std::vector<BusRequestInfo>& initRequests)
{
    initRequests.clear();
    std::shared_ptr<const CompiledRecord> pCompiled = getCompiledRecord(deviceTypeIndex);
    if (pCompiled)
        initRequestsFromSteps(addr, pCompiled->init, initRequests);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Build initialisation bus requests from compiled steps
void DeviceTypeRecords::initRequestsFromSteps(BusElemAddrType addr, const DeviceTypeGrammar::Result& compiled,
            std::vector<BusRequestInfo>& initRequests)
{
    for (const DeviceTypeGrammar::Step& step : compiled.steps)
    {
        BusRequestInfo reqRec(BUS_REQ_TYPE_FAST_SCAN,
                    addr,
                    0,
                    step.write.size(),
                    step.write.data(),
                    step.readLen,
                    step.pauseMs,
                    nullptr,
                    this);
        initRequests.push_back(reqRec);
#ifdef DEBUG_DEVICE_INIT_REQS
        String writeDataStr;
        Raft::getHexStrFromBytes(step.write.data(), step.write.size(), writeDataStr);
        LOG_I(MODULE_PREFIX, "initRequestsFromSteps addr %04x writeData %s readDataSize %d pauseMs %d",
                    addr, writeDataStr.c_str(), step.readLen, step.pauseMs);
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
/// @brief Get detection records
/// @param deviceType device type
/// @param detectionRecs (out) detection records
/// @return false if any detection value is invalid (detectionRecs is then empty and the record must not match)
bool DeviceTypeRecords::getDetectionRecs(const DeviceTypeRecord* pDevTypeRec, std::vector<DeviceDetectionRec>& detectionRecs)
{
    detectionRecs.clear();
    if (!pDevTypeRec)
        return false;
    DeviceTypeGrammar::Result compiled = DeviceTypeGrammar::compile(pDevTypeRec->detectionValues, DeviceTypeGrammar::Field::Detection);
    if (compiled.hasErrors())
    {
        LOG_W(MODULE_PREFIX, "getDetectionRecs %s invalid detection value %s",
                    pDevTypeRec->deviceType ? pDevTypeRec->deviceType : "?",
                    pDevTypeRec->detectionValues ? pDevTypeRec->detectionValues : "");
        return false;
    }
    detectionRecsFromSteps(compiled, detectionRecs);
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get detection records for a device type index (from the compiled record)
bool DeviceTypeRecords::getDetectionRecs(DeviceTypeIndexType deviceTypeIndex, std::vector<DeviceDetectionRec>& detectionRecs) const
{
    detectionRecs.clear();
    std::shared_ptr<const CompiledRecord> pCompiled = getCompiledRecord(deviceTypeIndex);
    if (!pCompiled || pCompiled->detection.hasErrors())
        return false;
    detectionRecsFromSteps(pCompiled->detection, detectionRecs);
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Build detection records from compiled steps
void DeviceTypeRecords::detectionRecsFromSteps(const DeviceTypeGrammar::Result& compiled, std::vector<DeviceDetectionRec>& detectionRecs)
{
    detectionRecs.reserve(compiled.steps.size());
    for (const DeviceTypeGrammar::Step& step : compiled.steps)
    {
        DeviceDetectionRec detectionRec;
        detectionRec.writeData = step.write;
        for (const DeviceTypeGrammar::Check& check : step.checks)
            detectionRec.checkValues.push_back(std::make_pair(check.mask, check.value));
        detectionRec.pauseAfterSendMs = step.pauseMs;
        detectionRec.readLen = step.readLen;
        detectionRec.crc8w = step.crc8w;
        detectionRecs.push_back(detectionRec);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Compile a record's command strings and log any problems
/// @param pDevTypeRec record
/// @param logWarnings log warnings as well as errors
std::shared_ptr<DeviceTypeRecords::CompiledRecord> DeviceTypeRecords::compileRecord(const DeviceTypeRecord* pDevTypeRec, bool logWarnings)
{
    auto pCompiled = std::make_shared<CompiledRecord>();
    if (!pDevTypeRec)
        return pCompiled;
    String pollStr = RaftJson(pDevTypeRec->pollInfo).getString("c", "");
    pCompiled->detection = DeviceTypeGrammar::compile(pDevTypeRec->detectionValues, DeviceTypeGrammar::Field::Detection);
    pCompiled->init = DeviceTypeGrammar::compile(pDevTypeRec->initValues, DeviceTypeGrammar::Field::Init);
    pCompiled->poll = DeviceTypeGrammar::compile(pollStr.c_str(), DeviceTypeGrammar::Field::Poll);
    const DeviceTypeGrammar::Result* results[] = { &pCompiled->detection, &pCompiled->init, &pCompiled->poll };
    const DeviceTypeGrammar::Field fields[] = { DeviceTypeGrammar::Field::Detection, DeviceTypeGrammar::Field::Init,
                DeviceTypeGrammar::Field::Poll };
    for (int i = 0; i < 3; i++)
    {
        for (const DeviceTypeGrammar::Issue& issue : results[i]->issues)
        {
            if (issue.isError())
            {
                LOG_E(MODULE_PREFIX, "compileRecord %s %s: %s at %d", pDevTypeRec->deviceType ? pDevTypeRec->deviceType : "?",
                            DeviceTypeGrammar::getFieldName(fields[i]), DeviceTypeGrammar::getIssueName(issue.code), issue.pos);
            }
            else if (logWarnings)
            {
                LOG_W(MODULE_PREFIX, "compileRecord %s %s: %s at %d", pDevTypeRec->deviceType ? pDevTypeRec->deviceType : "?",
                            DeviceTypeGrammar::getFieldName(fields[i]), DeviceTypeGrammar::getIssueName(issue.code), issue.pos);
            }
        }
    }
    return pCompiled;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get a record's compiled command strings (compiled when first asked for, then kept)
std::shared_ptr<const DeviceTypeRecords::CompiledRecord> DeviceTypeRecords::getCompiledRecord(DeviceTypeIndexType deviceTypeIndex) const
{
    const DeviceTypeRecord* pRec = getRecord(deviceTypeIndex);
    if (!pRec)
        return nullptr;
    if (!RaftMutex_lock(_compiledMutex, RAFT_MUTEX_WAIT_FOREVER))
        return compileRecord(pRec, false);
    if (_compiledRecords.size() <= deviceTypeIndex)
        _compiledRecords.resize(deviceTypeIndex + 1);
    std::shared_ptr<const CompiledRecord> pCompiled = _compiledRecords[deviceTypeIndex];
    if (!pCompiled)
    {
        // Base records' warnings are reported when the firmware is built, so only their errors are logged here
        pCompiled = compileRecord(pRec, deviceTypeIndex >= BASE_DEV_TYPE_ARRAY_SIZE);
        _compiledRecords[deviceTypeIndex] = pCompiled;
    }
    RaftMutex_unlock(_compiledMutex);
    return pCompiled;
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
    const char* pKey = devTypeRec.deviceTypeName.c_str();
    int32_t existingPos = findExtendedPosByKey(pKey);
    int32_t baseIdx = (existingPos < 0) ? findBaseIdxByKey(pKey) : -1;
    const DeviceTypeRecord* pBaseOverride = nullptr;
    if (baseIdx >= 0)
    {
        const DeviceTypeRecord* const* pOverrides = _baseOverrides.load(std::memory_order_acquire);
        pBaseOverride = pOverrides ? pOverrides[baseIdx] : nullptr;
    }
    if ((baseIdx >= 0) && pBaseOverride && DeviceTypeRecordPacked::contentMatches(*pBaseOverride, devTypeRec))
    {
        // The same override again
        result = AddResult::AlreadyPresent;
        deviceTypeIndex = baseIdx;
    }
    else if (((existingPos >= 0) || (baseIdx >= 0)) && isProtectedLocked(pKey) &&
                !((existingPos >= 0) && DeviceTypeRecordPacked::contentMatches(*_extRecords[existingPos], devTypeRec)))
    {
        // A protected type can't be replaced
        result = AddResult::Protected;
    }
    else if ((baseIdx >= 0) && pBaseOverride)
    {
        // A base record can be overridden once - a second, different, override would change what its index means
        result = AddResult::Conflict;
    }
    else if ((baseIdx >= 0) && !_ready)
    {
        // Override a base record before Ready: it keeps the base record's index (D2)
        result = replaceBaseRecord(baseIdx, devTypeRec);
        if (result == AddResult::Replaced)
            deviceTypeIndex = baseIdx;
    }
    else if (existingPos >= 0)
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

    // Compile the new record's command strings now, so any problems are reported when it is added
    if ((result == AddResult::Added) || (result == AddResult::Replaced))
        getCompiledRecord(deviceTypeIndex);

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
    AddResult result = addRecord(devTypeRec, deviceTypeIndex);
    return (result == AddResult::Added) || (result == AddResult::Replaced);
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
        case AddResult::Replaced: return "Replaced";
        case AddResult::Protected: return "Protected";
    }
    return "Unknown";
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Replace a base record with an extended record at its index (before Ready). Assumes the mutex is held
/// @note the record's bytes count against the extended records' byte limit. If the record has no decode function
///       and its response layout (devInfoJson "resp") is the same as the base record's, it uses the base
///       record's decode function and field descriptions
DeviceTypeRecords::AddResult DeviceTypeRecords::replaceBaseRecord(DeviceTypeIndexType baseIdx, const DeviceTypeRecordDynamic& devTypeRec)
{
    applyDefaultExtendedRecordLimits();
    uint32_t recBytes = DeviceTypeRecordPacked::sizeFor(devTypeRec);
    if (_extBytesUsed + recBytes > _extMaxBytes)
        return AddResult::Full;
    if (!_baseOverridesStore)
    {
        _baseOverridesStore = new (std::nothrow) const DeviceTypeRecord*[BASE_DEV_TYPE_ARRAY_SIZE]();
        if (!_baseOverridesStore)
            return AddResult::Full;
        _baseOverrides.store(_baseOverridesStore, std::memory_order_release);
    }
    DeviceTypeRecord* pRec = const_cast<DeviceTypeRecord*>(DeviceTypeRecordPacked::create(devTypeRec));
    if (!pRec)
        return AddResult::Full;

    // Inherit the decoder when the response layout is unchanged (review 9.2-G)
    const DeviceTypeRecord& baseRec = baseDevTypeRecords[baseIdx];
    if (!pRec->pollResultDecodeFn && sameResponseLayout(baseRec.devInfoJson, pRec->devInfoJson))
    {
        pRec->pollResultDecodeFn = baseRec.pollResultDecodeFn;
        pRec->pollFieldDescs = baseRec.pollFieldDescs;
        pRec->pollFieldCount = baseRec.pollFieldCount;
        pRec->pollStructSize = baseRec.pollStructSize;
    }

    // Publish the override, then forget anything derived from the base record at this index
    _extBytesUsed += recBytes;
    _baseOverridesStore[baseIdx] = pRec;
    std::atomic_thread_fence(std::memory_order_release);
    if (RaftMutex_lock(_compiledMutex, RAFT_MUTEX_WAIT_FOREVER))
    {
        if (baseIdx < _compiledRecords.size())
            _compiledRecords[baseIdx].reset();
        RaftMutex_unlock(_compiledMutex);
    }
    _generation.fetch_add(1, std::memory_order_acq_rel);
    return AddResult::Replaced;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check whether two devInfoJson strings have the same response layout ("resp"), ignoring whitespace
///        outside strings
bool DeviceTypeRecords::sameResponseLayout(const char* pDevInfoA, const char* pDevInfoB)
{
    if (!pDevInfoA || !pDevInfoB)
        return false;
    auto compact = [](const String& json) {
        std::string out;
        bool inString = false;
        for (uint32_t i = 0; i < json.length(); i++)
        {
            char c = json[i];
            if (c == '"' && ((i == 0) || (json[i - 1] != '\\')))
                inString = !inString;
            if (inString || !isspace((unsigned char)c))
                out += c;
        }
        return out;
    };
    String respA = RaftJson(pDevInfoA).getString("resp", "");
    String respB = RaftJson(pDevInfoB).getString("resp", "");
    return (respA.length() > 0) && (compact(respA) == compact(respB));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Find a base record by key (binary search)
/// @return its index, or -1
int32_t DeviceTypeRecords::findBaseIdxByKey(const char* deviceTypeKey)
{
    int32_t lo = 0;
    int32_t hi = (int32_t)BASE_DEV_TYPE_ARRAY_SIZE - 1;
    while (lo <= hi)
    {
        int32_t mid = (lo + hi) / 2;
        uint16_t recIdx = baseDevTypeIndexByKey[mid];
        int cmp = strcmp(baseDevTypeRecords[recIdx].deviceType, deviceTypeKey);
        if (cmp == 0)
            return recIdx;
        if (cmp < 0)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return -1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set a record's policy
DeviceTypeRecords::PolicyResult DeviceTypeRecords::setPolicy(DeviceTypeIndexType deviceTypeIndex, uint8_t policy)
{
    const DeviceTypeRecord* pRec = getRecord(deviceTypeIndex);
    if (!pRec || (policy > DeviceTypeRecord::POLICY_DISABLED_CLAIM))
        return PolicyResult::NotFound;
    if (!RaftMutex_lock(_extRecordsMutex, RAFT_MUTEX_WAIT_FOREVER))
        return PolicyResult::NotFound;
    PolicyResult result = PolicyResult::Set;
    if ((policy != DeviceTypeRecord::POLICY_ENABLED) && isProtectedLocked(pRec->deviceType))
    {
        result = PolicyResult::Protected;
    }
    else if (policyOf(deviceTypeIndex) != policy)
    {
        if (_policies.size() <= deviceTypeIndex)
            _policies.resize(deviceTypeIndex + 1, POLICY_UNSET);
        _policies[deviceTypeIndex] = policy;
        _generation.fetch_add(1, std::memory_order_acq_rel);
    }
    RaftMutex_unlock(_extRecordsMutex);
    return result;
}

DeviceTypeRecords::PolicyResult DeviceTypeRecords::setPolicy(const char* deviceTypeKey, uint8_t policy)
{
    DeviceTypeIndexType deviceTypeIndex = DEVICE_TYPE_INDEX_INVALID;
    if (!getRecord(deviceTypeKey, deviceTypeIndex))
        return PolicyResult::NotFound;
    return setPolicy(deviceTypeIndex, policy);
}

const char* DeviceTypeRecords::getPolicyResultStr(PolicyResult result)
{
    switch (result)
    {
        case PolicyResult::Set: return "Set";
        case PolicyResult::NotFound: return "NotFound";
        case PolicyResult::Protected: return "Protected";
    }
    return "Unknown";
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get a record's policy
uint8_t DeviceTypeRecords::getPolicy(DeviceTypeIndexType deviceTypeIndex) const
{
    if (!RaftMutex_lock(_extRecordsMutex, RAFT_MUTEX_WAIT_FOREVER))
        return DeviceTypeRecord::POLICY_ENABLED;
    uint8_t policy = policyOf(deviceTypeIndex);
    RaftMutex_unlock(_extRecordsMutex);
    return policy;
}

/// @brief A record's policy (the one set at runtime, else its initial policy). Assumes the mutex is held
uint8_t DeviceTypeRecords::policyOf(DeviceTypeIndexType deviceTypeIndex) const
{
    if ((deviceTypeIndex < _policies.size()) && (_policies[deviceTypeIndex] != POLICY_UNSET))
        return _policies[deviceTypeIndex];
    const DeviceTypeRecord* pRec = getRecord(deviceTypeIndex);
    return pRec ? pRec->initialPolicy : DeviceTypeRecord::POLICY_ENABLED;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Protect a device type key
void DeviceTypeRecords::setProtected(const char* deviceTypeKey)
{
    if (!deviceTypeKey || !deviceTypeKey[0] || !RaftMutex_lock(_extRecordsMutex, RAFT_MUTEX_WAIT_FOREVER))
        return;
    if (!isProtectedLocked(deviceTypeKey))
        _protectedKeys.push_back(deviceTypeKey);
    RaftMutex_unlock(_extRecordsMutex);
}

bool DeviceTypeRecords::isProtected(const char* deviceTypeKey) const
{
    if (!RaftMutex_lock(_extRecordsMutex, RAFT_MUTEX_WAIT_FOREVER))
        return false;
    bool isProt = isProtectedLocked(deviceTypeKey);
    RaftMutex_unlock(_extRecordsMutex);
    return isProt;
}

/// @brief Check whether a key is protected. Assumes the mutex is held
bool DeviceTypeRecords::isProtectedLocked(const char* deviceTypeKey) const
{
    if (!deviceTypeKey)
        return false;
    for (const std::string& key : _protectedKeys)
        if (key == deviceTypeKey)
            return true;
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Where a record comes from
DeviceTypeRecords::RecordSource DeviceTypeRecords::getRecordSource(DeviceTypeIndexType deviceTypeIndex) const
{
    if (deviceTypeIndex < BASE_DEV_TYPE_ARRAY_SIZE)
    {
        const DeviceTypeRecord* const* pOverrides = _baseOverrides.load(std::memory_order_acquire);
        return (pOverrides && pOverrides[deviceTypeIndex]) ? RecordSource::Override : RecordSource::Base;
    }
    return getRecord(deviceTypeIndex) ? RecordSource::Extended : RecordSource::None;
}

const char* DeviceTypeRecords::getRecordSourceStr(RecordSource source)
{
    switch (source)
    {
        case RecordSource::None: return "none";
        case RecordSource::Base: return "base";
        case RecordSource::Override: return "override";
        case RecordSource::Extended: return "extended";
    }
    return "none";
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check whether the identification hook must not be run at an address
bool DeviceTypeRecords::isAddrHookBlocked(BusElemAddrType addr) const
{
    return getCatalogueIndex()->isAddrHookBlocked(addr);
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
