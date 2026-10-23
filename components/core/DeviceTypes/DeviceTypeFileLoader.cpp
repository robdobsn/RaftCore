/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// DeviceTypeFileLoader.cpp
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "DeviceTypeFileLoader.h"
#include "DeviceTypeCodeInterpreter.h"
#include "DeviceTypeRecords.h"
#include "DeviceTypeRecordDynamic.h"
#include "DeviceTypeGrammar.h"
#include "DevicePollingInfo.h"
#include "MiniHDLC.h"
#include "RaftJson.h"
#include "Logger.h"
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <functional>

namespace
{

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Skip whitespace
const char* skipWs(const char* p, const char* pEnd)
{
    while ((p < pEnd) && ((*p == ' ') || (*p == '\t') || (*p == '\r') || (*p == '\n')))
        p++;
    return p;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Skip a string starting at its opening quote; returns the position after the closing quote (nullptr if unterminated)
const char* skipString(const char* p, const char* pEnd)
{
    p++;
    while (p < pEnd)
    {
        if (*p == '\\')
            p += 2;
        else if (*p == '"')
            return p + 1;
        else
            p++;
    }
    return nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Skip a JSON value; returns the position after it (nullptr if malformed)
const char* skipValue(const char* p, const char* pEnd)
{
    p = skipWs(p, pEnd);
    if (p >= pEnd)
        return nullptr;
    if (*p == '"')
        return skipString(p, pEnd);
    if ((*p == '{') || (*p == '['))
    {
        uint32_t depth = 0;
        while (p < pEnd)
        {
            if (*p == '"')
            {
                p = skipString(p, pEnd);
                if (!p)
                    return nullptr;
                continue;
            }
            if ((*p == '{') || (*p == '['))
                depth++;
            else if ((*p == '}') || (*p == ']'))
            {
                depth--;
                if (depth == 0)
                    return p + 1;
            }
            p++;
        }
        return nullptr;
    }
    // Number or literal
    const char* pStart = p;
    while ((p < pEnd) && (*p != ',') && (*p != '}') && (*p != ']') && (*p > ' '))
        p++;
    return (p > pStart) ? p : nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Walk the members of a JSON object (one pass): fn(key, valueStart, valueEnd). Returns false if not an object
bool forEachMember(const char* p, const char* pEnd,
            const std::function<void(const String& key, const char* pValStart, const char* pValEnd)>& fn)
{
    p = skipWs(p, pEnd);
    if ((p >= pEnd) || (*p != '{'))
        return false;
    p = skipWs(p + 1, pEnd);
    if ((p < pEnd) && (*p == '}'))
        return true;
    while (p < pEnd)
    {
        if (*p != '"')
            return false;
        const char* pKeyEnd = skipString(p, pEnd);
        if (!pKeyEnd)
            return false;
        String key = RaftJson::unescapeStr(p + 1, pKeyEnd - 1);
        p = skipWs(pKeyEnd, pEnd);
        if ((p >= pEnd) || (*p != ':'))
            return false;
        const char* pValStart = skipWs(p + 1, pEnd);
        const char* pValEnd = skipValue(pValStart, pEnd);
        if (!pValEnd)
            return false;
        fn(key, pValStart, pValEnd);
        p = skipWs(pValEnd, pEnd);
        if ((p < pEnd) && (*p == ','))
        {
            p = skipWs(p + 1, pEnd);
            continue;
        }
        return (p < pEnd) && (*p == '}');
    }
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check a record's "hwProbeEffects": {"<scheme>": {"effect": ..., "basis": ..., "note": ...}} (as the generator's
// hw_probe_effects_problem); empty if valid, else a description of the first problem
String hwProbeEffectsProblem(const RaftJson& rec)
{
    int arrayLen = 0;
    if (rec.getType("hwProbeEffects", arrayLen) != RaftJsonIF::RAFT_JSON_OBJECT)
        return "must be an object keyed by scheme, e.g. {\"rsao\": {\"effect\": \"none\"}}";
    static const char* EFFECTS[] = {"none", "readEffect", "changes", "unknown"};
    static const char* BASES[] = {"bench", "datasheet", "source", "inferred"};
    auto isOneOf = [](const String& value, const char* const* pNames, uint32_t numNames) {
        for (uint32_t i = 0; i < numNames; i++)
            if (value.equals(pNames[i]))
                return true;
        return false;
    };
    std::vector<String> schemes;
    rec.getKeys("hwProbeEffects", schemes);
    for (const String& scheme : schemes)
    {
        if (!DeviceTypeRecord::isValidHwScheme(scheme.c_str()))
            return "scheme " + scheme + " must be [a-z][a-z0-9-]{0,15} (as in hwTypeId)";
        String entryPath = "hwProbeEffects/" + scheme;
        if (rec.getType(entryPath.c_str(), arrayLen) != RaftJsonIF::RAFT_JSON_OBJECT)
            return scheme + " must be an object with \"effect\"";
        std::vector<String> fields;
        rec.getKeys(entryPath.c_str(), fields);
        for (const String& field : fields)
            if (!field.equals("effect") && !field.equals("basis") && !field.equals("note"))
                return scheme + " has unknown field " + field;
        String effectPath = entryPath + "/effect";
        if ((rec.getType(effectPath.c_str(), arrayLen) != RaftJsonIF::RAFT_JSON_STRING) ||
                    !isOneOf(rec.getString(effectPath.c_str(), ""), EFFECTS, sizeof(EFFECTS) / sizeof(EFFECTS[0])))
            return scheme + " effect must be one of none, readEffect, changes, unknown";
        String basisPath = entryPath + "/basis";
        if (rec.contains(basisPath.c_str()) &&
                    ((rec.getType(basisPath.c_str(), arrayLen) != RaftJsonIF::RAFT_JSON_STRING) ||
                     !isOneOf(rec.getString(basisPath.c_str(), ""), BASES, sizeof(BASES) / sizeof(BASES[0]))))
            return scheme + " basis must be one of bench, datasheet, source, inferred";
        String notePath = entryPath + "/note";
        if (rec.contains(notePath.c_str()) && (rec.getType(notePath.c_str(), arrayLen) != RaftJsonIF::RAFT_JSON_STRING))
            return scheme + " note must be a string";
    }
    return "";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Parse a whole number from a JSON number or a string ("0x" hex or decimal); false if not one or out of range
bool parseWholeNumber(const RaftJson& rec, const char* pPath, int64_t minVal, int64_t maxVal, int64_t& value)
{
    int arrayLen = 0;
    RaftJsonIF::RaftJsonType type = rec.getType(pPath, arrayLen);
    if (type == RaftJsonIF::RAFT_JSON_NUMBER)
    {
        double parsed = rec.getDouble(pPath, -1);
        if (!std::isfinite(parsed) || (std::floor(parsed) != parsed) || (parsed < (double)minVal) || (parsed > (double)maxVal))
            return false;
        value = (int64_t)parsed;
        return true;
    }
    if (type != RaftJsonIF::RAFT_JSON_STRING)
        return false;
    String str = rec.getString(pPath, "");
    const char* pStr = str.c_str();
    bool isHex = (strncmp(pStr, "0x", 2) == 0) || (strncmp(pStr, "0X", 2) == 0);
    const char* pDigits = isHex ? pStr + 2 : pStr;
    if (!isxdigit((unsigned char)*pDigits) || (!isHex && !isdigit((unsigned char)*pDigits)))
        return false;
    char* pParseEnd = nullptr;
    long long parsed = strtoll(pDigits, &pParseEnd, isHex ? 16 : 10);
    if (!pParseEnd || (*pParseEnd != '\0') || (parsed < minVal) || (parsed > maxVal))
        return false;
    value = parsed;
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Largest number of bytes one step of a compiled string reads
uint32_t largestRead(const DeviceTypeGrammar::Result& compiled)
{
    uint32_t largest = 0;
    for (const DeviceTypeGrammar::Step& step : compiled.steps)
    {
        uint32_t len = step.readLen;
        if (step.readLenExpr)
        {
            uint32_t maxVal = step.readLenExpr->getMaxValue();
            if (maxVal > len)
                len = maxVal;
        }
        if (len > largest)
            largest = len;
    }
    return largest;
}

}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Apply a device type file
void DeviceTypeFileLoader::applyJson(DeviceTypeRecords& table, const char* pJson, const Limits& limits, Result& result)
{
    if (!pJson)
        return;
    const char* pEnd = pJson + strlen(pJson);

    // Find "devTypes" (other top-level keys are ignored)
    const char* pDevTypesStart = nullptr;
    const char* pDevTypesEnd = nullptr;
    bool isObject = forEachMember(pJson, pEnd, [&](const String& key, const char* pValStart, const char* pValEnd) {
        if (key == "devTypes")
        {
            pDevTypesStart = pValStart;
            pDevTypesEnd = pValEnd;
        }
    });
    if (!isObject || !pDevTypesStart)
    {
        reject(result, "", isObject ? "no devTypes object" : "not valid JSON");
        return;
    }

    // Collect the entries, in file order
    struct Entry
    {
        String key;
        const char* pStart;
        const char* pEnd;
    };
    std::vector<Entry> entries;
    if (!forEachMember(pDevTypesStart, pDevTypesEnd, [&](const String& key, const char* pValStart, const char* pValEnd) {
            entries.push_back({key, pValStart, pValEnd});
        }))
    {
        reject(result, "", "devTypes is not a valid JSON object");
        return;
    }
    result.fileValid = true;

    // Records first, then policies (a policy entry for an unknown key is reported once every record is in)
    std::vector<std::pair<String, uint8_t>> policies;
    for (const Entry& entry : entries)
    {
        if ((entry.pStart >= entry.pEnd) || (*entry.pStart != '{'))
        {
            reject(result, entry.key, "entry is not an object");
            continue;
        }
        RaftJson rec(entry.pStart, false, entry.pEnd);

        // Policy: "disable": true, "claim" or false
        uint8_t policy = DeviceTypeRecord::POLICY_ENABLED;
        int arrayLen = 0;
        RaftJsonIF::RaftJsonType disableType = rec.getType("disable", arrayLen);
        if (disableType == RaftJsonIF::RAFT_JSON_BOOLEAN)
        {
            policy = rec.getBool("disable", false) ? DeviceTypeRecord::POLICY_DISABLED : DeviceTypeRecord::POLICY_ENABLED;
        }
        else if ((disableType == RaftJsonIF::RAFT_JSON_STRING) && (rec.getString("disable", "") == "claim"))
        {
            policy = DeviceTypeRecord::POLICY_DISABLED_CLAIM;
        }
        else if (disableType != RaftJsonIF::RAFT_JSON_UNDEFINED)
        {
            reject(result, entry.key, "disable must be true, false or \"claim\"");
            continue;
        }

        // A disable entry for a type that is present sets its policy (the rest of the entry is ignored). For a type
        // that isn't present, a full record is added already disabled
        if (policy != DeviceTypeRecord::POLICY_ENABLED)
        {
            DeviceTypeIndexType existingIdx = DEVICE_TYPE_INDEX_INVALID;
            bool hasBody = rec.contains("devInfoJson") || rec.contains("detectionValues") || rec.contains("addresses") ||
                        rec.contains("pollInfo");
            if (table.getRecord(entry.key.c_str(), existingIdx) || !hasBody)
            {
                policies.push_back({entry.key, policy});
                continue;
            }
        }
        applyRecord(table, entry.key, entry.pStart, entry.pEnd, limits, policy, result);
    }
    for (const auto& policy : policies)
        applyPolicy(table, policy.first, policy.second, result);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check that the record with a key in the table has compiled init code computing a value
/// @param pName value name, or nullptr for any init code
bool DeviceTypeFileLoader::compiledInitValueFor(const DeviceTypeRecords& table, const String& key, const char* pName)
{
    DeviceTypeIndexType idx = DEVICE_TYPE_INDEX_INVALID;
    const DeviceTypeRecord* pRec = table.getRecord(key.c_str(), idx);
    if (!pRec || !pRec->initValuesFn || !pRec->initValueNames)
        return false;
    if (!pName)
        return true;
    String names = String(",") + pRec->initValueNames + ",";
    return names.indexOf(String(",") + pName + ",") >= 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check and add one record
void DeviceTypeFileLoader::applyRecord(DeviceTypeRecords& table, const String& key, const char* pStart,
            const char* pEnd, const Limits& limits, uint8_t initialPolicy, Result& result)
{
    RaftJson rec(pStart, false, pEnd);

    // The key is the device type key; "deviceType", if present, must agree (D13)
    if (key.length() == 0)
    {
        reject(result, key, "empty key");
        return;
    }
    String declaredType = rec.getString("deviceType", "");
    if (rec.contains("deviceType") && (declaredType != key))
    {
        reject(result, key, "deviceType field says '" + declaredType + "'");
        return;
    }

    // Addresses
    String addresses = rec.getString("addresses", "");
    String addrProblem = checkAddresses(addresses.c_str(), CatalogueIndex::MAX_ADDR);
    if (addrProblem.length() > 0)
    {
        reject(result, key, "addresses " + addrProblem);
        return;
    }

    // Init code from the file (run by the code interpreter, if built in): compiled to check it and find the values
    // it computes
    bool hasFileInitCode = false;
    std::vector<String> fileInitNames;
    String fileInitCode;
#ifdef RAFT_RUNTIME_DECODE_CODE
    if (rec.contains("initCode"))
    {
        int initCodeLen = 0;
        fileInitCode = (rec.getType("initCode", initCodeLen) == RaftJsonIF::RAFT_JSON_OBJECT) ?
                    rec.getString("initCode/c", "") : rec.getString("initCode", "");
        String problem;
        DeviceTypeCodeProgram* pProg = DeviceTypeCodeProgram::compile(fileInitCode.c_str(), DeviceTypeCodeProgram::Kind::Init,
                    {}, problem);
        if (!pProg)
        {
            reject(result, key, "initCode: " + problem);
            return;
        }
        fileInitNames = pProg->getInitValueNames();
        delete pProg;
        hasFileInitCode = true;
    }
#endif

    // Identification and scanning fields (as the generator reads them)
    DeviceTypeRecordDynamic devTypeRec;
    devTypeRec.scanPriority = DeviceTypeRecord::SCAN_PRIORITY_NONE;
    int arrayLen = 0;
    RaftJsonIF::RaftJsonType scanPriorityType = rec.getType("scanPriority", arrayLen);
    if (scanPriorityType == RaftJsonIF::RAFT_JSON_NUMBER)
    {
        long value = rec.getLong("scanPriority", 0);
        devTypeRec.scanPriority = (uint8_t)std::min(std::max(value - 1, 0L), (long)CatalogueIndex::NUM_SCAN_PRIORITY_LEVELS - 1);
    }
    else if (scanPriorityType != RaftJsonIF::RAFT_JSON_UNDEFINED)
    {
        String value = rec.getString("scanPriority", "");
        devTypeRec.scanPriority = (value == "high") ? 0 : (value == "medium") ? 1 : CatalogueIndex::NUM_SCAN_PRIORITY_LEVELS - 1;
    }
    if (rec.getBool("excludeFromAddrMap", false) || rec.getBool("probeOnly", false))
        devTypeRec.flags |= DeviceTypeRecord::FLAG_PROBE_ONLY;
    if (rec.contains("scanWhilePolled"))
    {
        if (rec.getType("scanWhilePolled", arrayLen) != RaftJsonIF::RAFT_JSON_BOOLEAN)
        {
            reject(result, key, "scanWhilePolled must be true or false");
            return;
        }
        if (!rec.getBool("scanWhilePolled", true))
            devTypeRec.flags |= DeviceTypeRecord::FLAG_NO_SCAN_WHILE_POLLED;
    }
    if (rec.contains("deviceTypeId"))
    {
        reject(result, key, "deviceTypeId is now hwTypeId, a string \"<scheme>:<value>\", e.g. \"rsao:0x0084\"");
        return;
    }
    if (rec.contains("hwTypeId"))
    {
        String hwTypeId = rec.getString("hwTypeId", "");
        if ((rec.getType("hwTypeId", arrayLen) != RaftJsonIF::RAFT_JSON_STRING) ||
                    !DeviceTypeRecord::isValidHwTypeId(hwTypeId.c_str()))
        {
            reject(result, key, "hwTypeId must be a string \"<scheme>:<value>\", e.g. \"rsao:0x0084\"");
            return;
        }
        devTypeRec.setHwTypeId(hwTypeId);

        // A hardware type ID names one record: one used by a record with another key would make lookups by the ID
        // ambiguous (and the file's record would take over the devices of the other) - N2
        DeviceTypeIndexType otherIdx = DEVICE_TYPE_INDEX_INVALID;
        const DeviceTypeRecord* pOther = table.getRecordByHwTypeId(hwTypeId.c_str(), otherIdx);
        if (pOther && pOther->deviceType && !key.equals(pOther->deviceType))
        {
            reject(result, key, "hwTypeId " + hwTypeId + " is already used by " + pOther->deviceType);
            return;
        }
    }
    // What other protocols' identification probes do to the device (keyed by scheme): checked, not kept - only
    // tooling reads it
    if (rec.contains("hwProbeEffects"))
    {
        String problem = hwProbeEffectsProblem(rec);
        if (problem.length() > 0)
        {
            reject(result, key, "hwProbeEffects " + problem);
            return;
        }
    }
    if (rec.contains("identPriority"))
    {
        int64_t identPriority = 0;
        if ((rec.getType("identPriority", arrayLen) != RaftJsonIF::RAFT_JSON_NUMBER) ||
                    !parseWholeNumber(rec, "identPriority", -128, 127, identPriority))
        {
            reject(result, key, "identPriority must be an integer from -128 to 127");
            return;
        }
        devTypeRec.identPriority = (int8_t)identPriority;
    }
    devTypeRec.initialPolicy = initialPolicy;

    // A record with no addresses and no hwTypeId can't be identified (it can still be used by a static device)
    if ((addresses.length() == 0) && (devTypeRec.hwTypeId.length() == 0))
        warn(result, key, "no addresses or hwTypeId, so it can't be identified");

    // Command strings: the grammar the firmware uses. An error is a reason to reject the record
    String detectionValues = rec.getString("detectionValues", "");
    String initValues = rec.getString("initValues", "");
    String pollCommands = rec.getString("pollInfo/c", "");
    const struct
    {
        const String& str;
        DeviceTypeGrammar::Field field;
    } commandStrs[] = {
        {detectionValues, DeviceTypeGrammar::Field::Detection},
        {initValues, DeviceTypeGrammar::Field::Init},
        {pollCommands, DeviceTypeGrammar::Field::Poll},
    };
    uint32_t maxRead = 0;
    for (const auto& commandStr : commandStrs)
    {
        DeviceTypeGrammar::Result compiled = DeviceTypeGrammar::compile(commandStr.str.c_str(), commandStr.field);
        for (const DeviceTypeGrammar::Issue& issue : compiled.issues)
        {
            String what = String(DeviceTypeGrammar::getFieldName(commandStr.field)) + " " +
                        DeviceTypeGrammar::getIssueName(issue.code) + " at " + String((int)issue.pos);
            if (issue.isError())
            {
                reject(result, key, what);
                return;
            }
            warn(result, key, what);
        }
        maxRead = std::max(maxRead, largestRead(compiled));

        // Init values ({name} value slots and {?name} guards computed by initCode): with the code interpreter
        // (RAFT_RUNTIME_DECODE_CODE) the record's own initCode must compute every value; otherwise init code must be
        // compiled into the firmware - the record must override a compiled record of the same key whose init code
        // provides every value (the override uses that code, as it inherits the decoder)
        for (const DeviceTypeGrammar::Step& step : compiled.steps)
        {
            std::vector<std::pair<String, String>> used;
            for (const DeviceTypeGrammar::Step::ValueSlot& slot : step.slots)
                used.push_back({String(slot.name.c_str()), "value slot {" + String(slot.name.c_str()) + "}"});
            if (!step.guard.empty())
                used.push_back({String(step.guard.c_str()), "guard {?" + String(step.guard.c_str()) + "}"});
            for (const auto& entry : used)
            {
                if (hasFileInitCode)
                {
                    bool computed = false;
                    for (const String& name : fileInitNames)
                        computed = computed || (name == entry.first);
                    if (!computed)
                    {
                        reject(result, key, "initValues " + entry.second + " isn't computed by its initCode");
                        return;
                    }
                }
                else if (!compiledInitValueFor(table, key, entry.first.c_str()))
                {
                    reject(result, key, "initValues " + entry.second + " needs init code - initCode (with the code "
                                "interpreter) or a compiled " + key + " record whose initCode computes it");
                    return;
                }
            }
        }
    }
#ifndef RAFT_RUNTIME_DECODE_CODE
    if (rec.contains("initCode"))
    {
        if (!compiledInitValueFor(table, key, nullptr))
        {
            reject(result, key, "initCode needs the code interpreter (RAFT_RUNTIME_DECODE_CODE) or init code compiled into the firmware");
            return;
        }
        warn(result, key, "initCode in a type file isn't used - the firmware's compiled init code is");
    }
#endif
    if (maxRead > limits.maxReadBytesPerCommand)
    {
        reject(result, key, "reads " + String((int)maxRead) + " bytes in one command, limit " +
                    String((int)limits.maxReadBytesPerCommand));
        return;
    }

    // Samples kept (missing or 0 means 1)
    long numSamples = rec.getLong("pollInfo/s", 0);
    if ((numSamples < 0) || ((uint32_t)numSamples > limits.maxSamples))
    {
        reject(result, key, "retains " + String((int)numSamples) + " samples, limit " + String((int)limits.maxSamples));
        return;
    }

    // Poll data size
    long pollDataSizeBytes = rec.getLong("devInfoJson/resp/b", 0);
    if ((pollDataSizeBytes < 0) || (pollDataSizeBytes > UINT16_MAX))
    {
        reject(result, key, "devInfoJson.resp.b out of range");
        return;
    }

    // The poll bus speed is a choice for the board (DevMan "typeSettings"), no longer part of a record
    for (const char* pBusKey : {"busHz", "h", "busHzSlots", "hSlots"})
    {
        if (rec.contains((String("pollInfo/") + pBusKey).c_str()))
            warn(result, key, String("pollInfo.") + pBusKey + " is no longer used - set the poll bus speed in DevMan typeSettings");
    }

    // Data supplied by firmware code (pollInfo.ext): the device isn't polled, so without that code it has no data
    if ((rec.getLong("pollInfo/ext", 0) != 0) && !table.hasExtDataSource(key.c_str()))
        warn(result, key, "pollInfo.ext is set but no firmware code supplies this type's data, so it will have no data");

    // The record (pollInfo and devInfoJson are stored as JSON text without whitespace, as the generator emits them)
    String pollInfoJson = rec.getString("pollInfo", "");
    String devInfoJson = rec.getString("devInfoJson", "");
    devTypeRec.deviceTypeName = key;
    devTypeRec.addresses = addresses;
    devTypeRec.detectionValues = detectionValues;
    devTypeRec.initValues = initValues;
    devTypeRec.initCode = fileInitCode;
    devTypeRec.pollInfo = rec.contains("pollInfo") ? minifyJson(pollInfoJson.c_str(), pollInfoJson.c_str() + pollInfoJson.length()) : "{}";
    String devInfoMin = rec.contains("devInfoJson") ? minifyJson(devInfoJson.c_str(), devInfoJson.c_str() + devInfoJson.length()) : "{}";
    devTypeRec.devInfoJson = devInfoMin.c_str();
    devTypeRec.pollDataSizeBytes = (uint16_t)pollDataSizeBytes;

    // What the poll reads produce, from the runtime's own poll info - it must match resp.b (the generator checks
    // this for compiled records), and the poll ring must be within the limit
    DeviceTypeRecord runtimeRec;
    devTypeRec.getDeviceTypeRecord(runtimeRec);
    DevicePollingInfo pollingInfo;
    table.getPollInfo(0, &runtimeRec, pollingInfo);
    uint32_t pollResultSize = pollingInfo.pollResultSizeIncTimestamp;
    if ((pollCommands.length() > 0) &&
                (pollResultSize != (uint32_t)pollDataSizeBytes + DevicePollingInfo::POLL_RESULT_TIMESTAMP_SIZE))
    {
        uint32_t readBytes = pollResultSize >= DevicePollingInfo::POLL_RESULT_TIMESTAMP_SIZE ?
                    pollResultSize - DevicePollingInfo::POLL_RESULT_TIMESTAMP_SIZE : 0;
        reject(result, key, "poll reads produce " + String((int)readBytes) + " bytes but devInfoJson.resp.b is " +
                    String((int)pollDataSizeBytes));
        return;
    }
    uint64_t ringBytes = (uint64_t)pollResultSize * pollingInfo.numPollResultsToStore;
    if (ringBytes > limits.maxRecordRingBytes)
    {
        reject(result, key, String((int)pollingInfo.numPollResultsToStore) + " samples of " + String((int)pollResultSize) +
                    " bytes exceeds the " + String((int)limits.maxRecordRingBytes) + " byte limit");
        return;
    }

    // Add
    DeviceTypeIndexType deviceTypeIndex = DEVICE_TYPE_INDEX_INVALID;
    DeviceTypeRecords::AddResult addResult = table.addRecord(devTypeRec, deviceTypeIndex);
    switch (addResult)
    {
        case DeviceTypeRecords::AddResult::Added:
            result.added++;
            break;
        case DeviceTypeRecords::AddResult::Replaced:
            result.replaced++;
            break;
        case DeviceTypeRecords::AddResult::AlreadyPresent:
            result.unchanged++;
            break;
        case DeviceTypeRecords::AddResult::Conflict:
            reject(result, key, "a different record with this key is already present");
            return;
        case DeviceTypeRecords::AddResult::Protected:
            reject(result, key, "protected (a system device expects this type)");
            return;
        case DeviceTypeRecords::AddResult::Full:
        {
            DeviceTypeRecords::ExtendedRecordStats stats = table.getExtendedRecordStats();
            reject(result, key, "no room (" + String((int)stats.count) + " of " + String((int)stats.maxRecords) +
                        " records, " + String((int)stats.bytesUsed) + " of " + String((int)stats.maxBytes) + " bytes used)");
            return;
        }
        default:
            reject(result, key, DeviceTypeRecords::getAddResultStr(addResult));
            return;
    }
    if (initialPolicy != DeviceTypeRecord::POLICY_ENABLED)
        result.disabled++;
    RecordOutcome* pOutcome = outcomeFor(result, key);
    if (pOutcome)
    {
        pOutcome->outcome = (addResult == DeviceTypeRecords::AddResult::Added) ? Outcome::Added :
                    (addResult == DeviceTypeRecords::AddResult::Replaced) ? Outcome::Replaced : Outcome::Unchanged;
        const DeviceTypeRecord* pAdded = table.getRecord(deviceTypeIndex);
        pOutcome->noDecoder = pAdded && (pAdded->pollDataSizeBytes > 0) &&
                    (table.getDecoderKind(deviceTypeIndex) == DeviceTypeRecords::DecoderKind::None);
        if (pAdded && (pAdded->pollDataSizeBytes > 0))
            pOutcome->decoder = DeviceTypeRecords::getDecoderKindStr(table.getDecoderKind(deviceTypeIndex));
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set the policy of a type that is present
void DeviceTypeFileLoader::applyPolicy(DeviceTypeRecords& table, const String& key, uint8_t policy, Result& result)
{
    DeviceTypeRecords::PolicyResult policyResult = table.setPolicy(key.c_str(), policy);
    if (policyResult == DeviceTypeRecords::PolicyResult::Set)
    {
        result.disabled++;
        RecordOutcome* pOutcome = outcomeFor(result, key);
        if (pOutcome)
            pOutcome->outcome = Outcome::Disabled;
    }
    else if (policyResult == DeviceTypeRecords::PolicyResult::NotFound)
        reject(result, key, "unknown key (no record to disable)");
    else
        reject(result, key, "protected (a system device expects this type)");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Reject an entry (or the file, with an empty key)
void DeviceTypeFileLoader::reject(Result& result, const String& key, const String& why)
{
    result.rejected++;
    String msg = key.length() > 0 ? key + ": " + why : why;
    if (result.firstError.length() == 0)
        result.firstError = msg;
    RecordOutcome* pOutcome = key.length() > 0 ? outcomeFor(result, key) : nullptr;
    if (pOutcome)
    {
        pOutcome->outcome = Outcome::Rejected;
        pOutcome->error = why;
    }
    LOG_W(MODULE_PREFIX, "rejected %s", msg.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Note a warning
void DeviceTypeFileLoader::warn(Result& result, const String& key, const String& why)
{
    result.warnings++;
    String msg = key + ": " + why;
    if (result.firstWarning.length() == 0)
        result.firstWarning = msg;
    RecordOutcome* pOutcome = outcomeFor(result, key);
    if (pOutcome)
    {
        if (pOutcome->warnings.length() > 0)
            pOutcome->warnings += "; ";
        pOutcome->warnings += why;
    }
    LOG_W(MODULE_PREFIX, "warning %s", msg.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief CRC16-CCITT of the file contents
uint16_t DeviceTypeFileLoader::crc16(const uint8_t* pData, uint32_t len)
{
    return MiniHDLC::crcUpdateCCITT(MiniHDLC::crcInitCCITT(), pData, len);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief JSON text without whitespace outside strings
String DeviceTypeFileLoader::minifyJson(const char* pStart, const char* pEnd)
{
    // Into a plain buffer, then one String (appending a character at a time was a large part of a load's time)
    std::vector<char> out;
    out.reserve(pEnd - pStart + 1);
    bool inString = false;
    for (const char* p = pStart; p < pEnd; p++)
    {
        char ch = *p;
        if (inString)
        {
            out.push_back(ch);
            if ((ch == '\\') && (p + 1 < pEnd))
                out.push_back(*++p);
            else if (ch == '"')
                inString = false;
        }
        else if (ch == '"')
        {
            inString = true;
            out.push_back(ch);
        }
        else if ((ch != ' ') && (ch != '\t') && (ch != '\r') && (ch != '\n'))
        {
            out.push_back(ch);
        }
    }
    return String(out.data(), out.size());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check an addresses string
String DeviceTypeFileLoader::checkAddresses(const char* pAddresses, uint32_t maxAddr)
{
    // Parse "0xHH" at p, returning the value (or -1)
    auto parseAddr = [](const char*& p) -> int {
        if ((p[0] != '0') || ((p[1] != 'x') && (p[1] != 'X')) || !isxdigit((unsigned char)p[2]) || !isxdigit((unsigned char)p[3]))
            return -1;
        char hex[3] = {p[2], p[3], 0};
        int value = (int)strtol(hex, nullptr, 16);
        p += 4;
        return value;
    };
    const char* p = pAddresses;
    if (!p)
        return "";
    while (*p == ' ')
        p++;
    if (*p == '\0')
        return "";
    while (true)
    {
        while (*p == ' ')
            p++;
        const char* pItem = p;
        int start = parseAddr(p);
        int end = start;
        if ((start >= 0) && (*p == '-'))
        {
            p++;
            end = parseAddr(p);
        }
        while (*p == ' ')
            p++;
        if ((start < 0) || (end < 0) || ((*p != ',') && (*p != '\0')))
        {
            String item(pItem);
            int comma = item.indexOf(',');
            return "item '" + (comma >= 0 ? item.substring(0, comma) : item) + "' is not 0xHH or 0xHH-0xHH";
        }
        if ((uint32_t)end > maxAddr)
        {
            char buf[64];
            snprintf(buf, sizeof(buf), "0x%02x is above 0x%02x", end, (unsigned)maxAddr);
            return buf;
        }
        if (end < start)
            return "range " + String(pItem).substring(0, 9) + " ends before it starts";
        if (*p == '\0')
            return "";
        p++;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief A record as a type file entry - "<key>":{...}
String DeviceTypeFileLoader::recordToFileEntry(const DeviceTypeRecord& rec, uint8_t policy)
{
    String json = "{";
    auto appendStr = [&json](const char* name, const char* value) {
        RaftJson::appendStringField(json, name, value ? value : "");
    };
    appendStr("addresses", rec.addresses);
    appendStr("detectionValues", rec.detectionValues);
    appendStr("initValues", rec.initValues);
    String pollInfo = (rec.pollInfo && rec.pollInfo[0]) ? rec.pollInfo : "{}";
    json += ",\"pollInfo\":" + pollInfo;
    if (rec.devInfoJson && rec.devInfoJson[0])
        json += ",\"devInfoJson\":" + String(rec.devInfoJson);
    static const char* SCAN_PRIORITY_NAMES[] = {"high", "medium", "low"};
    if (rec.scanPriority < sizeof(SCAN_PRIORITY_NAMES) / sizeof(SCAN_PRIORITY_NAMES[0]))
        appendStr("scanPriority", SCAN_PRIORITY_NAMES[rec.scanPriority]);
    if (rec.identPriority != 0)
        json += ",\"identPriority\":" + String((int)rec.identPriority);
    if (rec.hasHwTypeId())
        appendStr("hwTypeId", rec.hwTypeId);
    if (rec.isProbeOnly())
        json += ",\"excludeFromAddrMap\":true";
    if (!rec.isScannedWhilePolled())
        json += ",\"scanWhilePolled\":false";
    if (policy == DeviceTypeRecord::POLICY_DISABLED)
        json += ",\"disable\":true";
    else if (policy == DeviceTypeRecord::POLICY_DISABLED_CLAIM)
        appendStr("disable", "claim");
    json += "}";
    String entry;
    RaftJson::appendStringField(entry, "k", rec.deviceType ? rec.deviceType : "");
    // appendStringField gives "k":"<key>" - keep the escaped key
    return entry.substring(4) + ":" + json;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set a type's policy in type file text
String DeviceTypeFileLoader::setPolicyInFile(const char* pFileJson, const char* key, uint8_t policy, bool& changed)
{
    changed = false;
    String fileJson = pFileJson ? pFileJson : "";
    const char* pStart = fileJson.c_str();
    const char* pEnd = pStart + fileJson.length();
    String keyStr = key ? key : "";

    // The file's top-level members, and its devTypes entries (a missing or empty file is {"devTypes":{}})
    std::vector<std::pair<String, String>> topMembers;
    std::vector<std::pair<String, String>> entries;
    bool hasDevTypes = false;
    bool fileOk = (fileJson.length() == 0) || forEachMember(pStart, pEnd,
            [&](const String& memberKey, const char* pValStart, const char* pValEnd) {
                if (memberKey == "devTypes")
                {
                    hasDevTypes = true;
                    forEachMember(pValStart, pValEnd, [&](const String& entryKey, const char* pEntryStart, const char* pEntryEnd) {
                        entries.push_back({entryKey, String(pEntryStart).substring(0, pEntryEnd - pEntryStart)});
                    });
                }
                topMembers.push_back({memberKey, String(pValStart).substring(0, pValEnd - pValStart)});
            });
    if (!fileOk)
        return fileJson;

    // Find the type's entry
    int32_t entryIdx = -1;
    for (uint32_t i = 0; i < entries.size(); i++)
        if (entries[i].first == keyStr)
            entryIdx = i;
    String disableValue = (policy == DeviceTypeRecord::POLICY_DISABLED) ? "true" :
                (policy == DeviceTypeRecord::POLICY_DISABLED_CLAIM) ? "\"claim\"" : "";

    if (entryIdx < 0)
    {
        // No entry: one is added to disable the type; nothing is needed to enable it
        if (disableValue.length() == 0)
            return fileJson;
        entries.push_back({keyStr, "{\"disable\":" + disableValue + "}"});
    }
    else
    {
        // The entry without "disable", noting whether it has anything other than a policy
        String& entryJson = entries[entryIdx].second;
        String rebuilt = "{";
        bool policyOnly = true;
        String oldDisable;
        forEachMember(entryJson.c_str(), entryJson.c_str() + entryJson.length(),
                [&](const String& fieldKey, const char* pValStart, const char* pValEnd) {
                    String value = String(pValStart).substring(0, pValEnd - pValStart);
                    if (fieldKey == "disable")
                    {
                        oldDisable = value;
                        return;
                    }
                    if ((fieldKey != "deviceType") && (fieldKey != "_notes"))
                        policyOnly = false;
                    String fieldJson;
                    RaftJson::appendStringField(fieldJson, "k", fieldKey.c_str());
                    if (rebuilt.length() > 1)
                        rebuilt += ",";
                    rebuilt += fieldJson.substring(4) + ":" + value;
                });
        if (oldDisable == disableValue)
            return fileJson;
        if ((disableValue.length() == 0) && policyOnly)
        {
            entries.erase(entries.begin() + entryIdx);
        }
        else
        {
            if (disableValue.length() > 0)
                rebuilt += String(rebuilt.length() > 1 ? "," : "") + "\"disable\":" + disableValue;
            entryJson = rebuilt + "}";
        }
    }

    // Write the file again: the other top-level members as they were, and devTypes
    String devTypes = "{";
    for (uint32_t i = 0; i < entries.size(); i++)
    {
        String entryKeyJson;
        RaftJson::appendStringField(entryKeyJson, "k", entries[i].first.c_str());
        devTypes += String(i > 0 ? "," : "") + entryKeyJson.substring(4) + ":" + entries[i].second;
    }
    devTypes += "}";
    if (!hasDevTypes)
        topMembers.push_back({"devTypes", devTypes});
    String out = "{";
    for (uint32_t i = 0; i < topMembers.size(); i++)
    {
        String memberKeyJson;
        RaftJson::appendStringField(memberKeyJson, "k", topMembers[i].first.c_str());
        out += String(i > 0 ? "," : "") + memberKeyJson.substring(4) + ":" +
                    (topMembers[i].first == "devTypes" ? devTypes : topMembers[i].second);
    }
    out += "}";
    changed = true;
    return out;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief The outcome of an entry (added when first needed), if outcomes are kept
DeviceTypeFileLoader::RecordOutcome* DeviceTypeFileLoader::outcomeFor(Result& result, const String& key)
{
    if (!result.keepRecordOutcomes)
        return nullptr;
    for (RecordOutcome& outcome : result.records)
        if (outcome.key == key)
            return &outcome;
    RecordOutcome outcome;
    outcome.key = key;
    result.records.push_back(outcome);
    return &result.records.back();
}

const char* DeviceTypeFileLoader::getOutcomeStr(Outcome outcome)
{
    switch (outcome)
    {
        case Outcome::Added: return "added";
        case Outcome::Replaced: return "replaced";
        case Outcome::Unchanged: return "unchanged";
        case Outcome::Disabled: return "disabled";
        case Outcome::Rejected: return "rejected";
    }
    return "rejected";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check a device type file: what applying it would do at the next startup, without changing anything
void DeviceTypeFileLoader::checkJson(const DeviceTypeRecords& live, const std::vector<String>& liveFileKeys,
            const char* pJson, const Limits& limits, Result& result)
{
    // A table as the device's is before its type file is loaded: the compiled records (a new table has them),
    // records registered by code, protected types, ext data sources and the same limits
    DeviceTypeRecords scratch;
    scratch.setCheckOnly();
    DeviceTypeRecords::ExtendedRecordStats stats = live.getExtendedRecordStats();
    scratch.setExtendedRecordLimits(stats.maxRecords, stats.maxBytes);
    scratch.setDecoderPolicy(stats.decoderPolicy, stats.maxDecoderBytes);
    for (const String& key : live.getProtectedKeys())
        scratch.setProtected(key.c_str());
    for (const String& key : live.getExtDataSources())
        scratch.registerExtDataSource(key.c_str());
    uint32_t numBase = DeviceTypeRecords::getNumBaseRecords();
    for (uint32_t pos = 0; pos < stats.count; pos++)
    {
        const DeviceTypeRecord* pRec = live.getRecord(numBase + pos);
        if (!pRec || !pRec->deviceType)
            continue;
        bool fromFile = false;
        for (const String& fileKey : liveFileKeys)
            fromFile = fromFile || (fileKey == pRec->deviceType);
        if (fromFile)
            continue;
        DeviceTypeRecordDynamic dyn(pRec->deviceType, pRec->addresses, pRec->detectionValues, pRec->initValues,
                    pRec->pollInfo, pRec->pollDataSizeBytes, pRec->devInfoJson, pRec->pollResultDecodeFn);
        dyn.scanPriority = pRec->scanPriority;
        dyn.flags = pRec->flags;
        dyn.identPriority = pRec->identPriority;
        dyn.hwTypeId = pRec->hasHwTypeId() ? pRec->hwTypeId : "";
        dyn.initialPolicy = pRec->initialPolicy;
        DeviceTypeIndexType idx = DEVICE_TYPE_INDEX_INVALID;
        scratch.addRecord(dyn, idx);
    }

    // Apply the file to it
    result.keepRecordOutcomes = true;
    applyJson(scratch, pJson, limits, result);
}

