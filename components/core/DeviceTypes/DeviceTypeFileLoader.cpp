/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// DeviceTypeFileLoader.cpp
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "DeviceTypeFileLoader.h"
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
    if (rec.contains("deviceTypeId"))
    {
        int64_t hwTypeId = 0;
        if (!parseWholeNumber(rec, "deviceTypeId", 0, 0xffff, hwTypeId))
        {
            reject(result, key, "deviceTypeId must be 0 to 65535 or a 0x hex string");
            return;
        }
        devTypeRec.setHwTypeId((uint16_t)hwTypeId);
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

    // A record with no addresses and no deviceTypeId can't be identified (it can still be used by a static device)
    if ((addresses.length() == 0) && !(devTypeRec.flags & DeviceTypeRecord::FLAG_HAS_HW_TYPE_ID))
        warn(result, key, "no addresses or deviceTypeId, so it can't be identified");

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
    }
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

    // The record (pollInfo and devInfoJson are stored as JSON text without whitespace, as the generator emits them)
    String pollInfoJson = rec.getString("pollInfo", "");
    String devInfoJson = rec.getString("devInfoJson", "");
    devTypeRec.deviceTypeName = key;
    devTypeRec.addresses = addresses;
    devTypeRec.detectionValues = detectionValues;
    devTypeRec.initValues = initValues;
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
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set the policy of a type that is present
void DeviceTypeFileLoader::applyPolicy(DeviceTypeRecords& table, const String& key, uint8_t policy, Result& result)
{
    DeviceTypeRecords::PolicyResult policyResult = table.setPolicy(key.c_str(), policy);
    if (policyResult == DeviceTypeRecords::PolicyResult::Set)
        result.disabled++;
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
    String out;
    out.reserve(pEnd - pStart);
    bool inString = false;
    for (const char* p = pStart; p < pEnd; p++)
    {
        char ch = *p;
        if (inString)
        {
            out += ch;
            if ((ch == '\\') && (p + 1 < pEnd))
                out += *++p;
            else if (ch == '"')
                inString = false;
        }
        else if (ch == '"')
        {
            inString = true;
            out += ch;
        }
        else if ((ch != ' ') && (ch != '\t') && (ch != '\r') && (ch != '\n'))
        {
            out += ch;
        }
    }
    return out;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check an addresses string
String DeviceTypeFileLoader::checkAddresses(const char* pAddresses, uint32_t maxAddr)
{
    // Parse "0xHH" at p, returning the value (or -1)
    auto parseAddr = [](const char*& p) -> int {
        if ((p[0] != '0') || ((p[1] != 'x') && (p[1] != 'X')) || !isxdigit((unsigned char)p[2]) || !isxdigit((unsigned char)p[3]))
            return -1;
        int value = (int)strtol(String(p + 2).substring(0, 2).c_str(), nullptr, 16);
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
