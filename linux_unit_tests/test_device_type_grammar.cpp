/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device type record command grammar tests (DeviceTypeGrammar)
//
// 1. The shared test vectors (scripts/tests/grammar_vectors.json, also run against the Python generator's copy
//    of the grammar).
// 2. Equivalence with the parser it replaced (legacy_device_type_parser.h): for every string the new grammar
//    accepts, the bus requests and checks are the same as before - for the built-in records, every catalogue
//    given on the command line and a fuzzed corpus - except where a change is intended (a bare pNN is a pause;
//    rN in detection compares nothing; :crc8w is new).
// 3. CRC-8 against Sensirion's documented values, and that random strings never crash the compiler.
//
// Usage: test_device_type_grammar <grammar_vectors.json> [catalogue.json ...]
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <chrono>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>
#include "testhdr.h"
#include "ArduinoWString.h"
#include "RaftJson.h"
#include "DeviceTypeGrammar.h"
#include "DeviceTypeRecords.h"
#include "legacy_device_type_parser.h"

typedef DeviceTypeGrammar::Field Field;

static std::string hex(const std::vector<uint8_t>& bytes)
{
    std::string out;
    char buf[4];
    for (uint8_t b : bytes)
    {
        snprintf(buf, sizeof(buf), "%02x", b);
        out += buf;
    }
    return out;
}

static std::string hex(const uint8_t* pBytes, uint32_t len)
{
    return hex(std::vector<uint8_t>(pBytes, pBytes + len));
}

static std::string readFile(const char* path)
{
    std::ifstream file(path);
    std::stringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

static bool hasIssue(const DeviceTypeGrammar::Result& result, DeviceTypeGrammar::IssueCode code)
{
    for (const auto& issue : result.issues)
        if (issue.code == code)
            return true;
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shared vectors
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static Field fieldFromName(const String& name)
{
    return name == "init" ? Field::Init : (name == "poll" ? Field::Poll : Field::Detection);
}

static void testVectors(const char* path)
{
    printf("Running testVectors (%s)...\n", path);
    std::string json = readFile(path);
    TEST_ASSERT(json.length() > 0, "vectors read");
    RaftJson doc(json.c_str());
    std::vector<String> vectors;
    doc.getArrayElems("vectors", vectors);
    TEST_ASSERT(vectors.size() > 20, "vectors present");
    uint32_t failures = 0;
    for (const String& vectorStr : vectors)
    {
        RaftJson vec(vectorStr.c_str());
        String name = vec.getString("name", "");
        String str = vec.getString("str", "");
        DeviceTypeGrammar::Result result = DeviceTypeGrammar::compile(str.c_str(), fieldFromName(vec.getString("field", "")));

        // Issues
        std::vector<String> expIssues;
        vec.getArrayElems("issues", expIssues);
        std::string expIssuesStr, gotIssuesStr;
        for (const String& issue : expIssues)
            expIssuesStr += std::string(issue.c_str()) + " ";
        for (const auto& issue : result.issues)
            gotIssuesStr += std::string(DeviceTypeGrammar::getIssueName(issue.code)) + " ";

        // Steps
        std::vector<String> expSteps;
        vec.getArrayElems("steps", expSteps);
        std::string expStepsStr, gotStepsStr;
        for (const String& stepStr : expSteps)
        {
            RaftJson step(stepStr.c_str());
            std::vector<String> checks;
            step.getArrayElems("checks", checks);
            expStepsStr += std::string("[w") + step.getString("write", "").c_str() + " r" + std::to_string(step.getInt("read", 0)) +
                    " p" + std::to_string(step.getInt("pause", 0)) + (step.getBool("crc8w", false) ? " crc" : "") +
                    (step.getBool("expr", false) ? " expr" : "");
            for (const String& check : checks)
                expStepsStr += std::string(" ") + check.c_str();
            expStepsStr += "]";
        }
        for (const auto& step : result.steps)
        {
            gotStepsStr += "[w" + hex(step.write) + " r" + std::to_string(step.readLen) + " p" + std::to_string(step.pauseMs) +
                    (step.crc8w ? " crc" : "") + (step.readLenExpr ? " expr" : "");
            for (const auto& check : step.checks)
                gotStepsStr += " " + hex(check.mask) + ":" + hex(check.value);
            gotStepsStr += "]";
        }
        if ((expIssuesStr != gotIssuesStr) || (expStepsStr != gotStepsStr))
        {
            failures++;
            printf("  vector \"%s\" (%s):\n    expected %s issues %s\n    got      %s issues %s\n", name.c_str(), str.c_str(),
                        expStepsStr.c_str(), expIssuesStr.c_str(), gotStepsStr.c_str(), gotIssuesStr.c_str());
        }
    }
    TEST_ASSERT(failures == 0, "all vectors match");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Equivalence with the previous parser
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct EquivStats
{
    uint32_t compared = 0;
    uint32_t rejectedNow = 0;       // the previous parser accepted it, the grammar reports an error
    uint32_t intendedDiffs = 0;     // bare pause, rN in detection, :crc8w
    uint32_t mismatches = 0;
};

// Compare one string. Returns false on a mismatch (and prints it)
static bool compareWithLegacy(const std::string& str, Field field, EquivStats& stats, bool printMismatch)
{
    DeviceTypeGrammar::Result result = DeviceTypeGrammar::compile(str.c_str(), field);
    std::string lc = str;
    for (char& c : lc)
        c = (char)tolower((unsigned char)c);
    bool intended = hasIssue(result, DeviceTypeGrammar::IssueCode::BarePause) || (lc.find(":crc8w") != std::string::npos) ||
            ((field == Field::Detection) && (lc.find('r') != std::string::npos));
    if (intended)
    {
        stats.intendedDiffs++;
        return true;
    }

    DeviceTypeRecord rec;
    rec.deviceType = "Test";
    std::string legacyStr, newStr;
    bool legacyOk = true;
    if (field == Field::Detection)
    {
        rec.detectionValues = str.c_str();
        std::vector<DeviceTypeRecords::DeviceDetectionRec> legacyRecs;
        legacyOk = LegacyParser::getDetectionRecs(&rec, legacyRecs);
        for (const auto& r : legacyRecs)
        {
            legacyStr += "[w" + hex(r.writeData) + " p" + std::to_string(r.pauseAfterSendMs);
            for (const auto& cv : r.checkValues)
                legacyStr += " " + hex(cv.first) + ":" + hex(cv.second);
            legacyStr += "]";
        }
        if (!result.hasErrors())
        {
            for (const auto& step : result.steps)
            {
                newStr += "[w" + hex(step.write) + " p" + std::to_string(step.pauseMs);
                for (const auto& check : step.checks)
                    newStr += " " + hex(check.mask) + ":" + hex(check.value);
                newStr += "]";
            }
        }
    }
    else if (field == Field::Init)
    {
        rec.initValues = str.c_str();
        std::vector<BusRequestInfo> legacyReqs;
        LegacyParser::getInitBusRequests(0x10, &rec, legacyReqs);
        for (auto& r : legacyReqs)
            legacyStr += "[w" + hex(r.getWriteData(), r.getWriteDataLen()) + " r" + std::to_string(r.getReadReqLen()) +
                    " p" + std::to_string(r.getBarAccessForMsAfterSend()) + "]";
        for (const auto& step : result.steps)
            newStr += "[w" + hex(step.write) + " r" + std::to_string(step.readLen) + " p" + std::to_string(step.pauseMs) + "]";
    }
    else
    {
        std::vector<BusRequestInfo> legacyReqs;
        uint16_t legacySize = 0;
        LegacyParser::getPollReqs(0x10, String(str.c_str()), legacyReqs, legacySize);
        for (auto& r : legacyReqs)
            legacyStr += "[w" + hex(r.getWriteData(), r.getWriteDataLen()) + " r" + std::to_string(r.getReadReqLen()) +
                    " p" + std::to_string(r.getBarAccessForMsAfterSend()) + (r.hasDynamicReadLen() ? " expr" : "") + "]";
        legacyStr += " size" + std::to_string(legacySize);
        uint32_t newSize = 0;
        for (const auto& step : result.steps)
        {
            newStr += "[w" + hex(step.write) + " r" + std::to_string(step.readLen) + " p" + std::to_string(step.pauseMs) +
                    (step.readLenExpr ? " expr" : "") + "]";
            newSize += step.readLen;
        }
        newStr += " size" + std::to_string(newSize);
    }

    if (result.hasErrors())
    {
        // Rejected (detection) or a step dropped (init/poll): fine if the previous parser also rejected or
        // dropped it; otherwise counted - the previous parser turned it into garbage
        if ((legacyStr != newStr) || ((field == Field::Detection) && legacyOk))
            stats.rejectedNow++;
        else
            stats.compared++;
        return true;
    }
    stats.compared++;
    if ((field == Field::Detection) && !legacyOk)
        legacyStr = "(rejected)";
    if (legacyStr != newStr)
    {
        stats.mismatches++;
        if (printMismatch)
            printf("  MISMATCH %s \"%s\"\n    before %s\n    now    %s\n", DeviceTypeGrammar::getFieldName(field), str.c_str(),
                        legacyStr.c_str(), newStr.c_str());
        return false;
    }
    return true;
}

static void compareRecord(const char* detection, const char* init, const char* poll, EquivStats& stats)
{
    if (detection && detection[0])
        compareWithLegacy(detection, Field::Detection, stats, true);
    if (init && init[0])
        compareWithLegacy(init, Field::Init, stats, true);
    if (poll && poll[0])
        compareWithLegacy(poll, Field::Poll, stats, true);
}

static void testBuiltInEquivalence()
{
    printf("Running testBuiltInEquivalence...\n");
    EquivStats stats;
    DeviceTypeRecords table;
    for (uint32_t idx = 0; idx < DeviceTypeRecords::getNumBaseRecords(); idx++)
    {
        const DeviceTypeRecord* pRec = table.getRecord(idx);
        String poll = RaftJson(pRec->pollInfo).getString("c", "");
        compareRecord(pRec->detectionValues, pRec->initValues, poll.c_str(), stats);
    }
    printf("  %u strings compared, %u now rejected, %u intended differences\n", stats.compared, stats.rejectedNow, stats.intendedDiffs);
    TEST_ASSERT(stats.mismatches == 0, "built-in records unchanged");
    TEST_ASSERT(stats.rejectedNow == 0, "no built-in record is rejected");
}

static void testCatalogueEquivalence(const char* path)
{
    printf("Running testCatalogueEquivalence (%s)...\n", path);
    std::string json = readFile(path);
    RaftJson catalogue(json.c_str());
    std::vector<String> keys;
    if (!catalogue.getKeys("devTypes", keys))
    {
        testFail(path, "no devTypes");
        return;
    }
    EquivStats stats;
    for (const String& key : keys)
    {
        String base = "devTypes/" + key + "/";
        String detection = catalogue.getString((base + "detectionValues").c_str(), "");
        String init = catalogue.getString((base + "initValues").c_str(), "");
        String poll = catalogue.getString((base + "pollInfo/c").c_str(), "");
        compareRecord(detection.c_str(), init.c_str(), poll.c_str(), stats);
    }
    printf("  %u records: %u strings compared, %u now rejected, %u intended differences\n", (unsigned)keys.size(),
                stats.compared, stats.rejectedNow, stats.intendedDiffs);
    TEST_ASSERT(stats.mismatches == 0, "catalogue records unchanged");
    TEST_ASSERT(stats.rejectedNow == 0, "no catalogue record is rejected");
}

// Random strings from the grammar's alphabet: the compiler never crashes, and what it accepts is what the
// previous parser produced
static void testFuzz()
{
    printf("Running testFuzz...\n");
    static const char* ALPHABET = "0123456789abcdefABCDEFxXbBpPrR=&;, {}$.:wmask*";
    static const char* TOKENS[] = { "0x", "0b", "=", "&", ";", ",", "p10", "r2", "0x0f", "0b1x0x", "=p25", "r{$0.w12:max8}",
                ":crc8w", " " };
    std::mt19937 rng(12345);
    EquivStats stats;
    uint32_t printed = 0;
    const uint32_t NUM = 200000;
    for (uint32_t n = 0; n < NUM; n++)
    {
        std::string str;
        uint32_t len = rng() % 24;
        for (uint32_t i = 0; i < len; i++)
        {
            if (rng() % 3 == 0)
                str += TOKENS[rng() % (sizeof(TOKENS) / sizeof(TOKENS[0]))];
            else
                str += ALPHABET[rng() % strlen(ALPHABET)];
        }
        Field field = (Field)(rng() % 3);
        if (!compareWithLegacy(str, field, stats, printed < 10))
            printed++;
    }
    printf("  %u strings: %u compared, %u now rejected, %u intended differences, %u mismatches\n", NUM, stats.compared,
                stats.rejectedNow, stats.intendedDiffs, stats.mismatches);
    TEST_ASSERT(stats.mismatches == 0, "fuzzed strings the grammar accepts are unchanged");
    TEST_ASSERT(stats.compared > NUM / 10, "fuzz compared a useful number of strings");
}

// How long identification spends getting a record's detection steps: parsing the string every time (as before)
// against using the compiled record (reported, not checked - the numbers depend on the host)
static void reportParseCost()
{
    printf("Running reportParseCost...\n");
    DeviceTypeRecords table;
    const uint32_t numBase = DeviceTypeRecords::getNumBaseRecords();
    const uint32_t REPEATS = 200;
    std::vector<DeviceTypeRecords::DeviceDetectionRec> recs;
    auto t0 = std::chrono::steady_clock::now();
    for (uint32_t r = 0; r < REPEATS; r++)
        for (uint32_t idx = 0; idx < numBase; idx++)
            LegacyParser::getDetectionRecs(table.getRecord(idx), recs);
    auto t1 = std::chrono::steady_clock::now();
    for (uint32_t r = 0; r < REPEATS; r++)
        for (uint32_t idx = 0; idx < numBase; idx++)
            table.getDetectionRecs((DeviceTypeIndexType)idx, recs);
    auto t2 = std::chrono::steady_clock::now();
    double legacyUs = std::chrono::duration<double, std::micro>(t1 - t0).count() / (REPEATS * numBase);
    double compiledUs = std::chrono::duration<double, std::micro>(t2 - t1).count() / (REPEATS * numBase);
    printf("  detection steps per record: parsed each time %.2f us, from the compiled record %.2f us\n", legacyUs, compiledUs);
}

static void testCrc()
{
    printf("Running testCrc...\n");
    // Sensirion's documented example: CRC(0xBEEF) = 0x92
    const uint8_t beef[] = { 0xbe, 0xef };
    TEST_ASSERT(DeviceTypeGrammar::crc8(beef, 2) == 0x92, "crc of 0xbeef");
    // STCC4 product ID 0x0901018A as it is read: 09 01 73 01 8a d4
    const uint8_t productId[] = { 0x09, 0x01, 0x73, 0x01, 0x8a, 0xd4 };
    TEST_ASSERT(DeviceTypeGrammar::checkCrc8Words(productId, 6), "valid words");
    const uint8_t corrupt[] = { 0x09, 0x01, 0x73, 0x01, 0x8b, 0xd4 };
    TEST_ASSERT(!DeviceTypeGrammar::checkCrc8Words(corrupt, 6), "corrupt word");
    TEST_ASSERT(!DeviceTypeGrammar::checkCrc8Words(productId, 5), "length not a multiple of 3");
}

// --dump: compile "<field>\t<string>" lines from stdin and print each result as one line (used by the Python
// grammar test to check that the two implementations agree)
static int dumpMode()
{
    std::string line;
    while (std::getline(std::cin, line))
    {
        size_t tab = line.find('\t');
        if (tab == std::string::npos)
            continue;
        DeviceTypeGrammar::Result result = DeviceTypeGrammar::compile(line.substr(tab + 1).c_str(),
                    fieldFromName(String(line.substr(0, tab).c_str())));
        std::string out;
        for (const auto& step : result.steps)
        {
            out += "[w" + hex(step.write) + " r" + std::to_string(step.readLen) + " p" + std::to_string(step.pauseMs) +
                    (step.crc8w ? " crc" : "") + (step.readLenExpr ? " expr" : "");
            for (const auto& check : step.checks)
                out += " " + hex(check.mask) + ":" + hex(check.value);
            out += "]";
        }
        out += " |";
        for (const auto& issue : result.issues)
            out += std::string(" ") + DeviceTypeGrammar::getIssueName(issue.code) + "@" + std::to_string(issue.pos);
        printf("%s\n", out.c_str());
    }
    return 0;
}

int main(int argc, char** argv)
{
    if ((argc > 1) && (strcmp(argv[1], "--dump") == 0))
        return dumpMode();
    if (argc > 1)
        testVectors(argv[1]);
    else
        testFail("testVectors", "no vectors file given");
    testCrc();
    testBuiltInEquivalence();
    for (int i = 2; i < argc; i++)
        testCatalogueEquivalence(argv[i]);
    testFuzz();
    reportParseCost();
    return testSummary("test_device_type_grammar");
}
