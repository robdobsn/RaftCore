/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Catalogue index tests
//
// 1. For the base records (compiled from the catalogue), the runtime-built address map and scan lists equal
//    what the generator used to derive at build time. The expected result is produced from the same catalogue
//    by scripts/tests/reference_catalogue_index.py (the old generator algorithm) and passed as argv[1].
// 2. Extended records are first-class: their scanPriority is honoured (not forced into list 0), they are
//    identification candidates (before base records), identPriority orders candidates, probe-only records are
//    not candidates, and hardware type IDs are looked up. The index is rebuilt when the generation changes and
//    an index already held is unchanged.
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <chrono>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "testhdr.h"
#include "ArduinoWString.h"
#include "RaftJson.h"
#include "DeviceTypeRecords.h"

static DeviceTypeRecordDynamic makeRecord(const char* key, const char* addresses)
{
    return DeviceTypeRecordDynamic(key, addresses, "0x00=0x12", "", R"({"c":"0x00=r2","i":100,"s":1})", 2,
                R"({"name":"Test","resp":{"b":2,"a":[]}})", nullptr);
}

static std::vector<DeviceTypeIndexType> candidates(const DeviceTypeRecords& table, uint32_t addr)
{
    return table.getIdentCandidates(addr);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Base records match the generator's derived tables
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void testMatchesGeneratorTables(const char* expectedPath)
{
    printf("Running testMatchesGeneratorTables (%s)...\n", expectedPath);
    std::ifstream file(expectedPath);
    std::stringstream buf;
    buf << file.rdbuf();
    RaftJson expected(buf.str().c_str());
    TEST_ASSERT(buf.str().length() > 0, "expected index read");

    DeviceTypeRecords table;
    auto startTime = std::chrono::steady_clock::now();
    std::shared_ptr<const CatalogueIndex> pIndex = table.getCatalogueIndex();
    auto buildUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startTime).count();
    printf("  index for %u base records built in %lld us, %u bytes\n", DeviceTypeRecords::getNumBaseRecords(),
                (long long)buildUs, pIndex->getBytesUsed());

    // Address map
    uint32_t addrMismatches = 0;
    for (uint32_t addr = 0; addr <= CatalogueIndex::MAX_ADDR; addr++)
    {
        std::vector<String> expectedIdxStrs;
        expected.getArrayElems(("addrMap/" + std::to_string(addr)).c_str(), expectedIdxStrs);
        std::vector<DeviceTypeIndexType> actual = candidates(table, addr);
        bool same = actual.size() == expectedIdxStrs.size();
        for (uint32_t i = 0; same && (i < actual.size()); i++)
            same = actual[i] == (DeviceTypeIndexType)expectedIdxStrs[i].toInt();
        if (!same)
        {
            addrMismatches++;
            printf("  address 0x%02x: %u candidates, expected %u\n", addr, (unsigned)actual.size(), (unsigned)expectedIdxStrs.size());
        }
        if (table.isAddrReserved(addr) != (expectedIdxStrs.size() > 0))
            addrMismatches++;
    }
    TEST_ASSERT(addrMismatches == 0, "address map matches the generator's");

    // Scan lists
    std::vector<std::vector<BusElemAddrType>> lists;
    table.getScanPriorityLists(lists);
    std::vector<String> expectedLists;
    expected.getArrayElems("scanLists", expectedLists);
    TEST_ASSERT(lists.size() == expectedLists.size(), "number of scan lists");
    for (uint32_t i = 0; (i < lists.size()) && (i < expectedLists.size()); i++)
    {
        RaftJson listJson(expectedLists[i].c_str());
        std::vector<String> addrs;
        listJson.getArrayElems("", addrs);
        bool same = addrs.size() == lists[i].size();
        for (uint32_t j = 0; same && (j < addrs.size()); j++)
            same = (BusElemAddrType)addrs[j].toInt() == lists[i][j];
        if (!same)
            printf("  scan list %u: %u addresses, expected %u\n", i, (unsigned)lists[i].size(), (unsigned)addrs.size());
        TEST_ASSERT(same, "scan list matches the generator's");
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Extended records
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// An address no base record uses
static uint32_t findFreeAddr(const DeviceTypeRecords& table, uint32_t from)
{
    for (uint32_t addr = from; addr <= CatalogueIndex::MAX_ADDR; addr++)
        if (!table.isAddrReserved(addr))
            return addr;
    return 0;
}

// An address with a base record, and its first candidate
static uint32_t findUsedAddr(const DeviceTypeRecords& table, DeviceTypeIndexType& firstCand)
{
    for (uint32_t addr = 0; addr <= CatalogueIndex::MAX_ADDR; addr++)
    {
        std::vector<DeviceTypeIndexType> cands = candidates(table, addr);
        if (cands.size() > 0)
        {
            firstCand = cands[0];
            return addr;
        }
    }
    return 0;
}

static bool inList(const std::vector<BusElemAddrType>& list, uint32_t addr)
{
    return std::find(list.begin(), list.end(), addr) != list.end();
}

static void testExtendedScanPriority()
{
    printf("Running testExtendedScanPriority...\n");
    DeviceTypeRecords table;
    uint32_t addrA = findFreeAddr(table, 0x08);
    uint32_t addrB = findFreeAddr(table, addrA + 1);
    uint32_t addrC = findFreeAddr(table, addrB + 1);
    TEST_ASSERT(addrA && addrB && addrC, "free addresses");

    // A low priority record is not forced into list 0
    char addrsLow[16];
    snprintf(addrsLow, sizeof(addrsLow), "0x%02x", addrA);
    DeviceTypeRecordDynamic lowRec = makeRecord("ExtLow", addrsLow);
    lowRec.scanPriority = 2;
    DeviceTypeIndexType idx = 0;
    TEST_ASSERT(table.addRecord(lowRec, idx) == DeviceTypeRecords::AddResult::Added, "low added");

    // A high priority record (the default for extended records): primary address in list 0, others in list 1
    char addrsHigh[24];
    snprintf(addrsHigh, sizeof(addrsHigh), "0x%02x,0x%02x", addrB, addrC);
    TEST_ASSERT(table.addRecord(makeRecord("ExtHigh", addrsHigh), idx) == DeviceTypeRecords::AddResult::Added, "high added");

    std::vector<std::vector<BusElemAddrType>> lists;
    table.getScanPriorityLists(lists);
    TEST_ASSERT(lists.size() == 3, "three lists");
    if (lists.size() == 3)
    {
        TEST_ASSERT(!inList(lists[0], addrA) && !inList(lists[1], addrA) && inList(lists[2], addrA), "low in last list only");
        TEST_ASSERT(inList(lists[0], addrB) && !inList(lists[2], addrB), "high primary in list 0");
        TEST_ASSERT(inList(lists[1], addrC) && !inList(lists[0], addrC) && !inList(lists[2], addrC), "high secondary in list 1");
    }
    TEST_ASSERT(table.isAddrReserved(addrA) && table.isAddrReserved(addrB), "extended addresses reserved");
}

static void testCandidateOrder()
{
    printf("Running testCandidateOrder...\n");
    DeviceTypeRecords table;
    DeviceTypeIndexType baseFirst = 0;
    uint32_t addr = findUsedAddr(table, baseFirst);
    TEST_ASSERT(addr != 0, "address with a base record");
    std::vector<DeviceTypeIndexType> baseCands = candidates(table, addr);
    char addrs[8];
    snprintf(addrs, sizeof(addrs), "0x%02x", addr);

    // An extended record at the address is tried before the base records (as before the index)
    DeviceTypeIndexType extIdx = 0;
    TEST_ASSERT(table.addRecord(makeRecord("ExtSame", addrs), extIdx) == DeviceTypeRecords::AddResult::Added, "added");
    std::vector<DeviceTypeIndexType> cands = candidates(table, addr);
    TEST_ASSERT((cands.size() == baseCands.size() + 1) && (cands[0] == extIdx), "extended record first");

    // identPriority: lower goes after the base records, higher goes first
    DeviceTypeRecordDynamic lowRec = makeRecord("ExtLowIdent", addrs);
    lowRec.identPriority = -1;
    DeviceTypeIndexType lowIdx = 0;
    table.addRecord(lowRec, lowIdx);
    DeviceTypeRecordDynamic highRec = makeRecord("ExtHighIdent", addrs);
    highRec.identPriority = 5;
    DeviceTypeIndexType highIdx = 0;
    table.addRecord(highRec, highIdx);
    cands = candidates(table, addr);
    TEST_ASSERT((cands.size() == baseCands.size() + 3) && (cands.front() == highIdx) && (cands[1] == extIdx) &&
                (cands.back() == lowIdx), "identPriority order");

    // A probe-only record (adopted by an identification hook) is not a candidate and reserves nothing
    uint32_t freeAddr = findFreeAddr(table, 0x08);
    char freeAddrs[8];
    snprintf(freeAddrs, sizeof(freeAddrs), "0x%02x", freeAddr);
    DeviceTypeRecordDynamic probeRec = makeRecord("ExtProbeOnly", freeAddrs);
    probeRec.flags |= DeviceTypeRecord::FLAG_PROBE_ONLY;
    DeviceTypeIndexType probeIdx = 0;
    TEST_ASSERT(table.addRecord(probeRec, probeIdx) == DeviceTypeRecords::AddResult::Added, "probe-only added");
    TEST_ASSERT(candidates(table, freeAddr).empty() && !table.isAddrReserved(freeAddr), "probe-only not a candidate");
}

static void testHwTypeIdAndGeneration()
{
    printf("Running testHwTypeIdAndGeneration...\n");
    DeviceTypeRecords table;
    std::shared_ptr<const CatalogueIndex> pFirst = table.getCatalogueIndex();
    uint32_t gen0 = table.getGeneration();
    TEST_ASSERT(table.getCatalogueIndex() == pFirst, "index reused while the generation is unchanged");

    // Every base record with a hardware type ID (e.g. RSAO records in Axiom's catalogue) is found by it
    uint32_t hwIdMismatches = 0;
    for (uint32_t i = 0; i < DeviceTypeRecords::getNumBaseRecords(); i++)
    {
        const DeviceTypeRecord* pBase = table.getRecord((DeviceTypeIndexType)i);
        DeviceTypeIndexType foundIdx = 0;
        if (pBase && pBase->hasHwTypeId() && (table.getRecordByHwTypeId(pBase->hwTypeId, foundIdx) != pBase))
            hwIdMismatches++;
    }
    TEST_ASSERT(hwIdMismatches == 0, "base records found by hardware type ID");

    // An ID no base record has
    uint16_t freeHwTypeId = 0xfff0;
    DeviceTypeIndexType idx = 0;
    while (table.getRecordByHwTypeId(freeHwTypeId, idx))
        freeHwTypeId--;
    TEST_ASSERT(idx == DEVICE_TYPE_INDEX_INVALID, "unknown hardware type ID index");

    // A probe-only record with a hardware type ID (as RSAO records are)
    DeviceTypeRecordDynamic rec = makeRecord("ExtHwType", "");
    rec.flags |= DeviceTypeRecord::FLAG_PROBE_ONLY;
    rec.setHwTypeId(freeHwTypeId);
    DeviceTypeIndexType addedIdx = 0;
    TEST_ASSERT(table.addRecord(rec, addedIdx) == DeviceTypeRecords::AddResult::Added, "added");
    TEST_ASSERT(table.getGeneration() > gen0, "generation increased");
    const DeviceTypeRecord* pRec = table.getRecordByHwTypeId(freeHwTypeId, idx);
    TEST_ASSERT(pRec && (idx == addedIdx) && (strcmp(pRec->deviceType, "ExtHwType") == 0), "found by hardware type ID");

    // The index was rebuilt; the one held before is unchanged
    std::shared_ptr<const CatalogueIndex> pSecond = table.getCatalogueIndex();
    TEST_ASSERT(pSecond != pFirst, "index rebuilt");
    TEST_ASSERT(!pFirst->findByHwTypeId(freeHwTypeId, idx), "earlier index unchanged");

    // Adding a record already present doesn't change the generation
    uint32_t gen1 = table.getGeneration();
    table.addRecord(rec, idx);
    TEST_ASSERT(table.getGeneration() == gen1, "already present: generation unchanged");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A type's scan priority set in place of its record's (DevMan "typeSettings")
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void testTypeScanPriority()
{
    printf("Running testTypeScanPriority...\n");
    DeviceTypeRecords table;
    // An extended record alone at a free address, with no scan priority of its own
    uint32_t addr = findFreeAddr(table, 0x50);
    DeviceTypeRecordDynamic rec = makeRecord("ScanTest", ("0x" + String(addr, 16)).c_str());
    rec.scanPriority = DeviceTypeRecord::SCAN_PRIORITY_NONE;
    DeviceTypeIndexType idx;
    TEST_ASSERT(table.addRecord(rec, idx) == DeviceTypeRecords::AddResult::Added, "record added");
    TEST_ASSERT(table.getScanPriority(idx) == DeviceTypeRecord::SCAN_PRIORITY_NONE, "the record's (none)");
    std::vector<std::vector<BusElemAddrType>> lists;
    table.getScanPriorityLists(lists);
    // (the last list holds every address not in a higher one)
    TEST_ASSERT(!inList(lists[0], addr) && !inList(lists[1], addr) && inList(lists[2], addr), "only in the last list");

    // Set high: in the first list, and the index rebuilt (a new generation)
    uint32_t generation = table.getGeneration();
    TEST_ASSERT(table.setScanPriority("ScanTest", 0), "set high");
    TEST_ASSERT(table.getGeneration() != generation, "generation changed");
    TEST_ASSERT(table.getScanPriority(idx) == 0, "high");
    table.getScanPriorityLists(lists);
    TEST_ASSERT(inList(lists[0], addr), "in the first list");

    // Low, then none
    TEST_ASSERT(table.setScanPriority("ScanTest", 2), "set low");
    table.getScanPriorityLists(lists);
    TEST_ASSERT(!inList(lists[0], addr) && inList(lists[2], addr), "in the last list");
    generation = table.getGeneration();
    TEST_ASSERT(table.setScanPriority("ScanTest", 2) && (table.getGeneration() == generation), "unchanged: no new generation");
    TEST_ASSERT(table.setScanPriority("ScanTest", DeviceTypeRecord::SCAN_PRIORITY_NONE), "set none");
    table.getScanPriorityLists(lists);
    TEST_ASSERT(!inList(lists[0], addr) && inList(lists[2], addr), "only in the last list again");

    // Errors
    TEST_ASSERT(!table.setScanPriority("NoSuchType", 0), "unknown key");
    TEST_ASSERT(!table.setScanPriority("ScanTest", 5), "invalid priority");

    // A base record's priority can be changed too
    std::vector<DeviceTypeIndexType> cands;
    uint32_t baseAddr = 0;
    for (uint32_t a = 0; (a <= CatalogueIndex::MAX_ADDR) && (baseAddr == 0); a++)
    {
        cands = table.getIdentCandidates(a);
        if ((cands.size() == 1) && (cands[0] < DeviceTypeRecords::getNumBaseRecords()) && inList(lists[0], a))
            baseAddr = a;
    }
    TEST_ASSERT(baseAddr != 0, "a base record alone at an address in the first list");
    const DeviceTypeRecord* pBase = table.getRecord(cands[0]);
    TEST_ASSERT(pBase && table.setScanPriority(pBase->deviceType, DeviceTypeRecord::SCAN_PRIORITY_NONE), "base record set none");
    table.getScanPriorityLists(lists);
    TEST_ASSERT(!inList(lists[0], baseAddr), "base record's address out of the first list");
    TEST_ASSERT(pBase->scanPriority == 0, "the record itself is unchanged");
}

int main(int argc, char** argv)
{
    if (argc > 1)
        testMatchesGeneratorTables(argv[1]);
    else
        testFail("testMatchesGeneratorTables", "no expected index file given");
    testExtendedScanPriority();
    testCandidateOrder();
    testHwTypeIdAndGeneration();
    testTypeScanPriority();
    return testSummary("test_catalogue_index");
}
