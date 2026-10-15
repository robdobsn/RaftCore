/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device type table tests (on the device)
//
// Memory used by extended device type records, which are added at runtime: each is one packed allocation,
// in PSRAM when the board has it. Records are of typical size (the median devInfoJson in the RaftCore catalogue
// is about 380 bytes). The default limits must hold 32 typical records without PSRAM and 256 with PSRAM (D3).
// The behaviour of the table is tested on Linux (linux_unit_tests/test_device_type_table.cpp).
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <string>
#include "unity.h"
#include "RaftArduino.h"
#include "Logger.h"
#include "PlatformUtils.h"
#include "DeviceTypeRecords.h"
#include "DeviceTypeRecordPacked.h"
#include "esp_heap_caps.h"

static const char* MODULE_PREFIX = "DevTypeTableTest";

// A record of typical size: devInfoJson of about 380 bytes, and typical addresses, detection and poll strings
static DeviceTypeRecordDynamic makeTypicalRecord(uint32_t n)
{
    static const char* devInfoJson =
        R"({"name":"Typical Sensor","desc":"Typical sensor record for memory tests","manu":"Test","type":"Typical",)"
        R"("resp":{"b":8,"a":[{"n":"x","t":">h","u":"mg","r":[-2000,2000],"d":1000,"f":".3f","o":"float"},)"
        R"({"n":"y","t":">h","u":"mg","r":[-2000,2000],"d":1000,"f":".3f","o":"float"},)"
        R"({"n":"z","t":">h","u":"mg","r":[-2000,2000],"d":1000,"f":".3f","o":"float"},)"
        R"({"n":"s","t":"B","u":"","r":[0,255],"f":"d","o":"uint8"}]}})";
    std::string key = "TypicalSensor" + std::to_string(n);
    return DeviceTypeRecordDynamic(key.c_str(), "0x48,0x49", "0x0f=0x33", "0x2007&0x2300",
                R"({"c":"0x28=r6&0x0f=r1","i":100,"s":1})", 8, devInfoJson, nullptr);
}

// Add records to a table with the default limits until it is full, measuring the heap used
static void addUntilFull(uint32_t expectedAtLeast)
{
    DeviceTypeRecords table;
    DeviceTypeRecords::ExtendedRecordStats limits = table.getExtendedRecordStats();
    uint32_t packedSize = DeviceTypeRecordPacked::sizeFor(makeTypicalRecord(0));

    uint32_t internalBefore = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    uint32_t spiramBefore = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    uint32_t added = 0;
    uint32_t addedPackedBytes = 0;
    for (uint32_t i = 0; i < limits.maxRecords + 1; i++)
    {
        DeviceTypeIndexType idx = 0;
        DeviceTypeRecordDynamic rec = makeTypicalRecord(i);
        if (table.addRecord(rec, idx) != DeviceTypeRecords::AddResult::Added)
            break;
        added++;
        addedPackedBytes += DeviceTypeRecordPacked::sizeFor(rec);
    }
    uint32_t internalUsed = internalBefore - heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    uint32_t spiramUsed = spiramBefore - heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    DeviceTypeRecords::ExtendedRecordStats stats = table.getExtendedRecordStats();

    LOG_I(MODULE_PREFIX, "PSRAM %s: limits %d records %d bytes; packed record %d bytes; added %d (%d bytes); "
                "heap used internal %d (%.1f per record) PSRAM %d (%.1f per record)",
                utilsGetSPIRAMSize() ? "yes" : "no", (int)limits.maxRecords, (int)limits.maxBytes, (int)packedSize,
                (int)added, (int)stats.bytesUsed, (int)internalUsed, added ? (double)internalUsed / added : 0.0,
                (int)spiramUsed, added ? (double)spiramUsed / added : 0.0);

    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(expectedAtLeast, added);
    TEST_ASSERT_EQUAL_UINT32(addedPackedBytes, stats.bytesUsed);
    if (utilsGetSPIRAMSize() != 0)
    {
        // Records are in PSRAM: internal RAM holds only the 4-byte record pointers and the 2-byte key index
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(added * 8 + limits.maxRecords * 4 + 64, internalUsed);
        TEST_ASSERT_GREATER_OR_EQUAL_UINT32(addedPackedBytes, spiramUsed);
    }
    else
    {
        // One allocation per record: allocator overhead is small compared with one String per field
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(addedPackedBytes + added * 24 + limits.maxRecords * 4 + 128, internalUsed);
    }
}

TEST_CASE("extended device type records within the default limits", "[DeviceTypeTable]")
{
    // D3: 32 typical records without PSRAM, 256 with
    addUntilFull(utilsGetSPIRAMSize() != 0 ? 256 : 32);
}

TEST_CASE("extended device type records freed with the table", "[DeviceTypeTable]")
{
    uint32_t internalBefore = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    {
        DeviceTypeRecords table;
        for (uint32_t i = 0; i < 20; i++)
        {
            DeviceTypeIndexType idx = 0;
            table.addRecord(makeTypicalRecord(i), idx);
        }
    }
    TEST_ASSERT_EQUAL_UINT32(internalBefore, heap_caps_get_free_size(MALLOC_CAP_8BIT));
}
