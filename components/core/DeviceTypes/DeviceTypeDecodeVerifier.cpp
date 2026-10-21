/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// DeviceTypeDecodeVerifier.cpp
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef RAFT_RUNTIME_DECODE_VERIFY

#ifndef RAFT_RUNTIME_DECODE
#error "RAFT_RUNTIME_DECODE_VERIFY needs RAFT_RUNTIME_DECODE"
#endif

#include <cstdio>
#include <cstring>
#include <algorithm>
#include "DeviceTypeDecodeVerifier.h"
#include "DeviceTypeRuntimeDecoder.h"
#include "DeviceTypeCodeInterpreter.h"
#include "DeviceTypeRecords.h"
#include "DevicePollingInfo.h"
#include "RaftJson.h"
#include "RaftUtils.h"
#include "Logger.h"
#ifdef ESP_PLATFORM
#include "esp_idf_version.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

static const char* MODULE_PREFIX = "DecodeVerify";

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Bytes of stack free at this point in the calling task (UINT32_MAX if not known)
static uint32_t stackFreeNow()
{
#ifdef ESP_PLATFORM
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    const uint8_t* pStart = (const uint8_t*)xTaskGetStackStart(nullptr);
#else
    const uint8_t* pStart = (const uint8_t*)pxTaskGetStackStart(nullptr);
#endif
    // The stack grows down from its end to its start
    const uint8_t* pHere = (const uint8_t*)__builtin_frame_address(0);
    return (pStart && (pHere > pStart)) ? (uint32_t)(pHere - pStart) : UINT32_MAX;
#else
    return UINT32_MAX;
#endif
}

DeviceTypeDecodeVerifier deviceTypeDecodeVerifier;

DeviceTypeDecodeVerifier::DeviceTypeDecodeVerifier()
{
    RaftMutex_init(_mutex);
}

DeviceTypeDecodeVerifier::~DeviceTypeDecodeVerifier()
{
    for (TypeStats& ts : _types)
    {
        delete ts.pDecoder;
        delete ts.pInit;
    }
    RaftMutex_destroy(_mutex);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief A type's results (added if new). Assumes the mutex is held
DeviceTypeDecodeVerifier::TypeStats& DeviceTypeDecodeVerifier::getTypeStats(uint16_t deviceTypeIndex, const DeviceTypeRecord* pRec)
{
    for (TypeStats& ts : _types)
        if (ts.deviceTypeIndex == deviceTypeIndex)
            return ts;
    _types.emplace_back();
    TypeStats& ts = _types.back();
    ts.deviceTypeIndex = deviceTypeIndex;
    ts.typeKey = (pRec && pRec->deviceType) ? pRec->deviceType : "?";
    return ts;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Note the stack left in this task. Assumes the mutex is held
void DeviceTypeDecodeVerifier::noteStack()
{
#ifdef ESP_PLATFORM
    uint32_t freeBytes = uxTaskGetStackHighWaterMark(nullptr);
    if (freeBytes < _stackMinFree)
    {
        _stackMinFree = freeBytes;
        const char* pName = pcTaskGetName(nullptr);
        _stackMinTask = pName ? pName : "?";
    }
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Note the stack free where the verifier is called. Assumes the mutex is held
void DeviceTypeDecodeVerifier::noteStackAtCall(uint32_t freeBytes)
{
#ifdef ESP_PLATFORM
    if (freeBytes < _stackAtCallMinFree)
    {
        _stackAtCallMinFree = freeBytes;
        const char* pName = pcTaskGetName(nullptr);
        _stackAtCallTask = pName ? pName : "?";
    }
#else
    (void)freeBytes;
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check a poll result
void DeviceTypeDecodeVerifier::checkDecode(RaftDeviceID deviceID, uint16_t deviceTypeIndex, const DeviceTypeRecord* pRec,
            bool isCompiled, const std::vector<uint8_t>& data)
{
    if (!pRec || !pRec->pollResultDecodeFn || (pRec->pollStructSize == 0))
        return;
    uint32_t stackFree = stackFreeNow();
    if (!RaftMutex_lock(_mutex, RAFT_MUTEX_WAIT_FOREVER))
        return;
    TypeStats& ts = getTypeStats(deviceTypeIndex, pRec);
    noteStackAtCall(stackFree);
    ts.notCompiled = !isCompiled;
    if (!isCompiled)
    {
        ts.polls++;
        RaftMutex_unlock(_mutex);
        return;
    }

    // Not where too little stack is left for the decodes (counted)
    if (stackFree < STACK_NEEDED_BYTES)
    {
        ts.skippedLowStack++;
        RaftMutex_unlock(_mutex);
        return;
    }

    // A runtime decoder beside this compiled decoder (again if the record has changed): made in loop(), as making
    // one (the JSON and the code compiler) needs more stack than decoding
    if (ts.compiledFn != pRec->pollResultDecodeFn)
    {
        if (!ts.pPendingRec || (ts.pPendingRec->pollResultDecodeFn != pRec->pollResultDecodeFn))
        {
            delete ts.pDecoder;
            ts.pDecoder = nullptr;
            ts.problem = "";
            ts.pPendingRec = pRec;
        }
        ts.notReady++;
        RaftMutex_unlock(_mutex);
        return;
    }
    ts.polls++;
    if (!ts.pDecoder)
    {
        RaftMutex_unlock(_mutex);
        return;
    }

    // The device's decode states
    DeviceState* pState = nullptr;
    for (DeviceState& ds : _devices)
        if ((ds.deviceID == deviceID) && (ds.deviceTypeIndex == deviceTypeIndex))
            pState = &ds;
    if (!pState)
    {
        _devices.erase(std::remove_if(_devices.begin(), _devices.end(),
                    [deviceID](const DeviceState& ds) { return ds.deviceID == deviceID; }), _devices.end());
        _devices.emplace_back();
        pState = &_devices.back();
        pState->deviceID = deviceID;
        pState->deviceTypeIndex = deviceTypeIndex;
    }

    // Padded to a whole record, as a subscriber's decode does (a FIFO read may be shorter)
    std::vector<uint8_t> buf = data;
    uint32_t recordBytes = pRec->pollDataSizeBytes + DevicePollingInfo::POLL_RESULT_TIMESTAMP_SIZE;
    if (buf.size() < recordBytes)
        buf.resize(recordBytes, 0);

    // Decode both ways into zeroed buffers
    uint32_t structSize = pRec->pollStructSize;
    _outCompiled.assign(structSize * MAX_RECS_PER_DECODE, 0);
    _outRuntime.assign(structSize * MAX_RECS_PER_DECODE, 0);
    uint64_t t0 = micros();
    uint32_t nCompiled = pRec->pollResultDecodeFn(buf.data(), buf.size(), _outCompiled.data(), _outCompiled.size(),
                MAX_RECS_PER_DECODE, pState->compiled);
    uint64_t t1 = micros();
    uint64_t nodesBefore = ts.pDecoder->getNodesRun();
    uint32_t nRuntime = ts.pDecoder->decode(buf.data(), buf.size(), _outRuntime.data(), _outRuntime.size(),
                MAX_RECS_PER_DECODE, pState->runtime);
    uint64_t t2 = micros();

    // Again straight away (warm caches), into scratch copies, for timing only
    {
        std::vector<uint8_t> scratch(_outCompiled.size());
        RaftBusDeviceDecodeState scratchState = pState->compiled;
        uint64_t w0 = micros();
        pRec->pollResultDecodeFn(buf.data(), buf.size(), scratch.data(), scratch.size(), MAX_RECS_PER_DECODE, scratchState);
        uint64_t w1 = micros();
        scratchState = pState->runtime;
        uint64_t nodesWarm = ts.pDecoder->getNodesRun();
        ts.pDecoder->decode(buf.data(), buf.size(), scratch.data(), scratch.size(), MAX_RECS_PER_DECODE, scratchState);
        uint64_t w2 = micros();
        ts.warmNodesSkip += ts.pDecoder->getNodesRun() - nodesWarm;
        ts.compiledWarmUsTotal += (uint32_t)(w1 - w0);
        ts.runtimeWarmUsTotal += (uint32_t)(w2 - w1);
    }

    // Compare
    bool same = (nCompiled == nRuntime) &&
                (pState->compiled.lastReportTimestampUs == pState->runtime.lastReportTimestampUs) &&
                (pState->compiled.reportTimestampOffsetUs == pState->runtime.reportTimestampOffsetUs) &&
                (memcmp(_outCompiled.data(), _outRuntime.data(), structSize * std::min(nCompiled, MAX_RECS_PER_DECODE)) == 0);
    ts.records += nCompiled;
    ts.nodes += ts.pDecoder->getNodesRun() - nodesBefore - ts.warmNodesSkip;
    ts.warmNodesSkip = 0;
    uint32_t compiledUs = (uint32_t)(t1 - t0);
    uint32_t runtimeUs = (uint32_t)(t2 - t1);
    ts.compiledUsTotal += compiledUs;
    ts.runtimeUsTotal += runtimeUs;
    ts.compiledUsMax = std::max(ts.compiledUsMax, compiledUs);
    ts.runtimeUsMax = std::max(ts.runtimeUsMax, runtimeUs);
    if (!same)
    {
        ts.mismatches++;
        if (ts.firstMismatch.length() == 0)
        {
            ts.firstMismatch = describeMismatch(pRec, buf, nCompiled, nRuntime, _outCompiled.data(), _outRuntime.data(),
                        structSize, pState->compiled, pState->runtime);
            LOG_W(MODULE_PREFIX, "%s %s decode differs: %s", ts.typeKey.c_str(), deviceID.toString().c_str(),
                        ts.firstMismatch.c_str());
        }
        // The next comparison starts from the same state
        pState->runtime = pState->compiled;
    }
    noteStack();
    RaftMutex_unlock(_mutex);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Make the runtime decoders and init functions to compare (the first time: init functions for every compiled
///        record with init code, used when a device is identified; then a decoder for each type with poll results)
void DeviceTypeDecodeVerifier::loop(const DeviceTypeRecords& table)
{
    // Init functions
    if (!_initsMade)
    {
        _initsMade = true;
        for (uint32_t idx = 0; idx < DeviceTypeRecords::getNumBaseRecords(); idx++)
        {
            const char* pInitCode = table.getBaseInitCode(idx);
            const DeviceTypeRecord* pRec = table.getRecord(idx);
            if (!pInitCode || !pRec)
                continue;
            String problem;
            DeviceTypeRuntimeInit* pInit = DeviceTypeRuntimeInit::create(pInitCode, pRec->initValues ? pRec->initValues : "",
                        problem, false);
            if (pInit && (strcmp(pInit->getNamesStr(), pRec->initValueNames ? pRec->initValueNames : "") != 0))
            {
                problem = String("value names differ: ") + pInit->getNamesStr();
                delete pInit;
                pInit = nullptr;
            }
            if (!pInit)
                LOG_W(MODULE_PREFIX, "%s init not verified: %s", pRec->deviceType, problem.c_str());
            if (!RaftMutex_lock(_mutex, RAFT_MUTEX_WAIT_FOREVER))
            {
                delete pInit;
                continue;
            }
            TypeStats& ts = getTypeStats(idx, pRec);
            delete ts.pInit;
            ts.pInit = pInit;
            ts.initProblem = problem;
            RaftMutex_unlock(_mutex);
        }
    }

    // Decoders: one at a time
    if (!RaftMutex_lock(_mutex, RAFT_MUTEX_WAIT_FOREVER))
        return;
    const DeviceTypeRecord* pRec = nullptr;
    uint16_t deviceTypeIndex = 0;
    for (TypeStats& ts : _types)
    {
        if (ts.pPendingRec)
        {
            pRec = ts.pPendingRec;
            deviceTypeIndex = ts.deviceTypeIndex;
            break;
        }
    }
    RaftMutex_unlock(_mutex);
    if (!pRec)
        return;
    String problem;
    DeviceTypeRuntimeDecoder* pDecoder = DeviceTypeRuntimeDecoder::create(pRec->devInfoJson, problem, false);

    // The same struct layout and field descriptions
    if (pDecoder)
    {
        bool layoutSame = (pDecoder->getStructSize() == pRec->pollStructSize) && (pDecoder->getFieldCount() == pRec->pollFieldCount);
        for (uint32_t i = 0; layoutSame && (i < pRec->pollFieldCount); i++)
        {
            const AttrFieldDesc& a = pRec->pollFieldDescs[i];
            const AttrFieldDesc& b = pDecoder->getFieldDescs()[i];
            layoutSame = (strcmp(a.name, b.name) == 0) && (a.offset == b.offset) && (a.type == b.type) &&
                        (strcmp(a.fmtStr ? a.fmtStr : "", b.fmtStr ? b.fmtStr : "") == 0) &&
                        (a.divisor == b.divisor) && (a.addend == b.addend) && (a.count == b.count);
            if (!layoutSame)
                problem = String("layout differs at field ") + a.name;
        }
        if (!layoutSame)
        {
            if (problem.length() == 0)
                problem = "struct size or field count differs";
            delete pDecoder;
            pDecoder = nullptr;
        }
    }
    if (!pDecoder)
        LOG_W(MODULE_PREFIX, "%s not verified: %s", pRec->deviceType ? pRec->deviceType : "?", problem.c_str());

    // Use it (unless the record changed meanwhile - it is made again)
    if (!RaftMutex_lock(_mutex, RAFT_MUTEX_WAIT_FOREVER))
    {
        delete pDecoder;
        return;
    }
    TypeStats& ts = getTypeStats(deviceTypeIndex, pRec);
    if (ts.pPendingRec == pRec)
    {
        delete ts.pDecoder;
        ts.pDecoder = pDecoder;
        ts.problem = problem;
        ts.compiledFn = pRec->pollResultDecodeFn;
        ts.pPendingRec = nullptr;
        _devices.erase(std::remove_if(_devices.begin(), _devices.end(),
                    [deviceTypeIndex](const DeviceState& ds) { return ds.deviceTypeIndex == deviceTypeIndex; }), _devices.end());
    }
    else
    {
        delete pDecoder;
    }
    RaftMutex_unlock(_mutex);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Describe a decode difference (JSON object): the poll data, the record counts, and the first record and
///        field that differ, with both decoders' bytes
String DeviceTypeDecodeVerifier::describeMismatch(const DeviceTypeRecord* pRec, const std::vector<uint8_t>& data,
            uint32_t nCompiled, uint32_t nRuntime, const uint8_t* pCompiled, const uint8_t* pRuntime, uint32_t structSize,
            const RaftBusDeviceDecodeState& stateCompiled, const RaftBusDeviceDecodeState& stateRuntime)
{
    String json = "{";
    RaftJson::appendStringField(json, "poll", Raft::getHexStr(data.data(), std::min((uint32_t)data.size(), (uint32_t)256)).c_str());
    json += ",\"recs\":[" + String(nCompiled) + "," + String(nRuntime) + "]";
    if ((stateCompiled.lastReportTimestampUs != stateRuntime.lastReportTimestampUs) ||
            (stateCompiled.reportTimestampOffsetUs != stateRuntime.reportTimestampOffsetUs))
    {
        char tsStr[100];
        snprintf(tsStr, sizeof(tsStr), ",\"tsState\":[%llu,%llu,%llu,%llu]",
                    (unsigned long long)stateCompiled.lastReportTimestampUs, (unsigned long long)stateRuntime.lastReportTimestampUs,
                    (unsigned long long)stateCompiled.reportTimestampOffsetUs, (unsigned long long)stateRuntime.reportTimestampOffsetUs);
        json += tsStr;
    }

    // The first byte that differs
    uint32_t numRecs = std::min(std::min(nCompiled, nRuntime), MAX_RECS_PER_DECODE);
    for (uint32_t pos = 0; pos < numRecs * structSize; pos++)
    {
        if (pCompiled[pos] == pRuntime[pos])
            continue;
        uint32_t rec = pos / structSize;
        uint32_t offset = pos % structSize;

        // The field it is in (the timestamp comes before the first field)
        const char* pFieldName = "timeMs";
        uint32_t fieldStart = 0;
        uint32_t fieldEnd = (pRec->pollFieldCount > 0) ? pRec->pollFieldDescs[0].offset : structSize;
        for (uint32_t i = 0; i < pRec->pollFieldCount; i++)
        {
            uint32_t start = pRec->pollFieldDescs[i].offset;
            uint32_t end = (i + 1 < pRec->pollFieldCount) ? pRec->pollFieldDescs[i + 1].offset : structSize;
            if ((offset >= start) && (offset < end))
            {
                pFieldName = pRec->pollFieldDescs[i].name;
                fieldStart = start;
                fieldEnd = end;
            }
        }
        uint32_t len = std::min(fieldEnd - fieldStart, (uint32_t)32);
        json += ",\"rec\":" + String(rec);
        RaftJson::appendStringField(json, "field", pFieldName);
        RaftJson::appendStringField(json, "compiled",
                    Raft::getHexStr(pCompiled + rec * structSize + fieldStart, len).c_str());
        RaftJson::appendStringField(json, "runtime",
                    Raft::getHexStr(pRuntime + rec * structSize + fieldStart, len).c_str());
        break;
    }
    json += "}";
    return json;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check init values
void DeviceTypeDecodeVerifier::checkInit(uint16_t deviceTypeIndex, const DeviceTypeRecord* pRec, const char* pInitCode,
            const std::vector<uint8_t>& readData, bool compiledOk, const std::vector<int32_t>& compiledValues)
{
#ifdef RAFT_RUNTIME_DECODE_CODE
    if (!pRec || !pInitCode)
        return;
    uint32_t stackFree = stackFreeNow();
    if (!RaftMutex_lock(_mutex, RAFT_MUTEX_WAIT_FOREVER))
        return;
    (void)pInitCode;
    TypeStats& ts = getTypeStats(deviceTypeIndex, pRec);
    noteStackAtCall(stackFree);
    if (stackFree < STACK_NEEDED_BYTES)
    {
        ts.initSkippedLowStack++;
        RaftMutex_unlock(_mutex);
        return;
    }
    // The init function is made in loop()
    if (!ts.pInit && (ts.initProblem.length() == 0))
        ts.initNotReady++;
    if (ts.pInit)
    {
        std::vector<int32_t> values(compiledValues.size(), 0);
        bool runtimeOk = ts.pInit->run(readData.data(), readData.size(), values.data(), values.size());
        ts.initRuns++;
        bool same = (runtimeOk == compiledOk) && (!compiledOk || (values == compiledValues));
        if (!same)
        {
            ts.initMismatches++;
            if (ts.initFirstMismatch.length() == 0)
            {
                String compiledStr, runtimeStr;
                for (size_t i = 0; i < values.size(); i++)
                {
                    compiledStr += String(i ? "," : "") + String(compiledValues[i]);
                    runtimeStr += String(i ? "," : "") + String(values[i]);
                }
                ts.initFirstMismatch = "{";
                RaftJson::appendStringField(ts.initFirstMismatch, "read", Raft::getHexStr(readData).c_str());
                ts.initFirstMismatch += ",\"ok\":[" + String(compiledOk ? "true" : "false") + "," +
                            String(runtimeOk ? "true" : "false") + "],\"compiled\":[" + compiledStr +
                            "],\"runtime\":[" + runtimeStr + "]}";
                LOG_W(MODULE_PREFIX, "%s init values differ: %s", ts.typeKey.c_str(), ts.initFirstMismatch.c_str());
            }
        }
    }
    noteStack();
    RaftMutex_unlock(_mutex);
#else
    (void)deviceTypeIndex; (void)pRec; (void)pInitCode; (void)readData; (void)compiledOk; (void)compiledValues;
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Results as JSON fields
String DeviceTypeDecodeVerifier::getJson() const
{
    if (!RaftMutex_lock(_mutex, RAFT_MUTEX_WAIT_FOREVER))
        return "";
    uint32_t polls = 0, mismatches = 0, initRuns = 0, initMismatches = 0;
    String typesJson = "[";
    for (const TypeStats& ts : _types)
    {
        String t = "{";
        RaftJson::appendStringField(t, "type", ts.typeKey.c_str());
        t += ",\"idx\":" + String(ts.deviceTypeIndex);
        const char* pDecoderState = ts.notCompiled ? "notCompiled" : ts.pDecoder ? "compared" : ts.pPendingRec ? "pending" :
                    (ts.problem.length() > 0) ? "notCompared" : "noPolls";
        RaftJson::appendStringField(t, "decoder", pDecoderState);
        if (ts.problem.length() > 0)
            RaftJson::appendStringField(t, "problem", ts.problem.c_str());
        t += ",\"polls\":" + String(ts.polls) + ",\"records\":" + String(ts.records) + ",\"mismatches\":" + String(ts.mismatches);
        if (ts.skippedLowStack > 0)
            t += ",\"skippedLowStack\":" + String(ts.skippedLowStack);
        if (ts.notReady > 0)
            t += ",\"notReady\":" + String(ts.notReady);
        if (ts.pDecoder && (ts.pDecoder->getMaxEvalDepth() > 0))
            t += ",\"depth\":{\"eval\":" + String(ts.pDecoder->getMaxEvalDepth()) + ",\"exec\":" +
                        String(ts.pDecoder->getMaxExecDepth()) + "}";
        uint32_t compared = ts.pDecoder ? ts.polls : 0;
        if (compared > 0)
        {
            char usStr[120];
            snprintf(usStr, sizeof(usStr), ",\"us\":{\"compiled\":%.1f,\"runtime\":%.1f,\"compiledMax\":%u,\"runtimeMax\":%u}",
                        (double)ts.compiledUsTotal / compared, (double)ts.runtimeUsTotal / compared,
                        (unsigned)ts.compiledUsMax, (unsigned)ts.runtimeUsMax);
            t += usStr;
            // Custom decode code: interpreter steps per poll, and the time per step
            if (ts.nodes > 0)
            {
                snprintf(usStr, sizeof(usStr), ",\"nodes\":%.1f,\"nsPerNode\":%.0f,\"nsPerNodeWarm\":%.0f", (double)ts.nodes / compared,
                            (double)ts.runtimeUsTotal * 1000.0 / ts.nodes, (double)ts.runtimeWarmUsTotal * 1000.0 / ts.nodes);
                t += usStr;
            }
            // The same decodes run again straight away (warm caches)
            snprintf(usStr, sizeof(usStr), ",\"usWarm\":{\"compiled\":%.1f,\"runtime\":%.1f}",
                        (double)ts.compiledWarmUsTotal / compared, (double)ts.runtimeWarmUsTotal / compared);
            t += usStr;
        }
        if (ts.firstMismatch.length() > 0)
            RaftJson::appendRawField(t, "first", ts.firstMismatch.c_str());
        if (ts.pInit || (ts.initProblem.length() > 0) || (ts.initNotReady > 0))
        {
            String init = "{\"runs\":" + String(ts.initRuns) + ",\"mismatches\":" + String(ts.initMismatches);
            if (ts.initSkippedLowStack > 0)
                init += ",\"skippedLowStack\":" + String(ts.initSkippedLowStack);
            if (ts.initNotReady > 0)
                init += ",\"notReady\":" + String(ts.initNotReady);
            if (ts.initProblem.length() > 0)
                RaftJson::appendStringField(init, "problem", ts.initProblem.c_str());
            if (ts.initFirstMismatch.length() > 0)
                RaftJson::appendRawField(init, "first", ts.initFirstMismatch.c_str());
            init += "}";
            RaftJson::appendRawField(t, "init", init.c_str());
        }
        t += "}";
        typesJson += String(typesJson.length() > 1 ? "," : "") + t;
        if (!ts.notCompiled && ts.pDecoder)
            polls += ts.polls;
        mismatches += ts.mismatches;
        initRuns += ts.initRuns;
        initMismatches += ts.initMismatches;
    }
    typesJson += "]";
    String json = "\"polls\":" + String(polls) + ",\"mismatches\":" + String(mismatches) +
                ",\"initRuns\":" + String(initRuns) + ",\"initMismatches\":" + String(initMismatches);
    if (_stackMinFree != UINT32_MAX)
        json += ",\"stackMinFree\":" + String(_stackMinFree) + ",\"stackMinTask\":\"" + _stackMinTask + "\"";
    if (_stackAtCallMinFree != UINT32_MAX)
        json += ",\"stackAtCallMinFree\":" + String(_stackAtCallMinFree) + ",\"stackAtCallTask\":\"" + _stackAtCallTask + "\"";
    uint32_t skipped = 0;
    for (const TypeStats& ts : _types)
        skipped += ts.skippedLowStack + ts.initSkippedLowStack;
    json += ",\"skippedLowStack\":" + String(skipped) + ",\"stackNeeded\":" + String(STACK_NEEDED_BYTES);
    json += ",\"types\":" + typesJson;
    RaftMutex_unlock(_mutex);
    return json;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Clear the results
void DeviceTypeDecodeVerifier::reset()
{
    if (!RaftMutex_lock(_mutex, RAFT_MUTEX_WAIT_FOREVER))
        return;
    for (TypeStats& ts : _types)
    {
        ts.polls = ts.records = ts.mismatches = 0;
        ts.compiledUsTotal = ts.runtimeUsTotal = 0;
        ts.compiledUsMax = ts.runtimeUsMax = 0;
        ts.firstMismatch = "";
        ts.initRuns = ts.initMismatches = 0;
        ts.initFirstMismatch = "";
        ts.skippedLowStack = ts.initSkippedLowStack = 0;
        ts.notReady = ts.initNotReady = 0;
        ts.nodes = 0;
        ts.compiledWarmUsTotal = ts.runtimeWarmUsTotal = 0;
    }
    _stackMinFree = UINT32_MAX;
    _stackMinTask = "";
    _stackAtCallMinFree = UINT32_MAX;
    _stackAtCallTask = "";
    RaftMutex_unlock(_mutex);
}

#endif // RAFT_RUNTIME_DECODE_VERIFY
