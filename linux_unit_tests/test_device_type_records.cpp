/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device type record detection value parsing tests
//
// Runs detection values through the real DeviceTypeRecords parser (getDetectionRecs) and checks the
// resulting write bytes, check masks, check values and pauses.
//
// 1. Explicit cases with expected bytes worked out by hand, including a regression case for each
//    shipped record broken by the binary-as-hex bug (a binary value such as 0b1010XXXX contains "0x"
//    where a 0 bit is followed by a don't-care bit, and was parsed as hex).
// 2. A sweep of every built-in record (compiled from devtypes/DeviceTypeRecords.json by the real
//    generator) plus any catalogue JSON files given on the command line. Every detection value must
//    parse, and every 0x/0b value must decode to exactly what an independent reference decoder says.
//
// Usage: test_device_type_records [catalogue.json ...]
// Exit code is non-zero if any test fails.
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <string.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "testhdr.h"
#include "DeviceTypeRecords.h"
#include "RaftJson.h"

typedef std::vector<uint8_t> Bytes;

static std::string bytesToHex(const Bytes& bytes)
{
    std::string out;
    char buf[4];
    for (uint8_t b : bytes)
    {
        snprintf(buf, sizeof(buf), "%02x", b);
        out += buf;
    }
    return out.empty() ? "<none>" : out;
}

static void fail(const std::string& context, const std::string& msg)
{
    testFail(context.c_str(), msg.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Expected parse of one write=check step
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct ExpectedCheck
{
    Bytes mask;
    Bytes value;
};

struct ExpectedStep
{
    Bytes writeData;
    std::vector<ExpectedCheck> checks;
    uint16_t pauseAfterSendMs;
};

struct ParseCase
{
    const char* name;
    const char* detectionValues;
    bool expectValid;
    std::vector<ExpectedStep> steps;
};

static bool parseDetection(const char* detectionValues, std::vector<DeviceTypeRecords::DeviceDetectionRec>& recs)
{
    DeviceTypeRecord rec;
    rec.deviceType = "Test";
    rec.detectionValues = detectionValues;
    return deviceTypeRecords.getDetectionRecs(&rec, recs);
}

static void runParseCase(const ParseCase& tc)
{
    testCheckCount++;
    std::string context = std::string(tc.name) + " \"" + tc.detectionValues + "\"";
    std::vector<DeviceTypeRecords::DeviceDetectionRec> recs;
    bool valid = parseDetection(tc.detectionValues, recs);
    if (valid != tc.expectValid)
    {
        fail(context, std::string("expected ") + (tc.expectValid ? "valid" : "invalid") + " got " + (valid ? "valid" : "invalid"));
        return;
    }
    if (!valid)
        return;
    if (recs.size() != tc.steps.size())
    {
        fail(context, "expected " + std::to_string(tc.steps.size()) + " steps got " + std::to_string(recs.size()));
        return;
    }
    for (size_t i = 0; i < recs.size(); i++)
    {
        const auto& got = recs[i];
        const auto& exp = tc.steps[i];
        std::string stepCtx = context + " step " + std::to_string(i);
        if (got.writeData != exp.writeData)
            fail(stepCtx, "write " + bytesToHex(got.writeData) + " expected " + bytesToHex(exp.writeData));
        if (got.pauseAfterSendMs != exp.pauseAfterSendMs)
            fail(stepCtx, "pause " + std::to_string(got.pauseAfterSendMs) + " expected " + std::to_string(exp.pauseAfterSendMs));
        if (got.checkValues.size() != exp.checks.size())
        {
            fail(stepCtx, "expected " + std::to_string(exp.checks.size()) + " alternatives got " + std::to_string(got.checkValues.size()));
            continue;
        }
        for (size_t j = 0; j < exp.checks.size(); j++)
        {
            const auto& gotMask = got.checkValues[j].first;
            const auto& gotValue = got.checkValues[j].second;
            if ((gotMask != exp.checks[j].mask) || (gotValue != exp.checks[j].value))
                fail(stepCtx + " alt " + std::to_string(j),
                     "mask " + bytesToHex(gotMask) + " value " + bytesToHex(gotValue) +
                     " expected mask " + bytesToHex(exp.checks[j].mask) + " value " + bytesToHex(exp.checks[j].value));
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Explicit cases
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void testExplicitCases()
{
    printf("Running explicit detection value cases...\n");
    const std::vector<ParseCase> cases = {

        // Regression: binary values containing "0x" (a 0 bit followed by a don't-care bit). Each was
        // parsed as hex from the middle of the value, with 'x' read as the digit 8
        {"MCP9808", "0x06=0b0000000001010100&0x07=0b00000100XXXXXXXX", true, {
            {{0x06}, {{{0xff, 0xff}, {0x00, 0x54}}}, 0},
            {{0x07}, {{{0xff, 0x00}, {0x04, 0x00}}}, 0}}},
        {"VCNL4040", "0x0c=0b100001100000XXXX", true, {
            {{0x0c}, {{{0xff, 0xf0}, {0x86, 0x00}}}, 0}}},
        {"M5Encoder", "0x20=0b0000000X11111111", true, {
            {{0x20}, {{{0xfe, 0xff}, {0x00, 0xff}}}, 0}}},
        {"AS5600", "0x00=0b000000xx", true, {
            {{0x00}, {{{0xfc}, {0x00}}}, 0}}},
        {"LTR-329", "0x86=0b1010XXXX&0x87=0x05", true, {
            {{0x86}, {{{0xf0}, {0xa0}}}, 0},
            {{0x87}, {{{0xff}, {0x05}}}, 0}}},
        {"MPU6050 binary with trigger", "0x75=0b0110100X", true, {
            {{0x75}, {{{0xfe}, {0x68}}}, 0}}},

        // Binary values that never contained the trigger must be unchanged
        {"LTR-390", "0x06=0b1011XXXX", true, {
            {{0x06}, {{{0xf0}, {0xb0}}}, 0}}},
        {"Binary all don't-care leading, no write (TrillBar)", "=0bXXXXXXXX00000001", true, {
            {{}, {{{0x00, 0xff}, {0x00, 0x01}}}, 0}}},
        {"Binary lower and upper case x", "0x01=0b1x0X1x0X", true, {
            {{0x01}, {{{0xaa}, {0x88}}}, 0}}},

        // Hex values
        {"Hex single", "0x75=0x68", true, {
            {{0x75}, {{{0xff}, {0x68}}}, 0}}},
        {"Hex upper case", "0x92=0xAB", true, {
            {{0x92}, {{{0xff}, {0xab}}}, 0}}},
        {"Hex alternatives", "0x92=0xab,0xa8,0x9c", true, {
            {{0x92}, {{{0xff}, {0xab}}, {{0xff}, {0xa8}}, {{0xff}, {0x9c}}}, 0}}},
        {"Hex multi-byte", "0x07=0x81c4,0x81d4", true, {
            {{0x07}, {{{0xff, 0xff}, {0x81, 0xc4}}, {{0xff, 0xff}, {0x81, 0xd4}}}, 0}}},
        {"Hex and binary alternatives mixed", "0x0f=0x33,0b0011010X", true, {
            {{0x0f}, {{{0xff}, {0x33}}, {{0xfe}, {0x34}}}, 0}}},
        {"Multi-byte write", "0x0001=0x55", true, {
            {{0x00, 0x01}, {{{0xff}, {0x55}}}, 0}}},

        // Pauses
        {"Pause only", "=p250", true, {
            {{}, {{{}, {}}}, 250}}},
        {"Write then pause", "0x0001=p25", true, {
            {{0x00, 0x01}, {{{}, {}}}, 25}}},
        {"Leading pause before hex", "0x10=p10 0x55", true, {
            {{0x10}, {{{0xff}, {0x55}}}, 10}}},
        {"Leading pause before binary", "0x10=p10 0b0101XXXX", true, {
            {{0x10}, {{{0xf0}, {0x50}}}, 10}}},
        {"Pause step then check step", "0x00FF=p25&0x01=0x80", true, {
            {{0x00, 0xff}, {{{}, {}}}, 25},
            {{0x01}, {{{0xff}, {0x80}}}, 0}}},

        // Invalid values must invalidate the whole record
        {"Unprefixed value (C3)", "0x75=FF", false, {}},
        {"Unprefixed value after valid step", "0x75=0x68&0x76=A005", false, {}},
        {"Zero-length read", "0x75=r0", false, {}},
    };
    for (const auto& tc : cases)
        runParseCase(tc);

    // Read requests: only the length is checked here, as the semantics of rN in detection are not
    // yet defined (see devdocs/device-identification-review.md C10)
    testCheckCount++;
    std::vector<DeviceTypeRecords::DeviceDetectionRec> recs;
    if (!parseDetection("0x00=r2", recs) || (recs.size() != 1) || (recs[0].checkValues.size() != 1) ||
            (recs[0].checkValues[0].first.size() != 2) || (recs[0].checkValues[0].second.size() != 2))
        fail("Read request \"0x00=r2\"", "expected one step with a 2-byte check");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Reference decoder for the catalogue sweep
// Deliberately independent of the parser under test: written from the grammar, not from the code
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static std::string lowerTrim(const std::string& in)
{
    size_t start = in.find_first_not_of(" \t");
    size_t end = in.find_last_not_of(" \t");
    std::string out = (start == std::string::npos) ? "" : in.substr(start, end - start + 1);
    for (auto& c : out)
        c = tolower(c);
    return out;
}

static std::vector<std::string> split(const std::string& in, const std::string& seps)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : in)
    {
        if (seps.find(c) != std::string::npos)
        {
            out.push_back(cur);
            cur.clear();
        }
        else
        {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

enum class RefKind { Hex, Binary, Other };

// Decode one alternative of a check value. Returns Other for forms the reference does not cover
// (read requests, pauses, odd-length hex) - those are only required to parse
static RefKind refDecode(const std::string& altIn, Bytes& mask, Bytes& value)
{
    std::string alt = lowerTrim(altIn);
    // Strip an optional leading pause
    if (!alt.empty() && (alt[0] == 'p'))
    {
        size_t i = 1;
        while ((i < alt.size()) && isdigit((unsigned char)alt[i]))
            i++;
        alt = lowerTrim(alt.substr(i));
    }
    mask.clear();
    value.clear();
    if (alt.rfind("0x", 0) == 0)
    {
        std::string hex = alt.substr(2);
        if ((hex.size() % 2) != 0)
            return RefKind::Other;
        for (size_t i = 0; i < hex.size(); i += 2)
        {
            mask.push_back(0xff);
            value.push_back((uint8_t)strtoul(hex.substr(i, 2).c_str(), nullptr, 16));
        }
        return RefKind::Hex;
    }
    if (alt.rfind("0b", 0) == 0)
    {
        std::string bits = alt.substr(2);
        for (size_t i = 0; i < bits.size(); i++)
        {
            if ((i % 8) == 0)
            {
                mask.push_back(0);
                value.push_back(0);
            }
            uint8_t bit = 0x80 >> (i % 8);
            if (bits[i] != 'x')
                mask.back() |= bit;
            if (bits[i] == '1')
                value.back() |= bit;
        }
        return RefKind::Binary;
    }
    return RefKind::Other;
}

static void checkDetectionValues(const std::string& catalogue, const std::string& deviceType, const std::string& detectionValues)
{
    if (detectionValues.empty())
        return;
    testCheckCount++;
    std::string context = catalogue + " " + deviceType + " \"" + detectionValues + "\"";
    std::vector<DeviceTypeRecords::DeviceDetectionRec> recs;
    if (!parseDetection(detectionValues.c_str(), recs))
    {
        fail(context, "detection values do not parse");
        return;
    }
    std::vector<std::string> steps = split(detectionValues, "&;");
    if (steps.size() != recs.size())
    {
        fail(context, "expected " + std::to_string(steps.size()) + " steps got " + std::to_string(recs.size()));
        return;
    }
    for (size_t i = 0; i < steps.size(); i++)
    {
        size_t eqPos = steps[i].find('=');
        if (eqPos == std::string::npos)
            continue;
        std::vector<std::string> alts = split(steps[i].substr(eqPos + 1), ",");
        if (alts.size() != recs[i].checkValues.size())
        {
            fail(context, "step " + std::to_string(i) + " expected " + std::to_string(alts.size()) +
                    " alternatives got " + std::to_string(recs[i].checkValues.size()));
            continue;
        }
        for (size_t j = 0; j < alts.size(); j++)
        {
            // The :crc8w suffix (a CRC check on the bytes read) isn't part of the value
            std::string alt = alts[j];
            size_t crcPos = alt.find(":crc8w");
            if (crcPos != std::string::npos)
                alt = alt.substr(0, crcPos);
            Bytes refMask, refValue;
            if (refDecode(alt, refMask, refValue) == RefKind::Other)
                continue;
            const Bytes& gotMask = recs[i].checkValues[j].first;
            const Bytes& gotValue = recs[i].checkValues[j].second;
            if ((gotMask != refMask) || (gotValue != refValue))
                fail(context, "step " + std::to_string(i) + " alt \"" + alts[j] + "\" parsed as mask " +
                        bytesToHex(gotMask) + " value " + bytesToHex(gotValue) + ", expected mask " +
                        bytesToHex(refMask) + " value " + bytesToHex(refValue));
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Poll info defaults and limits
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static DevicePollingInfo getPollInfoFor(const char* pollInfoJson)
{
    DeviceTypeRecord rec;
    rec.deviceType = "Test";
    rec.pollInfo = pollInfoJson;
    rec.pollDataSizeBytes = 2;
    DevicePollingInfo pollingInfo;
    deviceTypeRecords.getPollInfo(0x40, &rec, pollingInfo);
    return pollingInfo;
}

static void testPollInfoDefaults()
{
    printf("Running testPollInfoDefaults...\n");

    // Interval from the record (ms) and number of results
    DevicePollingInfo info = getPollInfoFor(R"({"c":"0x00=r2","i":50,"s":3})");
    TEST_ASSERT(info.pollReqs.size() == 1, "one poll request");
    TEST_ASSERT(info.pollIntervalUs == 50000, "interval from record");
    TEST_ASSERT(info.numPollResultsToStore == 3, "results from record");

    // Missing interval: the default, not 0 (which would poll on every pass); missing s: 1
    info = getPollInfoFor(R"({"c":"0x00=r2"})");
    TEST_ASSERT(info.pollIntervalUs == DevicePollingInfo::POLL_INTERVAL_DEFAULT_US, "missing interval gets default");
    TEST_ASSERT(info.numPollResultsToStore == 1, "missing s gets 1");

    // Interval below the minimum is clamped; s below 1 is clamped
    info = getPollInfoFor(R"({"c":"0x00=r2","i":0,"s":0})");
    TEST_ASSERT(info.pollIntervalUs == DevicePollingInfo::POLL_INTERVAL_MIN_US, "zero interval clamped to minimum");
    TEST_ASSERT(info.numPollResultsToStore == 1, "zero s clamped to 1");
    info = getPollInfoFor(R"({"c":"0x00=r2","i":1})");
    TEST_ASSERT(info.pollIntervalUs == DevicePollingInfo::POLL_INTERVAL_MIN_US, "1ms interval clamped to minimum");

    // Externally supplied records are not polled, but still store at least one result
    info = getPollInfoFor(R"({"ext":1,"s":0})");
    TEST_ASSERT(info.pollReqs.size() == 0, "ext record has no poll requests");
    TEST_ASSERT(info.numPollResultsToStore == 1, "ext record stores at least one result");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Catalogue sweeps
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void testBuiltInRecords()
{
    printf("Running sweep of built-in device type records...\n");
    uint32_t numRecords = 0;
    DeviceTypeRecord rec;
    for (uint16_t idx = 0; deviceTypeRecords.getDeviceInfo(idx, rec); idx++)
    {
        numRecords++;
        checkDetectionValues("built-in", rec.deviceType ? rec.deviceType : "?",
                    rec.detectionValues ? rec.detectionValues : "");
    }
    if (numRecords == 0)
        fail("built-in", "no built-in records found");
    printf("  %u built-in records checked\n", numRecords);
}

static void testCatalogueFile(const char* path)
{
    printf("Running sweep of catalogue %s...\n", path);
    std::ifstream file(path);
    if (!file)
    {
        fail(path, "cannot open file");
        return;
    }
    std::stringstream contents;
    contents << file.rdbuf();
    RaftJson catalogue(contents.str().c_str());
    std::vector<String> deviceTypes;
    if (!catalogue.getKeys("devTypes", deviceTypes) || deviceTypes.empty())
    {
        fail(path, "no devTypes found");
        return;
    }
    for (const String& deviceType : deviceTypes)
    {
        String detectionValues = catalogue.getString(("devTypes/" + deviceType + "/detectionValues").c_str(), "");
        checkDetectionValues(path, deviceType.c_str(), detectionValues.c_str());
    }
    printf("  %u records checked\n", (unsigned)deviceTypes.size());
}

// Init values: a record's initCode computes the values for its init string's {name} value slots from the bytes the
// earlier init steps read. The BME680 and BME690 records compute the gas heater set-point from their calibration -
// here from the calibration read from two real units, whose values Bosch's formula gives as 0x6E and 0x69
static void testInitValues()
{
    printf("Running testInitValues...\n");
    struct Case
    {
        const char* key;
        std::vector<uint8_t> read0xEB;      // par_g2 (2 bytes), par_g1, par_g3
        std::vector<uint8_t> read0x00;      // res_heat_val, -, res_heat_range (bits 5:4)
        uint8_t expected;
    };
    const Case cases[] = {
        {"BME680", {0x9b, 0xc4, 0xc5, 0x12}, {0x21, 0xaa, 0x16}, 0x6e},
        {"BME690", {0x19, 0xf1, 0xbe, 0x12}, {0x3b, 0xaa, 0x16}, 0x69},
    };
    for (const Case& c : cases)
    {
        DeviceTypeIndexType idx = DEVICE_TYPE_INDEX_INVALID;
        const DeviceTypeRecord* pRec = deviceTypeRecords.getRecord(c.key, idx);
        if (!pRec)
        {
            fail(c.key, "record not built in");
            continue;
        }
        TEST_ASSERT(pRec->initValuesFn && pRec->initValueNames && (strcmp(pRec->initValueNames, "heat") == 0),
                    "init values function and names generated");

        // Find the value slot step; the steps before it are unchanged
        std::shared_ptr<const DeviceTypeRecords::CompiledRecord> pCompiled = deviceTypeRecords.getCompiledRecord(idx);
        uint32_t slotStep = 0;
        while ((slotStep < pCompiled->init.steps.size()) && !deviceTypeRecords.initStepHasValueSlots(idx, slotStep))
            slotStep++;
        TEST_ASSERT(slotStep < pCompiled->init.steps.size(), "a step has a value slot");
        DeviceTypeRecords::InitValueState state;
        std::vector<uint8_t> write;
        TEST_ASSERT(deviceTypeRecords.getInitStepWrite(idx, 0, state, write) && (write == pCompiled->init.steps[0].write),
                    "a step without slots is written as compiled");

        // Too few bytes read: the values can't be computed
        DeviceTypeRecords::InitValueState shortState;
        DeviceTypeRecords::noteInitStepRead(shortState, c.read0xEB);
        TEST_ASSERT(!deviceTypeRecords.getInitStepWrite(idx, slotStep, shortState, write), "too few bytes read fails");

        // The calibration read: the heater set-point is computed and written
        DeviceTypeRecords::noteInitStepRead(state, c.read0xEB);
        DeviceTypeRecords::noteInitStepRead(state, c.read0x00);
        bool ok = deviceTypeRecords.getInitStepWrite(idx, slotStep, state, write);
        TEST_ASSERT(ok && (write.size() == 2) && (write[0] == 0x5a) && (write[1] == c.expected), "heater set-point written");
        printf("  %s: res_heat_0 = 0x%02x (expected 0x%02x)\n", c.key, write.size() > 1 ? write[1] : 0, c.expected);

        // Bytes read after the values are computed aren't kept
        DeviceTypeRecords::noteInitStepRead(state, {0x01, 0x02});
        TEST_ASSERT(state.readData.size() == 7, "reads after computing aren't kept");
    }
}

int main(int argc, char** argv)
{
    testExplicitCases();
    testPollInfoDefaults();
    testBuiltInRecords();
    testInitValues();
    for (int i = 1; i < argc; i++)
        testCatalogueFile(argv[i]);

    return testSummary("test_device_type_records");
}
