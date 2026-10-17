/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device type file tests (Stage 6)
//
// - A file adds, overrides (replacing the compiled record at its index) and disables records; the example file of
//   the feature/device-library branch loads with the result it was written for.
// - Each check on an untrusted record rejects that record alone, and the first error is kept.
// - Every record of a catalogue, loaded from a file, is identical to the record the generator compiles from it
//   (records from a file are first-class).
// - Record sources: identification waits until every registered source is ready, or its timeout passes.
//
// Usage: test_device_type_file <catalogue.json> <example-devlib.json>
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include "testhdr.h"
#include "ArduinoWString.h"
#include "ArduinoTime.h"
#include "RaftJson.h"
#include "DeviceTypeRecords.h"
#include "DeviceTypeFileLoader.h"

static std::string readFile(const char* pPath)
{
    std::ifstream file(pPath);
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

static DeviceTypeFileLoader::Result applyFile(DeviceTypeRecords& table, const std::string& json,
            const DeviceTypeFileLoader::Limits& limits = DeviceTypeFileLoader::Limits())
{
    DeviceTypeFileLoader::Result result;
    DeviceTypeFileLoader::applyJson(table, json.c_str(), limits, result);
    return result;
}

// A file with one record, with fields replaced as given
static std::string oneRecordFile(const char* key, const std::string& fields)
{
    return std::string(R"({"devTypes":{")") + key + R"(":{)" + fields + "}}}";
}

static const char* GOOD_FIELDS = R"("addresses":"0x51","detectionValues":"0x00=0x12","pollInfo":{"c":"0x01=r2","i":100,"s":2},)"
            R"("devInfoJson":{"name":"T","resp":{"b":2,"a":[{"n":"v","t":">H"}]}})";

static void testHelpers()
{
    printf("Running testHelpers...\n");
    const char* pCheck = "123456789";
    TEST_ASSERT(DeviceTypeFileLoader::crc16((const uint8_t*)pCheck, 9) == 0x29b1, "CRC16-CCITT check value");
    const char* pJson = "{ \"a\" : [ 1, 2 ],\n \"b\":\"x y \\\" z\" }";
    TEST_ASSERT(DeviceTypeFileLoader::minifyJson(pJson, pJson + strlen(pJson)) == "{\"a\":[1,2],\"b\":\"x y \\\" z\"}",
                "minify keeps strings");

    // Addresses: the generator's format
    TEST_ASSERT(DeviceTypeFileLoader::checkAddresses("", 0x77) == "", "empty addresses");
    TEST_ASSERT(DeviceTypeFileLoader::checkAddresses("0x18-0x1f", 0x77) == "", "range");
    TEST_ASSERT(DeviceTypeFileLoader::checkAddresses("0x48, 0x49,0x4A", 0x77) == "", "list");
    TEST_ASSERT(DeviceTypeFileLoader::checkAddresses("0o51", 0x77) != "", "octal rejected");
    TEST_ASSERT(DeviceTypeFileLoader::checkAddresses("0x48,junk", 0x77) != "", "junk rejected");
    TEST_ASSERT(DeviceTypeFileLoader::checkAddresses("72", 0x77) != "", "decimal rejected");
    TEST_ASSERT(DeviceTypeFileLoader::checkAddresses("0x78", 0x77) != "", "above the maximum rejected");
    TEST_ASSERT(DeviceTypeFileLoader::checkAddresses("0x20-0x10", 0x77) != "", "backwards range rejected");
    TEST_ASSERT(DeviceTypeFileLoader::checkAddresses("0x48,", 0x77) != "", "trailing comma rejected");
}

static void testBranchExampleFile(const char* pExamplePath)
{
    printf("Running testBranchExampleFile...\n");
    DeviceTypeRecords table;
    DeviceTypeIndexType mcpIdx = DEVICE_TYPE_INDEX_INVALID, lsmIdx = DEVICE_TYPE_INDEX_INVALID;
    TEST_ASSERT(table.getRecord("MCP9808", mcpIdx) && table.getRecord("LSM6DS", lsmIdx), "base records present");
    std::string json = readFile(pExamplePath);
    TEST_ASSERT(json.length() > 0, "example file read");
    DeviceTypeFileLoader::Result result = applyFile(table, json);
    TEST_ASSERT(result.fileValid, "file valid");
    TEST_ASSERT(result.added == 1, "one record added (ExampleTempSensor)");
    TEST_ASSERT(result.replaced == 1, "one record replaced (MCP9808)");
    TEST_ASSERT(result.disabled == 1, "one type disabled (LSM6DS)");
    TEST_ASSERT(result.rejected == 0, ("nothing rejected: " + std::string(result.firstError.c_str())).c_str());

    // The override has the base record's index, and its content
    DeviceTypeIndexType idx = DEVICE_TYPE_INDEX_INVALID;
    const DeviceTypeRecord* pRec = table.getRecord("MCP9808", idx);
    TEST_ASSERT(pRec && (idx == mcpIdx), "override keeps the base index");
    TEST_ASSERT(table.getRecordSource(mcpIdx) == DeviceTypeRecords::RecordSource::Override, "source is override");
    TEST_ASSERT(pRec && strstr(pRec->devInfoJson, "Overridden to poll once a second"), "override content");
    TEST_ASSERT(pRec && strcmp(pRec->pollInfo, R"({"c":"0x05=r2","i":1000,"s":1})") == 0, "pollInfo minified");
    TEST_ASSERT(pRec && (pRec->scanPriority == 0), "scanPriority high");

    // The new record and the disabled one
    const DeviceTypeRecord* pNew = table.getRecord("ExampleTempSensor", idx);
    TEST_ASSERT(pNew && (table.getRecordSource(idx) == DeviceTypeRecords::RecordSource::Extended), "new record extended");
    TEST_ASSERT(pNew && (pNew->pollDataSizeBytes == 2), "new record poll size");
    TEST_ASSERT(table.getPolicy(lsmIdx) == DeviceTypeRecord::POLICY_DISABLED, "LSM6DS disabled");
    TEST_ASSERT(table.getRecord("LSM6DS", idx) != nullptr, "a disabled type can still be looked up");
}

static void testFileLevel()
{
    printf("Running testFileLevel...\n");
    DeviceTypeRecords table;
    DeviceTypeFileLoader::Result result = applyFile(table, R"({"_notes":"x","devTypes":{}})");
    TEST_ASSERT(result.fileValid && (result.rejected == 0) && !result.anyApplied(), "empty devTypes is valid and does nothing");
    result = applyFile(table, R"({"other":{}})");
    TEST_ASSERT(!result.fileValid && (result.rejected == 1) && (result.firstError == "no devTypes object"), "no devTypes");
    result = applyFile(table, R"({"devTypes":{"A":{)");
    TEST_ASSERT(!result.fileValid && (result.rejected == 1), "truncated file");
    result = applyFile(table, "not json");
    TEST_ASSERT(!result.fileValid && (result.firstError == "not valid JSON"), "not JSON");
}

static void testRecordChecks()
{
    printf("Running testRecordChecks...\n");
    struct Case
    {
        const char* name;
        std::string fields;
        const char* errorPart;
    };
    std::string good = GOOD_FIELDS;
    const Case cases[] = {
        {"deviceType mismatch", R"("deviceType":"Other",)" + good, "deviceType field says 'Other'"},
        {"bad addresses", R"("addresses":"0x51,junk","detectionValues":"0x00=0x12")", "addresses item 'junk'"},
        {"bad deviceTypeId", R"("deviceTypeId":"0x1FFFF",)" + good, "deviceTypeId"},
        {"deviceTypeId text", R"("deviceTypeId":"abc",)" + good, "deviceTypeId"},
        {"identPriority range", R"("identPriority":200,)" + good, "identPriority"},
        {"grammar error", R"("addresses":"0x51","detectionValues":"0x00=0xZZ")", "detectionValues BadCheck"},
        {"read limit", R"("addresses":"0x51","detectionValues":"0x00=r2000")", "reads 2000 bytes in one command, limit 1024"},
        {"read limit expr", R"("addresses":"0x51","pollInfo":{"c":"0x00=r1&0x01=r{$0.w12:mask0FFF*2:max5000}","i":100,"s":1},"devInfoJson":{"resp":{"b":5001}})",
                    "reads 5000 bytes"},
        {"samples", R"("addresses":"0x51","pollInfo":{"c":"0x01=r2","i":100,"s":300},"devInfoJson":{"resp":{"b":2}})", "retains 300 samples"},
        {"resp.b mismatch", R"("addresses":"0x51","pollInfo":{"c":"0x01=r2","i":100,"s":1},"devInfoJson":{"resp":{"b":3}})",
                    "poll reads produce 2 bytes but devInfoJson.resp.b is 3"},
        {"ring limit", R"("addresses":"0x51","pollInfo":{"c":"0x01=r1000","i":100,"s":100},"devInfoJson":{"resp":{"b":1000}})",
                    "100 samples of 1002 bytes exceeds the 65536 byte limit"},
        {"bad disable", R"("disable":"yes")", "disable must be"},
    };
    for (const Case& c : cases)
    {
        DeviceTypeRecords table;
        DeviceTypeFileLoader::Result result = applyFile(table, oneRecordFile("FileRec", c.fields));
        bool ok = (result.rejected == 1) && (result.added == 0) && strstr(result.firstError.c_str(), c.errorPart) &&
                    result.firstError.startsWith("FileRec: ");
        if (!ok)
            printf("  %s: rejected %d first error '%s'\n", c.name, (int)result.rejected, result.firstError.c_str());
        TEST_ASSERT(ok, c.name);
        DeviceTypeIndexType idx;
        TEST_ASSERT(table.getRecord("FileRec", idx) == nullptr, (std::string(c.name) + ": not added").c_str());
    }

    // One bad record doesn't stop the others; the first error is kept
    DeviceTypeRecords table;
    std::string json = std::string(R"({"devTypes":{"Bad1":{"addresses":"zz"},"Good":{)") + GOOD_FIELDS +
                R"(},"Bad2":{"addresses":"0x51","detectionValues":"0x00=0xZZ"}}})";
    DeviceTypeFileLoader::Result result = applyFile(table, json);
    TEST_ASSERT((result.added == 1) && (result.rejected == 2), "good record added, bad ones rejected");
    TEST_ASSERT(result.firstError.startsWith("Bad1: "), "first error kept");
}

static void testRecordFields()
{
    printf("Running testRecordFields...\n");
    DeviceTypeRecords table;
    std::string json = R"({"devTypes":{)"
                R"("Rsao":{"deviceTypeId":"0x00AB","excludeFromAddrMap":true,"addresses":"0x10-0x17","pollInfo":{"c":"0x01=r2","i":100,"s":1},"devInfoJson":{"resp":{"b":2}}},)"
                R"("Num":{"deviceTypeId":172,"addresses":"0x52","identPriority":-5,"scanPriority":2,"detectionValues":"0x00=0x12"},)"
                R"("NoAddr":{"detectionValues":"0x00=0x12"},)"
                R"("Warn":{"addresses":"0x53","detectionValues":"0x00=0x123","_notes":"ignored"})"
                "}}";
    DeviceTypeFileLoader::Result result = applyFile(table, json);
    TEST_ASSERT((result.added == 4) && (result.rejected == 0), ("records added: " + std::string(result.firstError.c_str())).c_str());
    DeviceTypeIndexType idx;
    const DeviceTypeRecord* pRsao = table.getRecord("Rsao", idx);
    TEST_ASSERT(pRsao && pRsao->isProbeOnly() && (pRsao->hwTypeId == 0xab) && (pRsao->flags & DeviceTypeRecord::FLAG_HAS_HW_TYPE_ID),
                "probe-only record with hwTypeId");
    TEST_ASSERT(pRsao && (pRsao->scanPriority == DeviceTypeRecord::SCAN_PRIORITY_NONE), "scanPriority absent: none (as the generator)");
    DeviceTypeIndexType rsaoIdx = idx;
    TEST_ASSERT(table.getRecordByHwTypeId(0xab, idx) == pRsao && (idx == rsaoIdx), "found by hwTypeId");
    const DeviceTypeRecord* pNum = table.getRecord("Num", idx);
    TEST_ASSERT(pNum && (pNum->identPriority == -5) && (pNum->scanPriority == 1), "identPriority and numeric scanPriority");
    TEST_ASSERT(pNum && (pNum->hwTypeId == 172), "numeric deviceTypeId");
    TEST_ASSERT(result.warnings == 2, "warnings: no addresses or deviceTypeId; odd hex");
    TEST_ASSERT(result.firstWarning.startsWith("NoAddr: no addresses"), "first warning");
}

// A hardware type ID names one record: a file record with an ID already used by a record with another key is
// rejected (it would take over that record's devices), and a code-registered one is added (trusted) - N2
static void testDuplicateHwTypeId()
{
    printf("Running testDuplicateHwTypeId...\n");
    DeviceTypeRecords table;
    std::string json = R"({"devTypes":{)"
                R"("RsaoA":{"deviceTypeId":"0x0084","excludeFromAddrMap":true,"addresses":"0x10"},)"
                R"("RsaoB":{"deviceTypeId":132,"excludeFromAddrMap":true,"addresses":"0x10"},)"
                R"("RsaoC":{"deviceTypeId":"0x0085","excludeFromAddrMap":true,"addresses":"0x10"})"
                "}}";
    DeviceTypeFileLoader::Result result = applyFile(table, json);
    TEST_ASSERT((result.added == 2) && (result.rejected == 1), "the record with a duplicate ID is rejected");
    TEST_ASSERT(result.firstError == "RsaoB: deviceTypeId 0x0084 is already used by RsaoA", result.firstError.c_str());
    DeviceTypeIndexType idx;
    const DeviceTypeRecord* pFound = table.getRecordByHwTypeId(0x84, idx);
    TEST_ASSERT(pFound && (strcmp(pFound->deviceType, "RsaoA") == 0), "the ID still finds the first record");

    // The same key again (the same content) keeps its own ID
    result = applyFile(table, R"({"devTypes":{"RsaoA":{"deviceTypeId":"0x0084","excludeFromAddrMap":true,"addresses":"0x10"}}})");
    TEST_ASSERT((result.rejected == 0) && (result.unchanged == 1), "the same record again is not a duplicate");

    // Code-registered records are trusted: added, with a warning logged
    DeviceTypeRecordDynamic dyn;
    dyn.deviceTypeName = "CodeRec";
    dyn.addresses = "0x11";
    dyn.setHwTypeId(0x85);
    TEST_ASSERT(table.addRecord(dyn, idx) == DeviceTypeRecords::AddResult::Added, "code-registered duplicate is added");
}

// A record with pollInfo.ext is reported unless firmware code supplies its data (§10.7 item 15)
static void testExtDataSource()
{
    printf("Running testExtDataSource...\n");
    DeviceTypeRecords table;
    table.registerExtDataSource("ExtSupplied");
    table.registerExtDataSource("ExtSupplied");
    TEST_ASSERT(table.hasExtDataSource("ExtSupplied") && !table.hasExtDataSource("ExtOther"), "registered sources");
    std::string json = R"({"devTypes":{)"
                R"("ExtSupplied":{"addresses":"0x29","detectionValues":"0x00=0x12","pollInfo":{"ext":1},"devInfoJson":{"resp":{"b":2}}},)"
                R"("ExtOther":{"addresses":"0x2a","detectionValues":"0x00=0x12","pollInfo":{"ext":1},"devInfoJson":{"resp":{"b":2}}})"
                "}}";
    DeviceTypeFileLoader::Result result = applyFile(table, json);
    TEST_ASSERT((result.added == 2) && (result.rejected == 0), ("both added: " + std::string(result.firstError.c_str())).c_str());
    TEST_ASSERT(result.warnings == 1, "one warning");
    TEST_ASSERT(result.firstWarning.startsWith("ExtOther: pollInfo.ext is set but no firmware code"), result.firstWarning.c_str());
}

// The poll bus speed is a choice for the board (DevMan "typeSettings"): a record that still has it is warned about
static void testRecordBusSpeedWarning()
{
    printf("Running testRecordBusSpeedWarning...\n");
    DeviceTypeRecords table;
    DeviceTypeFileLoader::Result result = applyFile(table, oneRecordFile("BusSpeed",
                R"("addresses":"0x51","detectionValues":"0x00=0x12","pollInfo":{"c":"0x01=r2","i":100,"s":1,"busHz":400000},)"
                R"("devInfoJson":{"name":"T","resp":{"b":2,"a":[{"n":"v","t":">H"}]}})"));
    TEST_ASSERT((result.added == 1) && (result.warnings == 1), "added, with a warning");
    TEST_ASSERT(result.firstWarning.startsWith("BusSpeed: pollInfo.busHz is no longer used"), result.firstWarning.c_str());
}

// Checking a file (devman/typefile/check): what it would do at the next startup, against a table as the device's is
// before its type file is loaded - without changing the device's table
static const DeviceTypeFileLoader::RecordOutcome* findOutcome(const DeviceTypeFileLoader::Result& result, const char* key)
{
    for (const auto& outcome : result.records)
        if (outcome.key == key)
            return &outcome;
    return nullptr;
}

static void testCheckJson()
{
    printf("Running testCheckJson...\n");
    // The device's table: a record registered by code, a protected type, an ext data source, and its current type
    // file (a new record and an override of a compiled one)
    DeviceTypeRecords live;
    DeviceTypeRecordDynamic code("CodeRec", "0x52", "0x00=0x12", "", R"({"c":"0x01=r2","i":100,"s":1})", 2,
                R"({"name":"Code","resp":{"b":2,"a":[{"n":"v","t":">H"}]}})", nullptr);
    DeviceTypeIndexType idx;
    TEST_ASSERT(live.addRecord(code, idx) == DeviceTypeRecords::AddResult::Added, "code record");
    live.setProtected("LSM6DS");
    live.registerExtDataSource("ExtA");
    TEST_ASSERT(live.getRecord("MCP9808", idx) != nullptr, "MCP9808 compiled");
    String mcp = DeviceTypeFileLoader::recordToFileEntry(*live.getRecord(idx), DeviceTypeRecord::POLICY_ENABLED);
    String mcpOverride = mcp;
    mcpOverride.replace("\"initValues\":\"0x010000\"", "\"initValues\":\"0x010100\"");
    TEST_ASSERT(mcpOverride != mcp, "override differs");
    std::string current = std::string(R"({"devTypes":{"Alpha":{)") + GOOD_FIELDS + "}," + mcpOverride.c_str() + "}}";
    DeviceTypeFileLoader::Result loaded;
    loaded.keepRecordOutcomes = true;
    DeviceTypeFileLoader::applyJson(live, current.c_str(), DeviceTypeFileLoader::Limits(), loaded);
    TEST_ASSERT((loaded.added == 1) && (loaded.replaced == 1) && (loaded.records.size() == 2), "current file applied");
    std::vector<String> fileKeys;
    for (const auto& outcome : loaded.records)
        fileKeys.push_back(outcome.key);
    DeviceTypeRecords::ExtendedRecordStats statsBefore = live.getExtendedRecordStats();
    uint32_t generationBefore = live.getGeneration();

    // The file to check
    std::string checked = std::string(R"({"devTypes":{)") +
                R"("Alpha":{)" + GOOD_FIELDS + "}," +
                mcpOverride.c_str() + "," +
                R"("CodeRec":{"addresses":"0x52","detectionValues":"0x00=0x13","pollInfo":{"c":"0x01=r2","i":100,"s":1},"devInfoJson":{"name":"Other","resp":{"b":2,"a":[{"n":"v","t":">H"}]}}},)" +
                R"("LSM6DS":{"disable":true},)" +
                R"("TrillRing":{"disable":true},)" +
                R"("ExtA":{"addresses":"0x2a","detectionValues":"0x00=0x12","pollInfo":{"ext":1},"devInfoJson":{"resp":{"b":2}}},)" +
                R"("Nope":{"disable":true})" +
                "}}";
    DeviceTypeFileLoader::Result result;
    DeviceTypeFileLoader::checkJson(live, fileKeys, checked.c_str(), DeviceTypeFileLoader::Limits(), result);
    using O = DeviceTypeFileLoader::Outcome;
    const auto* pAlpha = findOutcome(result, "Alpha");
    TEST_ASSERT(pAlpha && (pAlpha->outcome == O::Added) && pAlpha->noDecoder, "a record from the current file is added again (no decoder)");
    const auto* pMcp = findOutcome(result, "MCP9808");
    TEST_ASSERT(pMcp && (pMcp->outcome == O::Replaced) && !pMcp->noDecoder, "override of a compiled record, decoder kept (same layout)");
    const auto* pCode = findOutcome(result, "CodeRec");
    TEST_ASSERT(pCode && (pCode->outcome == O::Rejected) && pCode->error.startsWith("a different record"), "conflicts with a code record");
    const auto* pLsm = findOutcome(result, "LSM6DS");
    TEST_ASSERT(pLsm && (pLsm->outcome == O::Rejected) && pLsm->error.startsWith("protected"), "protected type");
    const auto* pTrill = findOutcome(result, "TrillRing");
    TEST_ASSERT(pTrill && (pTrill->outcome == O::Disabled), "policy entry");
    const auto* pExt = findOutcome(result, "ExtA");
    TEST_ASSERT(pExt && (pExt->outcome == O::Added) && (pExt->warnings.length() == 0), "ext record with a data source: no warning");
    const auto* pNope = findOutcome(result, "Nope");
    TEST_ASSERT(pNope && (pNope->outcome == O::Rejected), "policy for an unknown type");
    TEST_ASSERT((result.added == 2) && (result.replaced == 1) && (result.disabled == 1) && (result.rejected == 3), "counts");
    TEST_ASSERT(result.records.size() == 7, "an outcome per entry");

    // A changed response layout: no decoder for the override
    String mcpNewLayout = mcp;
    mcpNewLayout.replace("\"n\":\"temperature\"", "\"n\":\"tempC\"");
    TEST_ASSERT(mcpNewLayout != mcp, "layout changed");
    DeviceTypeFileLoader::Result layout;
    DeviceTypeFileLoader::checkJson(live, fileKeys, (std::string("{\"devTypes\":{") + mcpNewLayout.c_str() + "}}").c_str(),
                DeviceTypeFileLoader::Limits(), layout);
    const auto* pLayout = findOutcome(layout, "MCP9808");
    TEST_ASSERT(pLayout && (pLayout->outcome == O::Replaced) && pLayout->noDecoder, "override with a new layout: no decoder");

    // The device's table is unchanged
    DeviceTypeRecords::ExtendedRecordStats statsAfter = live.getExtendedRecordStats();
    TEST_ASSERT((statsAfter.count == statsBefore.count) && (statsAfter.bytesUsed == statsBefore.bytesUsed) &&
                (live.getGeneration() == generationBefore), "live table unchanged");
    TEST_ASSERT(live.getPolicy(live.getRecord("TrillRing", idx) ? idx : 0) == DeviceTypeRecord::POLICY_ENABLED, "live policy unchanged");
}

static void testOverridesAndPolicies()
{
    printf("Running testOverridesAndPolicies...\n");
    DeviceTypeRecords table;
    DeviceTypeIndexType mcpIdx = DEVICE_TYPE_INDEX_INVALID, lsmIdx = DEVICE_TYPE_INDEX_INVALID;
    table.getRecord("MCP9808", mcpIdx);
    table.getRecord("LSM6DS", lsmIdx);
    table.setProtected("LSM6DS");
    std::string json = R"({"devTypes":{)"
                R"("LSM6DS":{"disable":true},)"                                            // protected
                R"("NoSuchType":{"disable":true},)"                                        // unknown key
                R"("MCP9808":{"disable":"claim","addresses":"0x18"},)"                     // present: policy only
                R"("NewDisabled":{"disable":true,"addresses":"0x55","detectionValues":"0x00=0x12"})"   // new: added disabled
                "}}";
    DeviceTypeFileLoader::Result result = applyFile(table, json);
    TEST_ASSERT((result.rejected == 2) && (result.disabled == 2) && (result.added == 1), "policies applied");
    TEST_ASSERT(result.firstError.startsWith("NoSuchType: unknown key") || result.firstError.startsWith("LSM6DS: protected"),
                "rejections reported");
    TEST_ASSERT(table.getPolicy(lsmIdx) == DeviceTypeRecord::POLICY_ENABLED, "protected type not disabled");
    TEST_ASSERT(table.getPolicy(mcpIdx) == DeviceTypeRecord::POLICY_DISABLED_CLAIM, "claim policy");
    TEST_ASSERT(table.getRecordSource(mcpIdx) == DeviceTypeRecords::RecordSource::Base, "policy-only entry doesn't override");
    DeviceTypeIndexType idx;
    TEST_ASSERT(table.getRecord("NewDisabled", idx) && (table.getPolicy(idx) == DeviceTypeRecord::POLICY_DISABLED),
                "new record added disabled");

    // A protected base record can't be overridden; the same file again changes nothing; a different record for an
    // added key conflicts
    table.setProtected("MCP9808");
    result = applyFile(table, oneRecordFile("MCP9808", GOOD_FIELDS));
    TEST_ASSERT((result.rejected == 1) && strstr(result.firstError.c_str(), "protected"), "protected override rejected");
    result = applyFile(table, oneRecordFile("FileRec", GOOD_FIELDS));
    TEST_ASSERT(result.added == 1, "added");
    result = applyFile(table, oneRecordFile("FileRec", GOOD_FIELDS));
    TEST_ASSERT((result.unchanged == 1) && (result.rejected == 0), "same record again is unchanged");
    result = applyFile(table, oneRecordFile("FileRec", std::string(R"("identPriority":3,)") + GOOD_FIELDS));
    TEST_ASSERT((result.rejected == 1) && strstr(result.firstError.c_str(), "already present"), "different record conflicts");

    // Full
    DeviceTypeRecords small;
    small.setExtendedRecordLimits(1, 0);
    json = std::string(R"({"devTypes":{"F1":{)") + GOOD_FIELDS + R"(},"F2":{)" + GOOD_FIELDS + "}}}";
    result = applyFile(small, json);
    TEST_ASSERT((result.added == 1) && (result.rejected == 1) && strstr(result.firstError.c_str(), "F2: no room (1 of 1 records"),
                "limit reported");
}

// JSON texts with the same tokens: strings equal once unescaped (the generator writes non-ASCII as \uXXXX) and
// numbers equal as doubles (the generator re-prints floats, e.g. 182.04444444444444 as 182.04444444444445)
static bool sameJson(const char* pA, const char* pB)
{
    auto next = [](const char*& p, std::string& tok, bool& isNum) -> bool {
        while (*p == ' ')
            p++;
        if (!*p)
            return false;
        isNum = false;
        if (*p == '"')
        {
            const char* pStart = ++p;
            while (*p && (*p != '"'))
                p += (*p == '\\') ? 2 : 1;
            tok = RaftJson::unescapeStr(pStart, p).c_str();
            if (*p)
                p++;
            return true;
        }
        if ((*p == '-') || isdigit((unsigned char)*p))
        {
            const char* pStart = p;
            while (*p && strchr("+-.eE0123456789", *p))
                p++;
            tok.assign(pStart, p);
            isNum = true;
            return true;
        }
        tok.assign(p, 1);
        p++;
        return true;
    };
    std::string tokA, tokB;
    bool numA = false, numB = false;
    while (true)
    {
        bool moreA = next(pA, tokA, numA);
        bool moreB = next(pB, tokB, numB);
        if (moreA != moreB)
            return false;
        if (!moreA)
            return true;
        if (numA && numB)
        {
            double a = strtod(tokA.c_str(), nullptr), b = strtod(tokB.c_str(), nullptr);
            if (std::fabs(a - b) > 1e-12 * std::max(std::fabs(a), std::fabs(b)))
                return false;
        }
        else if ((numA != numB) || (tokA != tokB))
        {
            return false;
        }
    }
}

// Every record of a catalogue, loaded from a file, equals the record the generator compiled from it
static void testCatalogueRoundTrip(const char* pCataloguePath)
{
    printf("Running testCatalogueRoundTrip (%s)...\n", pCataloguePath);
    TEST_ASSERT(!sameJson(R"({"a":1,"u":"x"})", R"({"a":2,"u":"x"})") && !sameJson(R"({"u":"x"})", R"({"u":"y"})"),
                "sameJson finds differences");
    TEST_ASSERT(sameJson("{\"u\":\"\xc2\xb0" "C\",\"d\":0.1}", R"({"u":"\u00b0C","d":0.10000000000000001})"),
                "sameJson: escapes and float printing");
    DeviceTypeRecords table;
    table.setExtendedRecordLimits(1000, 4 * 1024 * 1024);
    uint32_t numBase = DeviceTypeRecords::getNumBaseRecords();
    std::vector<const DeviceTypeRecord*> base;
    for (uint32_t i = 0; i < numBase; i++)
        base.push_back(table.getRecord(i));
    std::string json = readFile(pCataloguePath);
    DeviceTypeFileLoader::Result result = applyFile(table, json);
    TEST_ASSERT(result.fileValid && (result.rejected == 0), ("catalogue loads: " + std::string(result.firstError.c_str())).c_str());
    TEST_ASSERT(result.replaced == numBase, "every base record replaced");
    uint32_t numDiffer = 0;
    for (uint32_t i = 0; i < numBase; i++)
    {
        const DeviceTypeRecord* pB = base[i];
        const DeviceTypeRecord* pF = table.getRecord(i);
        auto same = [](const char* a, const char* b) { return strcmp(a ? a : "", b ? b : "") == 0; };
        bool equal = pF && (table.getRecordSource(i) == DeviceTypeRecords::RecordSource::Override) &&
                    same(pB->deviceType, pF->deviceType) && same(pB->addresses, pF->addresses) &&
                    same(pB->detectionValues, pF->detectionValues) && same(pB->initValues, pF->initValues) &&
                    same(pB->pollInfo, pF->pollInfo) && sameJson(pB->devInfoJson, pF->devInfoJson) &&
                    (pB->pollDataSizeBytes == pF->pollDataSizeBytes) && (pB->scanPriority == pF->scanPriority) &&
                    (pB->flags == pF->flags) && (pB->identPriority == pF->identPriority) &&
                    (pB->hwTypeId == pF->hwTypeId) && (pB->initialPolicy == pF->initialPolicy);
        if (!equal)
        {
            numDiffer++;
            printf("  record %d %s differs:%s%s%s%s%s%s%s%s%s%s%s\n", (int)i, pB->deviceType,
                        same(pB->addresses, pF->addresses) ? "" : " addresses",
                        same(pB->detectionValues, pF->detectionValues) ? "" : " detectionValues",
                        same(pB->initValues, pF->initValues) ? "" : " initValues",
                        same(pB->pollInfo, pF->pollInfo) ? "" : " pollInfo",
                        sameJson(pB->devInfoJson, pF->devInfoJson) ? "" : " devInfoJson",
                        (pB->pollDataSizeBytes == pF->pollDataSizeBytes) ? "" : " pollDataSizeBytes",
                        (pB->scanPriority == pF->scanPriority) ? "" : " scanPriority",
                        (pB->flags == pF->flags) ? "" : " flags",
                        (pB->identPriority == pF->identPriority) ? "" : " identPriority",
                        (pB->hwTypeId == pF->hwTypeId) ? "" : " hwTypeId",
                        (pB->initialPolicy == pF->initialPolicy) ? "" : " initialPolicy");
            if (!pF)
                printf("  (no record from the file)\n");
        }
    }
    TEST_ASSERT(numDiffer == 0, "every record from the file equals the generated record");

    // So the catalogue index is the same
    numDiffer = 0;
    DeviceTypeRecords fresh;
    for (uint32_t addr = 0; addr <= CatalogueIndex::MAX_ADDR; addr++)
    {
        if (fresh.getIdentCandidates(addr) != table.getIdentCandidates(addr))
        {
            printf("  candidates differ at 0x%02x\n", (int)addr);
            numDiffer++;
        }
    }
    TEST_ASSERT(numDiffer == 0, "identification candidates are the same");
}

// Every record of the catalogue, written as a type file entry (typeinfo full=1) and loaded again, is the same record
static void testRecordToFileEntry()
{
    printf("Running testRecordToFileEntry...\n");
    DeviceTypeRecords source;
    uint32_t numBase = DeviceTypeRecords::getNumBaseRecords();
    String file = "{\"devTypes\":{";
    for (uint32_t i = 0; i < numBase; i++)
    {
        const DeviceTypeRecord* pRec = source.getRecord(i);
        file += String(i > 0 ? "," : "") + DeviceTypeFileLoader::recordToFileEntry(*pRec, source.getPolicy(i));
    }
    file += "}}";
    DeviceTypeRecords table;
    table.setExtendedRecordLimits(1000, 4 * 1024 * 1024);
    DeviceTypeFileLoader::Result result = applyFile(table, file.c_str());
    TEST_ASSERT(result.fileValid && (result.rejected == 0) && (result.replaced == numBase),
                ("exported records load: " + std::string(result.firstError.c_str())).c_str());
    uint32_t numDiffer = 0;
    for (uint32_t i = 0; i < numBase; i++)
    {
        const DeviceTypeRecord* pB = source.getRecord(i);
        const DeviceTypeRecord* pF = table.getRecord(i);
        auto same = [](const char* a, const char* b) { return strcmp(a ? a : "", b ? b : "") == 0; };
        if (!pF || !same(pB->addresses, pF->addresses) || !same(pB->detectionValues, pF->detectionValues) ||
                    !same(pB->initValues, pF->initValues) || !same(pB->pollInfo, pF->pollInfo) ||
                    !same(pB->devInfoJson, pF->devInfoJson) || (pB->pollDataSizeBytes != pF->pollDataSizeBytes) ||
                    (pB->scanPriority != pF->scanPriority) || (pB->flags != pF->flags) ||
                    (pB->identPriority != pF->identPriority) || (pB->hwTypeId != pF->hwTypeId))
        {
            numDiffer++;
            printf("  %s differs after export\n", pB->deviceType);
        }
    }
    TEST_ASSERT(numDiffer == 0, "exported records are the same records");

    // A disabled record exports its policy
    DeviceTypeIndexType idx = 0;
    source.getRecord("MCP9808", idx);
    String entry = DeviceTypeFileLoader::recordToFileEntry(*source.getRecord(idx), DeviceTypeRecord::POLICY_DISABLED_CLAIM);
    TEST_ASSERT(entry.startsWith("\"MCP9808\":{") && (entry.indexOf("\"disable\":\"claim\"") > 0), "policy exported");
}

static void testSetPolicyInFile()
{
    printf("Running testSetPolicyInFile...\n");
    bool changed = false;
    // No file: an entry is added to disable; nothing to enable
    String out = DeviceTypeFileLoader::setPolicyInFile("", "LSM6DS", DeviceTypeRecord::POLICY_DISABLED, changed);
    TEST_ASSERT(changed && (out == "{\"devTypes\":{\"LSM6DS\":{\"disable\":true}}}"), "disable with no file");
    out = DeviceTypeFileLoader::setPolicyInFile(nullptr, "LSM6DS", DeviceTypeRecord::POLICY_ENABLED, changed);
    TEST_ASSERT(!changed && (out == ""), "enable with no file: no change");

    // A file with notes and a full record: other content kept, disable added to the entry and removed again
    const char* file = "{ \"_notes\": \"keep me\", \"devTypes\": { \"Foo\": { \"addresses\": \"0x51\", \"_notes\": \"n\" } } }";
    out = DeviceTypeFileLoader::setPolicyInFile(file, "Foo", DeviceTypeRecord::POLICY_DISABLED_CLAIM, changed);
    TEST_ASSERT(changed && (out == "{\"_notes\":\"keep me\",\"devTypes\":{\"Foo\":{\"addresses\":\"0x51\",\"_notes\":\"n\",\"disable\":\"claim\"}}}"),
                ("claim added: " + std::string(out.c_str())).c_str());
    String again = DeviceTypeFileLoader::setPolicyInFile(out.c_str(), "Foo", DeviceTypeRecord::POLICY_DISABLED_CLAIM, changed);
    TEST_ASSERT(!changed && (again == out), "same policy: no change");
    out = DeviceTypeFileLoader::setPolicyInFile(out.c_str(), "Foo", DeviceTypeRecord::POLICY_ENABLED, changed);
    TEST_ASSERT(changed && (out == "{\"_notes\":\"keep me\",\"devTypes\":{\"Foo\":{\"addresses\":\"0x51\",\"_notes\":\"n\"}}}"),
                "disable removed, record kept");

    // A policy-only entry is removed when enabled; another key is added alongside
    out = DeviceTypeFileLoader::setPolicyInFile("{\"devTypes\":{\"A\":{\"disable\":true},\"B\":{\"disable\":true}}}", "A",
                DeviceTypeRecord::POLICY_ENABLED, changed);
    TEST_ASSERT(changed && (out == "{\"devTypes\":{\"B\":{\"disable\":true}}}"), "policy-only entry removed");
    out = DeviceTypeFileLoader::setPolicyInFile(out.c_str(), "C", DeviceTypeRecord::POLICY_DISABLED, changed);
    TEST_ASSERT(changed && (out == "{\"devTypes\":{\"B\":{\"disable\":true},\"C\":{\"disable\":true}}}"), "entry added");

    // The result loads with the policies it says
    DeviceTypeRecords table;
    DeviceTypeIndexType idx = 0;
    DeviceTypeFileLoader::Result result = applyFile(table,
                DeviceTypeFileLoader::setPolicyInFile("", "MCP9808", DeviceTypeRecord::POLICY_DISABLED, changed).c_str());
    table.getRecord("MCP9808", idx);
    TEST_ASSERT((result.disabled == 1) && (table.getPolicy(idx) == DeviceTypeRecord::POLICY_DISABLED), "written file loads");

    // A file that isn't valid JSON is left alone
    out = DeviceTypeFileLoader::setPolicyInFile("{bad", "X", DeviceTypeRecord::POLICY_DISABLED, changed);
    TEST_ASSERT(!changed && (out == "{bad"), "invalid file not changed");
}

static void testRecordSources()
{
    printf("Running testRecordSources...\n");
    DeviceTypeRecords table;
    TEST_ASSERT(table.areRecordSourcesReady(), "ready with no sources");
    uint32_t fileSource = table.registerRecordSource("typeFile");
    uint32_t codeSource = table.registerRecordSource("code");
    TEST_ASSERT(!table.areRecordSourcesReady(), "not ready while sources are outstanding");
    table.markRecordSourceReady(fileSource);
    TEST_ASSERT(!table.areRecordSourcesReady(), "not ready with one outstanding");
    table.markRecordSourceReady(codeSource);
    TEST_ASSERT(table.areRecordSourcesReady(), "ready when all are");

    // Timeout
    DeviceTypeRecords table2;
    table2.registerRecordSource("slow", 50);
    TEST_ASSERT(!table2.areRecordSourcesReady(), "not ready before the timeout");
    delay(80);
    TEST_ASSERT(table2.areRecordSourcesReady(), "ready after the timeout");
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        printf("Usage: %s <catalogue.json> <example-devlib.json>\n", argv[0]);
        return 1;
    }
    testHelpers();
    testBranchExampleFile(argv[2]);
    testFileLevel();
    testRecordChecks();
    testRecordFields();
    testDuplicateHwTypeId();
    testExtDataSource();
    testRecordBusSpeedWarning();
    testCheckJson();
    testOverridesAndPolicies();
    testCatalogueRoundTrip(argv[1]);
    testRecordSources();
    testRecordToFileEntry();
    testSetPolicyInFile();
    return testSummary("test_device_type_file");
}
