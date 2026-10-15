/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device type table tests
//
// DeviceTypeRecords as a table: looking records up by index and by key (base records, compiled from
// devtypes/DeviceTypeRecords.json, and extended records added at runtime), adding extended records (results,
// limits, a key added twice with the same or different content, overriding a base key), and reading records
// by index from one thread while another adds them.
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <atomic>
#include <string>
#include <thread>
#include <vector>
#include "testhdr.h"
#include "ArduinoWString.h"
#include "DeviceTypeRecords.h"

static DeviceTypeRecordDynamic makeRecord(const std::string& key, const char* addresses = "0x50",
            const char* devInfoJson = R"({"name":"Test","resp":{"b":2,"a":[]}})", uint16_t pollDataSizeBytes = 2)
{
    return DeviceTypeRecordDynamic(key.c_str(), addresses, "0x00=0x12", "", R"({"c":"0x00=r2","i":100,"s":1})",
                pollDataSizeBytes, devInfoJson, nullptr);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Base records
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void testBaseLookups()
{
    printf("Running testBaseLookups...\n");
    DeviceTypeRecords table;
    uint32_t numBase = DeviceTypeRecords::getNumBaseRecords();
    TEST_ASSERT(numBase > 0, "base records present");

    // Every base record is found by index (without a copy) and by key (binary search), at the same index
    uint32_t keyMismatches = 0;
    for (uint32_t idx = 0; idx < numBase; idx++)
    {
        const DeviceTypeRecord* pRec = table.getRecord((DeviceTypeIndexType)idx);
        if (!pRec || !pRec->deviceType)
        {
            keyMismatches++;
            continue;
        }
        DeviceTypeIndexType foundIdx = DEVICE_TYPE_INDEX_INVALID;
        const DeviceTypeRecord* pByKey = table.getRecord(pRec->deviceType, foundIdx);
        if ((pByKey != pRec) || (foundIdx != idx))
            keyMismatches++;
    }
    TEST_ASSERT(keyMismatches == 0, "every base record found by index and by key");

    // The copying API returns the same content
    DeviceTypeRecord copy;
    TEST_ASSERT(table.getDeviceInfo(0, copy) && (copy.deviceType == table.getRecord(0)->deviceType), "getDeviceInfo copy");

    // Not found
    DeviceTypeIndexType idx = 0;
    TEST_ASSERT(table.getRecord("NoSuchDeviceTypeKey", idx) == nullptr, "unknown key");
    TEST_ASSERT(idx == DEVICE_TYPE_INDEX_INVALID, "unknown key index invalid");
    TEST_ASSERT(table.getRecord((DeviceTypeIndexType)numBase) == nullptr, "index past the base records");
    TEST_ASSERT(table.getRecord(DEVICE_TYPE_INDEX_INVALID) == nullptr, "invalid index");
    TEST_ASSERT(table.getRecord(nullptr, idx) == nullptr, "null key");
    String keyStr = "NoSuchDeviceTypeKey";
    TEST_ASSERT(!table.getDeviceInfo(keyStr, copy, idx), "getDeviceInfo unknown key");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Extended records
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void testAddResults()
{
    printf("Running testAddResults...\n");
    DeviceTypeRecords table;
    const uint32_t numBase = DeviceTypeRecords::getNumBaseRecords();
    TEST_ASSERT(table.setExtendedRecordLimits(4, 100000), "limits set before any record");

    // Added: indexes follow the base records
    DeviceTypeIndexType idxA = 0, idxB = 0;
    TEST_ASSERT(table.addRecord(makeRecord("ExtA"), idxA) == DeviceTypeRecords::AddResult::Added, "add A");
    TEST_ASSERT(idxA == numBase, "A index after base");
    TEST_ASSERT(table.addRecord(makeRecord("ExtB", "0x51"), idxB) == DeviceTypeRecords::AddResult::Added, "add B");
    TEST_ASSERT(idxB == numBase + 1, "B index");

    // The record is found by index and key, with its content
    const DeviceTypeRecord* pA = table.getRecord(idxA);
    TEST_ASSERT(pA && (strcmp(pA->deviceType, "ExtA") == 0) && (strcmp(pA->addresses, "0x50") == 0) &&
                (pA->pollDataSizeBytes == 2) && (strcmp(pA->pollInfo, R"({"c":"0x00=r2","i":100,"s":1})") == 0),
                "A content");
    DeviceTypeIndexType foundIdx = 0;
    TEST_ASSERT(table.getRecord("ExtB", foundIdx) == table.getRecord(idxB) && (foundIdx == idxB), "B by key");

    // Same key, same content: the same record
    DeviceTypeIndexType idxAgain = 0;
    TEST_ASSERT(table.addRecord(makeRecord("ExtA"), idxAgain) == DeviceTypeRecords::AddResult::AlreadyPresent, "A again");
    TEST_ASSERT(idxAgain == idxA, "A again same index");

    // Same key, different content (e.g. a second static device of the same type with a different data size):
    // rejected, since one key and one index must mean one record (D17)
    DeviceTypeIndexType idxConflict = 0;
    TEST_ASSERT(table.addRecord(makeRecord("ExtA", "0x50", R"({"name":"Test","resp":{"b":4,"a":[]}})", 4), idxConflict) ==
                DeviceTypeRecords::AddResult::Conflict, "A with different content");
    TEST_ASSERT(idxConflict == DEVICE_TYPE_INDEX_INVALID, "conflict index invalid");
    TEST_ASSERT(table.getRecord(idxA)->pollDataSizeBytes == 2, "A unchanged");

    // No key
    DeviceTypeIndexType idxInvalid = 0;
    TEST_ASSERT(table.addRecord(makeRecord(""), idxInvalid) == DeviceTypeRecords::AddResult::Invalid, "no key");
    TEST_ASSERT(idxInvalid == DEVICE_TYPE_INDEX_INVALID, "invalid index invalid");

    // Full by count (limit 4)
    DeviceTypeIndexType idx = 0;
    TEST_ASSERT(table.addRecord(makeRecord("ExtC"), idx) == DeviceTypeRecords::AddResult::Added, "add C");
    TEST_ASSERT(table.addRecord(makeRecord("ExtD"), idx) == DeviceTypeRecords::AddResult::Added, "add D");
    TEST_ASSERT(table.addRecord(makeRecord("ExtE"), idx) == DeviceTypeRecords::AddResult::Full, "full by count");
    TEST_ASSERT(idx == DEVICE_TYPE_INDEX_INVALID, "full index invalid (never 0)");
    TEST_ASSERT(table.getRecord("ExtE", idx) == nullptr, "E not added");

    // Limits can't change once records are added
    TEST_ASSERT(!table.setExtendedRecordLimits(100, 100000), "limits fixed after first add");

    // Stats
    DeviceTypeRecords::ExtendedRecordStats stats = table.getExtendedRecordStats();
    TEST_ASSERT((stats.count == 4) && (stats.maxRecords == 4) && (stats.maxBytes == 100000), "stats");
    TEST_ASSERT(stats.bytesUsed > 4 * sizeof(DeviceTypeRecord), "stats bytes");

    // The legacy API: true only when added; index of the existing record when already present
    DeviceTypeIndexType legacyIdx = 0;
    TEST_ASSERT(!table.addExtendedDeviceTypeRecord(makeRecord("ExtB", "0x51"), legacyIdx) && (legacyIdx == idxB),
                "legacy add of present record");
    TEST_ASSERT(!table.addExtendedDeviceTypeRecord(makeRecord("ExtF"), legacyIdx) &&
                (legacyIdx == DEVICE_TYPE_INDEX_INVALID), "legacy add when full");
}

static void testByteBudget()
{
    printf("Running testByteBudget...\n");
    DeviceTypeRecords table;
    DeviceTypeRecordDynamic rec = makeRecord("Big0");
    uint32_t recBytes = sizeof(DeviceTypeRecord) + 5 + 5 + 10 + 1 + strlen(R"({"c":"0x00=r2","i":100,"s":1})") + 1 +
                strlen(R"({"name":"Test","resp":{"b":2,"a":[]}})") + 1;
    TEST_ASSERT(table.setExtendedRecordLimits(100, recBytes * 2 + recBytes / 2), "limits");
    DeviceTypeIndexType idx = 0;
    TEST_ASSERT(table.addRecord(makeRecord("Big0"), idx) == DeviceTypeRecords::AddResult::Added, "first fits");
    TEST_ASSERT(table.addRecord(makeRecord("Big1"), idx) == DeviceTypeRecords::AddResult::Added, "second fits");
    TEST_ASSERT(table.addRecord(makeRecord("Big2"), idx) == DeviceTypeRecords::AddResult::Full, "third over budget");
    TEST_ASSERT(table.getExtendedRecordStats().bytesUsed == recBytes * 2, "bytes used is the packed size");
}

static void testOverrideBaseKeyAndAddressLookups()
{
    printf("Running testOverrideBaseKeyAndAddressLookups...\n");
    DeviceTypeRecords table;
    const uint32_t numBase = DeviceTypeRecords::getNumBaseRecords();
    const char* baseKey = table.getRecord(0)->deviceType;

    // An extended record with a base record's key is found by key in its place (the base record keeps its
    // index and stays reachable by index)
    DeviceTypeIndexType extIdx = 0;
    TEST_ASSERT(table.addRecord(makeRecord(baseKey, "0x52"), extIdx) == DeviceTypeRecords::AddResult::Added, "override added");
    DeviceTypeIndexType foundIdx = 0;
    TEST_ASSERT(table.getRecord(baseKey, foundIdx) == table.getRecord(extIdx) && (foundIdx == extIdx), "override found by key");
    TEST_ASSERT(table.getRecord(0) != table.getRecord(extIdx), "base record still at its index");

    // Address lookup and scan lists include extended records
    std::vector<DeviceTypeIndexType> idxs = table.getDeviceTypeIdxsForAddr(0x52);
    TEST_ASSERT(std::find(idxs.begin(), idxs.end(), extIdx) != idxs.end(), "extended record found by address");
    std::vector<std::vector<BusElemAddrType>> lists;
    table.getScanPriorityLists(lists);
    TEST_ASSERT((lists.size() > 0) && (std::find(lists[0].begin(), lists[0].end(), 0x52) != lists[0].end()),
                "extended address in first scan list");
    TEST_ASSERT(extIdx == numBase, "override index");
}

// Many records: key lookups (binary search over records added out of key order) and pointers that don't move
static void testManyRecords()
{
    printf("Running testManyRecords...\n");
    DeviceTypeRecords table;
    const uint32_t NUM = 256;
    TEST_ASSERT(table.setExtendedRecordLimits(NUM, 1024 * 1024), "limits");
    std::vector<const DeviceTypeRecord*> firstPtrs;
    uint32_t addFailures = 0;
    for (uint32_t i = 0; i < NUM; i++)
    {
        // Keys added in a scrambled order
        uint32_t k = (i * 97) % NUM;
        DeviceTypeIndexType idx = 0;
        if (table.addRecord(makeRecord("Rec" + std::to_string(k)), idx) != DeviceTypeRecords::AddResult::Added)
            addFailures++;
        firstPtrs.push_back(table.getRecord(idx));
    }
    TEST_ASSERT(addFailures == 0, "all added");
    uint32_t lookupFailures = 0;
    for (uint32_t i = 0; i < NUM; i++)
    {
        uint32_t k = (i * 97) % NUM;
        DeviceTypeIndexType idx = 0;
        const DeviceTypeRecord* pRec = table.getRecord(("Rec" + std::to_string(k)).c_str(), idx);
        if (!pRec || (pRec != firstPtrs[i]) || (idx != DeviceTypeRecords::getNumBaseRecords() + i))
            lookupFailures++;
    }
    TEST_ASSERT(lookupFailures == 0, "all found by key at their index, at the same address");
}

// Reading by index (no lock) while another thread adds records
static void testConcurrentReadWhileAdding()
{
    printf("Running testConcurrentReadWhileAdding...\n");
    DeviceTypeRecords table;
    const uint32_t NUM = 200;
    TEST_ASSERT(table.setExtendedRecordLimits(NUM, 1024 * 1024), "limits");
    const uint32_t numBase = DeviceTypeRecords::getNumBaseRecords();
    std::atomic<bool> done{false};
    std::atomic<uint32_t> badReads{0};
    std::atomic<uint32_t> goodReads{0};
    std::thread reader([&]() {
        while (!done.load())
        {
            for (uint32_t i = 0; i < NUM; i++)
            {
                const DeviceTypeRecord* pRec = table.getRecord((DeviceTypeIndexType)(numBase + i));
                if (!pRec)
                    break;
                // A record that is visible is complete
                if (!pRec->deviceType || (std::string(pRec->deviceType) != "Conc" + std::to_string(i)) ||
                            (pRec->pollDataSizeBytes != 2))
                    badReads++;
                else
                    goodReads++;
            }
        }
    });
    for (uint32_t i = 0; i < NUM; i++)
    {
        DeviceTypeIndexType idx = 0;
        table.addRecord(makeRecord("Conc" + std::to_string(i)), idx);
    }
    done = true;
    reader.join();
    TEST_ASSERT(badReads == 0, "no incomplete records seen");
    TEST_ASSERT(goodReads > 0, "records read while adding");
    TEST_ASSERT(table.getExtendedRecordStats().count == NUM, "all added");
}

int main()
{
    testBaseLookups();
    testAddResults();
    testByteBudget();
    testOverrideBaseKeyAndAddressLookups();
    testManyRecords();
    testConcurrentReadWhileAdding();
    return testSummary("test_device_type_table");
}
