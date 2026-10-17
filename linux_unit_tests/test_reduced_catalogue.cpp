/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Reduced catalogue test (device identification plan §10.7 item 6)
//
// This test is built with a reduced base catalogue: half the records of the RaftCore catalogue (those at even
// positions, scripts/tests/split_catalogue.py). The other half is supplied as a device type file. With the file
// loaded, the identification candidates at every address, the reserved addresses and the scan lists are those of
// the full catalogue - the one difference is the documented one: at the same identPriority and detection strength,
// records from the file (extended records) are tried before compiled ones.
//
// Usage: test_reduced_catalogue <full catalogue.json> <file.json> <full catalogue index expected.json>
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "testhdr.h"
#include "ArduinoWString.h"
#include "RaftJson.h"
#include "DeviceTypeRecords.h"
#include "DeviceTypeFileLoader.h"
#include "DeviceTypeGrammar.h"

static std::string readFile(const char* pPath)
{
    std::ifstream file(pPath);
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        printf("Usage: %s <full catalogue.json> <file.json> <expected.json>\n", argv[0]);
        return 1;
    }
    printf("Running testReducedCatalogue...\n");
    std::string fullJson = readFile(argv[1]);
    std::string fileJson = readFile(argv[2]);
    std::string expectedJson = readFile(argv[3]);
    RaftJson full(fullJson.c_str());
    RaftJson expected(expectedJson.c_str());

    // The full catalogue's keys in order (its device type indexes), and the ordering fields of each record
    std::vector<String> keys;
    full.getKeys("devTypes", keys);
    std::map<std::string, std::pair<int, int32_t>> orderFields;
    for (const String& key : keys)
    {
        String base = "devTypes/" + key + "/";
        int identPriority = full.getLong((base + "identPriority").c_str(), 0);
        String detection = full.getString((base + "detectionValues").c_str(), "");
        int32_t strength = DeviceTypeGrammar::detectionStrength(
                    DeviceTypeGrammar::compile(detection.c_str(), DeviceTypeGrammar::Field::Detection));
        orderFields[key.c_str()] = {identPriority, strength};
    }

    // The base records are the reduced catalogue; the rest comes from the file
    DeviceTypeRecords table;
    uint32_t numBase = DeviceTypeRecords::getNumBaseRecords();
    TEST_ASSERT((numBase > 0) && (numBase == (keys.size() + 1) / 2), "the base catalogue is the reduced one");
    DeviceTypeFileLoader::Result result;
    DeviceTypeFileLoader::applyJson(table, fileJson.c_str(), DeviceTypeFileLoader::Limits(), result);
    TEST_ASSERT((result.added == keys.size() - numBase) && (result.rejected == 0),
                ("every file record is added: " + std::string(result.firstError.c_str())).c_str());
    std::set<std::string> fromFile;
    for (uint32_t pos = 1; pos < keys.size(); pos += 2)
        fromFile.insert(keys[pos].c_str());

    // Candidates at every address
    uint32_t addrMismatches = 0;
    uint32_t addrsWithTieReorder = 0;
    for (uint32_t addr = 0; addr <= CatalogueIndex::MAX_ADDR; addr++)
    {
        // Expected: the full catalogue's order, with file records first within each group of equal ordering fields
        std::vector<String> expectedIdxStrs;
        expected.getArrayElems(("addrMap/" + std::to_string(addr)).c_str(), expectedIdxStrs);
        std::vector<std::string> expectedKeys;
        for (const String& idxStr : expectedIdxStrs)
            expectedKeys.push_back(keys[idxStr.toInt()].c_str());
        std::vector<std::string> reordered;
        for (uint32_t start = 0; start < expectedKeys.size();)
        {
            uint32_t end = start;
            while ((end < expectedKeys.size()) && (orderFields[expectedKeys[end]] == orderFields[expectedKeys[start]]))
                end++;
            for (uint32_t i = start; i < end; i++)
                if (fromFile.count(expectedKeys[i]))
                    reordered.push_back(expectedKeys[i]);
            for (uint32_t i = start; i < end; i++)
                if (!fromFile.count(expectedKeys[i]))
                    reordered.push_back(expectedKeys[i]);
            start = end;
        }
        if (reordered != expectedKeys)
            addrsWithTieReorder++;

        // Actual
        std::vector<std::string> actualKeys;
        for (DeviceTypeIndexType idx : table.getIdentCandidates(addr))
        {
            const DeviceTypeRecord* pRec = table.getRecord(idx);
            actualKeys.push_back((pRec && pRec->deviceType) ? pRec->deviceType : "?");
        }
        if (actualKeys != reordered)
        {
            addrMismatches++;
            printf("  address 0x%02x:", addr);
            for (const std::string& key : actualKeys)
                printf(" %s", key.c_str());
            printf(" - expected");
            for (const std::string& key : reordered)
                printf(" %s", key.c_str());
            printf("\n");
        }
        if (table.isAddrReserved(addr) != (expectedIdxStrs.size() > 0))
            addrMismatches++;
    }
    printf("  %u addresses where file records come first among equals\n", addrsWithTieReorder);
    TEST_ASSERT(addrMismatches == 0, "candidates and reserved addresses are the full catalogue's");

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
        std::set<BusElemAddrType> expectedSet;
        for (const String& addr : addrs)
            expectedSet.insert((BusElemAddrType)addr.toInt());
        std::set<BusElemAddrType> actualSet(lists[i].begin(), lists[i].end());
        TEST_ASSERT((actualSet == expectedSet) && (lists[i].size() == addrs.size()), "scan list has the full catalogue's addresses");
    }
    return testSummary("test_reduced_catalogue");
}
