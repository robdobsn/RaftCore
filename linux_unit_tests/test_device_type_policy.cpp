/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device type record policy and override tests (Stage 5)
//
// - A DISABLED record is not an identification candidate (the next candidate is) and reserves nothing.
// - A DISABLED_CLAIM record is not a candidate, but its address stays reserved and the identification hook is
//   blocked there.
// - A protected type can't be disabled, overridden or replaced.
// - Before Ready, a record with a base record's key replaces it at its index (one index per key); the decoder is
//   inherited when the response layout is the same; a second, different override is a conflict. After Ready it
//   gets a new index.
// - Policy changes and overrides increase the generation, and the compiled record follows an override.
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <algorithm>
#include <string>
#include <vector>
#include "testhdr.h"
#include "ArduinoWString.h"
#include "RaftJson.h"
#include "DeviceTypeRecords.h"

static DeviceTypeRecordDynamic makeRecord(const char* key, const char* addresses, const char* detection = "0x00=0x12",
            const char* devInfoJson = R"({"name":"Test","resp":{"b":2,"a":[]}})")
{
    return DeviceTypeRecordDynamic(key, addresses, detection, "", R"({"c":"0x00=r2","i":100,"s":1})", 2, devInfoJson, nullptr);
}

static bool contains(const std::vector<DeviceTypeIndexType>& v, DeviceTypeIndexType idx)
{
    return std::find(v.begin(), v.end(), idx) != v.end();
}

// An address with at least two base candidates
static uint32_t findSharedAddr(const DeviceTypeRecords& table)
{
    for (uint32_t addr = 0; addr <= CatalogueIndex::MAX_ADDR; addr++)
        if (table.getIdentCandidates(addr).size() >= 2)
            return addr;
    return 0;
}

// An address with exactly one base candidate
static uint32_t findSingleAddr(const DeviceTypeRecords& table)
{
    for (uint32_t addr = 0; addr <= CatalogueIndex::MAX_ADDR; addr++)
        if (table.getIdentCandidates(addr).size() == 1)
            return addr;
    return 0;
}

static void testDisable()
{
    printf("Running testDisable...\n");
    DeviceTypeRecords table;
    uint32_t addr = findSharedAddr(table);
    TEST_ASSERT(addr != 0, "shared address");
    std::vector<DeviceTypeIndexType> before = table.getIdentCandidates(addr);
    DeviceTypeIndexType first = before[0];
    uint32_t gen = table.getGeneration();

    TEST_ASSERT(table.setPolicy(first, DeviceTypeRecord::POLICY_DISABLED) == DeviceTypeRecords::PolicyResult::Set, "disable");
    TEST_ASSERT(table.getGeneration() > gen, "generation increased");
    TEST_ASSERT(table.getPolicy(first) == DeviceTypeRecord::POLICY_DISABLED, "policy reported");
    std::vector<DeviceTypeIndexType> after = table.getIdentCandidates(addr);
    TEST_ASSERT(!contains(after, first) && (after.size() == before.size() - 1) && (after[0] == before[1]),
                "the next candidate is first");
    TEST_ASSERT(!contains(table.getKnownTypesAtAddr(addr), first), "a disabled record isn't known at the address");
    TEST_ASSERT(table.getRecord(first) != nullptr, "the record itself is still there (typeinfo, decoding)");

    // Disabling the only record at an address frees the address
    uint32_t singleAddr = findSingleAddr(table);
    DeviceTypeIndexType only = table.getIdentCandidates(singleAddr)[0];
    TEST_ASSERT(table.isAddrReserved(singleAddr), "reserved while enabled");
    table.setPolicy(only, DeviceTypeRecord::POLICY_DISABLED);
    TEST_ASSERT(!table.isAddrReserved(singleAddr) && !table.isAddrHookBlocked(singleAddr), "disabled: not reserved, hook allowed");

    // Enable again; setting the same policy doesn't change the generation
    table.setPolicy(first, DeviceTypeRecord::POLICY_ENABLED);
    TEST_ASSERT(table.getIdentCandidates(addr) == before, "enabled again");
    gen = table.getGeneration();
    table.setPolicy(first, DeviceTypeRecord::POLICY_ENABLED);
    TEST_ASSERT(table.getGeneration() == gen, "no change, same generation");

    // By key, and not found
    const char* key = table.getRecord(first)->deviceType;
    TEST_ASSERT(table.setPolicy(key, DeviceTypeRecord::POLICY_DISABLED) == DeviceTypeRecords::PolicyResult::Set, "disable by key");
    TEST_ASSERT(table.setPolicy("NoSuchType", DeviceTypeRecord::POLICY_DISABLED) == DeviceTypeRecords::PolicyResult::NotFound,
                "unknown key");
}

static void testDisabledClaim()
{
    printf("Running testDisabledClaim...\n");
    DeviceTypeRecords table;
    uint32_t addr = findSingleAddr(table);
    DeviceTypeIndexType idx = table.getIdentCandidates(addr)[0];
    TEST_ASSERT(!table.isAddrHookBlocked(addr), "hook allowed while enabled");
    TEST_ASSERT(table.setPolicy(idx, DeviceTypeRecord::POLICY_DISABLED_CLAIM) == DeviceTypeRecords::PolicyResult::Set, "claim");
    TEST_ASSERT(table.getIdentCandidates(addr).empty(), "not a candidate");
    TEST_ASSERT(table.isAddrReserved(addr), "address still reserved");
    TEST_ASSERT(table.isAddrHookBlocked(addr), "hook blocked");
    TEST_ASSERT(contains(table.getKnownTypesAtAddr(addr), idx), "still known at the address");
    TEST_ASSERT(contains(table.getDeviceTypeIdxsForAddr(addr), idx), "and for existing reservation callers");

    // An extended record can start disabled
    DeviceTypeRecordDynamic rec = makeRecord("ExtStartsDisabled", "0x0a");
    rec.initialPolicy = DeviceTypeRecord::POLICY_DISABLED;
    DeviceTypeIndexType extIdx = 0;
    TEST_ASSERT(table.addRecord(rec, extIdx) == DeviceTypeRecords::AddResult::Added, "added");
    TEST_ASSERT(!contains(table.getIdentCandidates(0x0a), extIdx) && (table.getPolicy(extIdx) == DeviceTypeRecord::POLICY_DISABLED),
                "initial policy");
}

static void testProtected()
{
    printf("Running testProtected...\n");
    DeviceTypeRecords table;
    uint32_t addr = findSingleAddr(table);
    DeviceTypeIndexType idx = table.getIdentCandidates(addr)[0];
    const char* key = table.getRecord(idx)->deviceType;
    table.setProtected(key);
    TEST_ASSERT(table.isProtected(key), "protected");
    TEST_ASSERT(table.setPolicy(idx, DeviceTypeRecord::POLICY_DISABLED) == DeviceTypeRecords::PolicyResult::Protected,
                "can't be disabled");
    TEST_ASSERT(table.setPolicy(idx, DeviceTypeRecord::POLICY_DISABLED_CLAIM) == DeviceTypeRecords::PolicyResult::Protected,
                "can't be claim-disabled");
    TEST_ASSERT(table.setPolicy(idx, DeviceTypeRecord::POLICY_ENABLED) == DeviceTypeRecords::PolicyResult::Set, "can be enabled");
    DeviceTypeIndexType newIdx = 0;
    TEST_ASSERT(table.addRecord(makeRecord(key, "0x0b"), newIdx) == DeviceTypeRecords::AddResult::Protected, "can't be overridden");
    TEST_ASSERT(newIdx == DEVICE_TYPE_INDEX_INVALID, "no index");
    TEST_ASSERT(table.getRecordSource(idx) == DeviceTypeRecords::RecordSource::Base, "still the base record");
    table.markReady();
    TEST_ASSERT(table.addRecord(makeRecord(key, "0x0b"), newIdx) == DeviceTypeRecords::AddResult::Protected,
                "can't be replaced by a new index either");
}

static void testOverride()
{
    printf("Running testOverride...\n");
    DeviceTypeRecords table;
    uint32_t addr = findSingleAddr(table);
    DeviceTypeIndexType baseIdx = table.getIdentCandidates(addr)[0];
    std::string key = table.getRecord(baseIdx)->deviceType;
    const DeviceTypeRecord* pBase = table.getRecord(baseIdx);

    // Compile the base record first: after the override the compiled record must follow it
    std::vector<DeviceTypeRecords::DeviceDetectionRec> recs;
    table.getDetectionRecs(baseIdx, recs);
    uint32_t gen = table.getGeneration();

    // Before Ready: replaced at the base index
    DeviceTypeIndexType idx = 0;
    TEST_ASSERT(table.addRecord(makeRecord(key.c_str(), "0x0c", "0x01=0x34"), idx) == DeviceTypeRecords::AddResult::Replaced,
                "replaced");
    TEST_ASSERT(idx == baseIdx, "keeps the base index");
    TEST_ASSERT(table.getGeneration() > gen, "generation increased");
    const DeviceTypeRecord* pRec = table.getRecord(baseIdx);
    TEST_ASSERT((pRec != pBase) && (strcmp(pRec->addresses, "0x0c") == 0), "the index now means the override");
    DeviceTypeIndexType foundIdx = 0;
    TEST_ASSERT((table.getRecord(key.c_str(), foundIdx) == pRec) && (foundIdx == baseIdx), "one index per key");
    TEST_ASSERT(table.getRecordSource(baseIdx) == DeviceTypeRecords::RecordSource::Override, "source override");
    TEST_ASSERT(contains(table.getIdentCandidates(0x0c), baseIdx) && !contains(table.getIdentCandidates(addr), baseIdx),
                "candidate at the override's address only");
    TEST_ASSERT(table.getDetectionRecs(baseIdx, recs) && (recs.size() == 1) && (recs[0].writeData[0] == 0x01),
                "compiled record follows the override");
    TEST_ASSERT(table.getExtendedRecordStats().count == 0, "no extended index used");

    // The same override again is the same record; a different one is a conflict (one override per base record)
    TEST_ASSERT(table.addRecord(makeRecord(key.c_str(), "0x0c", "0x01=0x34"), idx) == DeviceTypeRecords::AddResult::AlreadyPresent &&
                (idx == baseIdx), "same override again");
    TEST_ASSERT(table.addRecord(makeRecord(key.c_str(), "0x0d"), idx) == DeviceTypeRecords::AddResult::Conflict, "second override");

    // After Ready a record with another base key gets a new index
    uint32_t otherAddr = 0;
    for (uint32_t a = 0; a <= CatalogueIndex::MAX_ADDR; a++)
        if ((table.getIdentCandidates(a).size() == 1) && (table.getIdentCandidates(a)[0] != baseIdx))
            otherAddr = a;
    DeviceTypeIndexType otherBase = table.getIdentCandidates(otherAddr)[0];
    std::string otherKey = table.getRecord(otherBase)->deviceType;
    table.markReady();
    TEST_ASSERT(table.isReady(), "ready");
    TEST_ASSERT(table.addRecord(makeRecord(otherKey.c_str(), "0x0e"), idx) == DeviceTypeRecords::AddResult::Added, "added after Ready");
    TEST_ASSERT(idx >= DeviceTypeRecords::getNumBaseRecords(), "new index");
    TEST_ASSERT(table.getRecordSource(otherBase) == DeviceTypeRecords::RecordSource::Base, "base record unchanged");
    TEST_ASSERT(table.getRecordSource(idx) == DeviceTypeRecords::RecordSource::Extended, "source extended");
}

static void testDecoderInherited()
{
    printf("Running testDecoderInherited...\n");
    DeviceTypeRecords table;
    // A base record with a decoder and a response layout
    DeviceTypeIndexType baseIdx = DEVICE_TYPE_INDEX_INVALID;
    for (uint32_t i = 0; i < DeviceTypeRecords::getNumBaseRecords(); i++)
    {
        const DeviceTypeRecord* pRec = table.getRecord(i);
        if (pRec->pollResultDecodeFn && pRec->devInfoJson && strstr(pRec->devInfoJson, "\"resp\"") &&
                    (table.getRecordSource(i) == DeviceTypeRecords::RecordSource::Base))
        {
            baseIdx = i;
            break;
        }
    }
    TEST_ASSERT(baseIdx != DEVICE_TYPE_INDEX_INVALID, "base record with a decoder");
    const DeviceTypeRecord* pBase = table.getRecord(baseIdx);
    std::string devInfo = pBase->devInfoJson;
    DeviceTypeRecordDecodeFn baseDecode = pBase->pollResultDecodeFn;

    // Same response layout (whitespace aside): the override uses the base decoder
    std::string spaced = devInfo;
    size_t respPos = spaced.find("\"resp\":");
    spaced.insert(respPos + 7, " ");
    DeviceTypeIndexType idx = 0;
    TEST_ASSERT(table.addRecord(makeRecord(pBase->deviceType, pBase->addresses, pBase->detectionValues, spaced.c_str()), idx) ==
                DeviceTypeRecords::AddResult::Replaced, "replaced");
    TEST_ASSERT(table.getPollDecodeFn(idx) == baseDecode, "decoder inherited");

    // A different layout: no decoder
    DeviceTypeRecords table2;
    TEST_ASSERT(table2.addRecord(makeRecord(pBase->deviceType, pBase->addresses), idx) == DeviceTypeRecords::AddResult::Replaced,
                "replaced with a different layout");
    TEST_ASSERT(table2.getPollDecodeFn(idx) == nullptr, "decoder not inherited");
}

int main()
{
    testDisable();
    testDisabledClaim();
    testProtected();
    testOverride();
    testDecoderInherited();
    return testSummary("test_device_type_policy");
}
