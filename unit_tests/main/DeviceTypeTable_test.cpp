/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device type table tests (on the device)
//
// Memory used by extended device type records, which are added at runtime: each is one packed allocation,
// in PSRAM when the board has it. Records are of typical size (the median devInfoJson in the RaftCore catalogue
// is about 380 bytes). The default limits must hold 32 typical records without PSRAM and 256 with PSRAM (D3).
// A record's runtime decoder is made when a device of its type is first identified (ensureDecoder), so adding
// records costs only the records; each decoder made costs about 1 KB, within the decoder budget without PSRAM
// (devdocs/runtime-decoders-on-demand-plan.md). The stack and time to make a decoder are measured.
// The behaviour of the table is tested on Linux (linux_unit_tests/test_device_type_table.cpp).
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <string>
#include <algorithm>
#include "unity.h"
#include "RaftArduino.h"
#include "Logger.h"
#include "PlatformUtils.h"
#include "RaftJson.h"
#include "DeviceTypeRecords.h"
#include "DeviceTypeRecordPacked.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

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

static bool hasPSRAM()
{
    return utilsGetSPIRAMSize() != 0;
}

// Add records to a table with the default limits until it is full, measuring the heap used; then make decoders for
// some of them (devices of those types identified), measuring again
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
                hasPSRAM() ? "yes" : "no", (int)limits.maxRecords, (int)limits.maxBytes, (int)packedSize,
                (int)added, (int)stats.bytesUsed, (int)internalUsed, added ? (double)internalUsed / added : 0.0,
                (int)spiramUsed, added ? (double)spiramUsed / added : 0.0);

    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(expectedAtLeast, added);
    TEST_ASSERT_EQUAL_UINT32(addedPackedBytes, stats.bytesUsed);
    TEST_ASSERT_EQUAL_UINT32(0, stats.decodersMade);
    if (hasPSRAM())
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

    // Devices of four of the types identified: a decoder each, about 1 KB, internal without PSRAM
    static const uint32_t NUM_DECODERS = 4;
    internalBefore = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    spiramBefore = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    uint32_t numBase = DeviceTypeRecords::getNumBaseRecords();
    for (uint32_t i = 0; i < NUM_DECODERS; i++)
        TEST_ASSERT_TRUE(table.ensureDecoder(numBase + i));
    internalUsed = internalBefore - heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    spiramUsed = spiramBefore - heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    stats = table.getExtendedRecordStats();
    uint32_t heapUsed = hasPSRAM() ? spiramUsed : internalUsed;
    LOG_I(MODULE_PREFIX, "PSRAM %s: %d decoders made: %d bytes (%d per decoder) reported, heap used internal %d PSRAM %d; "
                "budget %d", hasPSRAM() ? "yes" : "no", (int)stats.decodersMade, (int)stats.decoderBytes,
                (int)(stats.decoderBytes / NUM_DECODERS), (int)internalUsed, (int)spiramUsed, (int)stats.maxDecoderBytes);
    TEST_ASSERT_EQUAL_UINT32(NUM_DECODERS, stats.decodersMade);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(1500 * NUM_DECODERS, heapUsed);
    // The bytes reported are the allocations themselves: allocator overhead is at most a few dozen bytes for each of
    // a decoder's allocations
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(heapUsed, stats.decoderBytes);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(heapUsed, stats.decoderBytes + 400 * NUM_DECODERS);
    if (hasPSRAM())
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(NUM_DECODERS * 64, internalUsed);
}

TEST_CASE("extended device type records within the default limits", "[DeviceTypeTable]")
{
    // D3: 32 typical records without PSRAM, 256 with
    addUntilFull(hasPSRAM() ? 256 : 32);
}

TEST_CASE("runtime decoders within the decoder limits", "[DeviceTypeTable]")
{
    // Without PSRAM: the default budget (8 KB) holds about 8 typical decoders. With PSRAM: no budget, but a slot
    // each (RAFT_RUNTIME_DECODE_MAX_DECODERS) - one more type than there are slots is reported, not silently missing
    DeviceTypeRecords table;
    DeviceTypeRecords::ExtendedRecordStats limits = table.getExtendedRecordStats();
    uint32_t slotsFree = RAFT_RUNTIME_DECODE_MAX_DECODERS - DeviceTypeRuntimeDecoder::getNumSlotsInUse();
    uint32_t numToAdd = std::min<uint32_t>(limits.maxRecords, slotsFree + 1);
    uint32_t numBase = DeviceTypeRecords::getNumBaseRecords();
    for (uint32_t i = 0; i < numToAdd; i++)
    {
        DeviceTypeIndexType idx = 0;
        TEST_ASSERT_TRUE(table.addRecord(makeTypicalRecord(i), idx) == DeviceTypeRecords::AddResult::Added);
    }
    uint32_t made = 0;
    for (uint32_t i = 0; i < numToAdd; i++)
        made += table.ensureDecoder(numBase + i) ? 1 : 0;
    DeviceTypeRecords::ExtendedRecordStats stats = table.getExtendedRecordStats();
    LOG_I(MODULE_PREFIX, "PSRAM %s: %d types identified, %d decoders made (%d bytes, budget %d), %d failed: %s",
                hasPSRAM() ? "yes" : "no", (int)numToAdd, (int)made, (int)stats.decoderBytes, (int)stats.maxDecoderBytes,
                (int)stats.decodersFailed, table.getDecoderProblem(numBase + numToAdd - 1).c_str());
    TEST_ASSERT_EQUAL_UINT32(made, stats.decodersMade);
    TEST_ASSERT_EQUAL_UINT32(numToAdd - made, stats.decodersFailed);
    TEST_ASSERT_TRUE(table.getDecoderKind(numBase + numToAdd - 1) == DeviceTypeRecords::DecoderKind::RuntimeFailed);
    if (hasPSRAM())
    {
        TEST_ASSERT_EQUAL_UINT32(slotsFree, made);
    }
    else
    {
        TEST_ASSERT_EQUAL_UINT32(DeviceTypeRecords::RUNTIME_DECODER_MAX_BYTES_DEFAULT, stats.maxDecoderBytes);
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(stats.maxDecoderBytes, stats.decoderBytes);
        TEST_ASSERT_GREATER_OR_EQUAL_UINT32(6, made);
    }
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
            table.ensureDecoder(idx);
        }
    }
    TEST_ASSERT_EQUAL_UINT32(internalBefore, heap_caps_get_free_size(MALLOC_CAP_8BIT));
}

// Stack and time to make large records' decoders, one after another as their devices are identified. They are
// made on the main task (DeviceManager): a bus's worker task (the I2C one has 5500 bytes) hasn't the stack to spare
// (measured 2026-10-22: about 2 KB for create() alone, 7-10 ms each, on an ESP32-S3)
// devInfoJson of four of the largest RaftCore records decoded from their attribute lists (TrillSquare and CAP1188
// have 16 attributes each)
static const char* LARGE_DEV_INFO_JSONS[] = {
    // TrillSquare
    R"JSON({"name":"Trill Square","desc":"Capacitive Touch Square","manu":"Bela","type":"TrillSquare","clas":[")JSON"
    R"JSON(TCH"],"resp":{"b":32,"a":[{"n":"vloc0","t":">H","r":[0,65535],"f":"d","o":"uint16"},{"n":"vloc1","t")JSON"
    R"JSON(:">H","r":[0,65535],"f":"d","o":"uint16"},{"n":"vloc2","t":">H","r":[0,65535],"f":"d","o":"uint16"},)JSON"
    R"JSON({"n":"vloc3","t":">H","r":[0,65535],"f":"d","o":"uint16"},{"n":"vsize0","t":">H","r":[0,65535],"f":")JSON"
    R"JSON(d","o":"uint16"},{"n":"vsize1","t":">H","r":[0,65535],"f":"d","o":"uint16"},{"n":"vsize2","t":">H",")JSON"
    R"JSON(r":[0,65535],"f":"d","o":"uint16"},{"n":"vsize3","t":">H","r":[0,65535],"f":"d","o":"uint16"},{"n":")JSON"
    R"JSON(hloc0","t":">H","r":[0,65535],"f":"d","o":"uint16"},{"n":"hloc1","t":">H","r":[0,65535],"f":"d","o":)JSON"
    R"JSON("uint16"},{"n":"hloc2","t":">H","r":[0,65535],"f":"d","o":"uint16"},{"n":"hloc3","t":">H","r":[0,655)JSON"
    R"JSON(35],"f":"d","o":"uint16"},{"n":"hsize0","t":">H","r":[0,65535],"f":"d","o":"uint16"},{"n":"hsize1",")JSON"
    R"JSON(t":">H","r":[0,65535],"f":"d","o":"uint16"},{"n":"hsize2","t":">H","r":[0,65535],"f":"d","o":"uint16)JSON"
    R"JSON("},{"n":"hsize3","t":">H","r":[0,65535],"f":"d","o":"uint16"}]},"actions":[{"n":"baseline","w":"0006)JSON"
    R"JSON(","desc":"Update baseline"},{"n":"_conf.mode","t":"B","r":[0,3],"d":0,"desc":"Sensor mode","map":{"c)JSON"
    R"JSON(entroid":{"w":"000100"},"raw":{"w":"000101"},"baseline":{"w":"000102"},"diff":{"w":"000103"}}},{"n":)JSON"
    R"JSON("_conf.noise","t":"B","w":"0004","r":[0,255],"d":0,"desc":"Noise threshold"},{"n":"_conf.prescaler",)JSON"
    R"JSON("t":"B","w":"0003","r":[0,8],"d":2,"desc":"Prescaler"},{"n":"_conf.minsize","t":">H","w":"0007","r":)JSON"
    R"JSON([0,65535],"d":0,"desc":"Minimum touch size"}]})JSON",
    // CAP1188
    R"JSON({"name":"Capacitive Touch","desc":"8 channel","manu":"Microchip","type":"CAP1188","clas":["TCH"],"re)JSON"
    R"JSON(sp":{"b":10,"a":[{"n":"C1","at":1,"t":"B","r":[0,1],"m":"0x01","s":0,"f":"b","o":"bool"},{"n":"C2",")JSON"
    R"JSON(at":1,"t":"B","r":[0,1],"m":"0x02","s":1,"f":"b","o":"bool"},{"n":"C3","at":1,"t":"B","r":[0,1],"m":)JSON"
    R"JSON("0x04","s":2,"f":"b","o":"bool"},{"n":"C4","at":1,"t":"B","r":[0,1],"m":"0x08","s":3,"f":"b","o":"bo)JSON"
    R"JSON(ol"},{"n":"C5","at":1,"t":"B","r":[0,1],"m":"0x10","s":4,"f":"b","o":"bool"},{"n":"C6","at":1,"t":"B)JSON"
    R"JSON(","r":[0,1],"m":"0x20","s":5,"f":"b","o":"bool"},{"n":"C7","at":1,"t":"B","r":[0,1],"m":"0x40","s":6)JSON"
    R"JSON(,"f":"b","o":"bool"},{"n":"C8","at":1,"t":"B","r":[0,1],"m":"0x80","s":7,"f":"b","o":"bool"},{"n":"d)JSON"
    R"JSON(1","at":2,"t":"b","r":[-128,127],"f":"d","o":"int8"},{"n":"d2","at":3,"t":"b","r":[-128,127],"f":"d")JSON"
    R"JSON(,"o":"int8"},{"n":"d3","at":4,"t":"b","r":[-128,127],"f":"d","o":"int8"},{"n":"d4","at":5,"t":"b","r)JSON"
    R"JSON(":[-128,127],"f":"d","o":"int8"},{"n":"d5","at":6,"t":"b","r":[-128,127],"f":"d","o":"int8"},{"n":"d)JSON"
    R"JSON(6","at":7,"t":"b","r":[-128,127],"f":"d","o":"int8"},{"n":"d7","at":8,"t":"b","r":[-128,127],"f":"d")JSON"
    R"JSON(,"o":"int8"},{"n":"d8","at":9,"t":"b","r":[-128,127],"f":"d","o":"int8"}]}})JSON",
    // VL6180
    R"JSON({"name":"Distance Sensor","desc":"","manu":"ST","type":"VL6180","clas":["DIST"],"resp":{"b":4,"a":[{)JSON"
    R"JSON("n":"valid","t":"B","at":0,"u":"","r":[0,1],"m":"0xF0","s":4,"f":"b","o":"bool","vs":false,"lut":[{")JSON"
    R"JSON(r":"0","v":1},{"r":"","v":0}]},{"n":"err","t":"B","at":0,"u":"","r":[0,15],"m":"0xF0","s":4,"f":"d",)JSON"
    R"JSON("vs":false,"vl":{"0":"OK","1":"VCSEL cont","2":"VCSEL test","3":"VCSEL wdog","4":"PLL1","5":"PLL2",")JSON"
    R"JSON(6":"early conv","7":"max conv","8":"no target","9":"unused","10":"unused","11":"SNR","12":"raw under)JSON"
    R"JSON(","13":"raw over","14":"underflow","15":"overflow"}},{"n":"fresh","t":"B","at":2,"u":"","r":[0,1],"m)JSON"
    R"JSON(":"0x04","s":2,"f":"b","o":"bool","vs":false},{"n":"dist","t":"B","at":3,"u":"mm","r":[0,255],"f":"3)JSON"
    R"JSON(d","o":"float","vft":"valid"}]},"actions":[{"n":"_conf.rate","t":"B","r":[1,50],"d":5,"desc":"Sample)JSON"
    R"JSON( rate (Hz)","map":{"1":{"w":"001801&001b08&001c31&010a30&001507&001803","i":1000000,"s":1},"2":{"w":)JSON"
    R"JSON("001801&001b08&001c31&010a30&001507&001803","i":500000,"s":1},"5":{"w":"001801&001b08&001c31&010a30&)JSON"
    R"JSON(001507&001803","i":200000,"s":1},"10":{"w":"001801&001b08&001c31&010a30&001507&001803","i":100000,"s)JSON"
    R"JSON(":1},"20":{"w":"001801&001b03&001c19&010a30&001507&001803","i":50000,"s":1},"50":{"w":"001801&001b00)JSON"
    R"JSON(&001c03&010a08&001507&001803","i":20000,"s":1}}}]})JSON",
    // STCC4
    R"JSON({"name":"CO2 Sensor","desc":"CO2, Temp & Humidity","manu":"Sensirion","type":"STCC4","clas":["CO2",")JSON"
    R"JSON(TEMP","RH"],"resp":{"b":12,"a":[{"n":"CO2","at":0,"t":">h","u":"ppm","r":[0,32000],"f":"d","o":"int1)JSON"
    R"JSON(6"},{"n":"temperature","at":3,"t":">H","u":"&deg;C","r":[-45,130],"d":374.48571428571427,"a":-45,"f")JSON"
    R"JSON(:".2f","o":"float"},{"n":"humidity","at":6,"t":">H","u":"%","r":[0,100],"d":524.28,"a":-6,"f":".1f",)JSON"
    R"JSON("o":"float"},{"n":"status","at":9,"t":">H","u":"","r":[0,65535],"f":"04x","o":"uint16","vs":false,"b)JSON"
    R"JSON(l":{"0":"OK","0x0040":"testing"}}]},"actions":[{"n":"_conf.rate","r":[0.0167,0.667],"d":0.667,"desc")JSON"
    R"JSON(:"Sample rate (Hz)","map":{"0.667":{"i":1500000,"s":1},"0.5":{"i":2000000,"s":1},"0.2":{"i":5000000,)JSON"
    R"JSON("s":1},"0.1":{"i":10000000,"s":1},"0.0333":{"i":30000000,"s":1},"0.0167":{"i":60000000,"s":1}}},{"n")JSON"
    R"JSON(:"testingMode","map":{"0":"3f3d","1":"3fbc"},"desc":"Testing mode"},{"n":"pressure","t":">H","w":"e0)JSON"
    R"JSON(16","crc":"crc8w","mul":50,"r":[400,1100],"d":1013,"desc":"Pressure (hPa)"}]})JSON",
};

struct DecoderTaskResult
{
    uint32_t made = 0;
    uint32_t maxUs = 0;
    uint32_t totalUs = 0;
    uint32_t largestJson = 0;
    UBaseType_t minFreeStack = 0;
    SemaphoreHandle_t done = nullptr;
};

static void decoderTask(void* pArg)
{
    DecoderTaskResult& result = *static_cast<DecoderTaskResult*>(pArg);

    // A table with 10 types of these layouts added (as from a type file), each identified in turn
    static const uint32_t NUM_TYPES = 10;
    static const uint32_t NUM_LAYOUTS = sizeof(LARGE_DEV_INFO_JSONS) / sizeof(LARGE_DEV_INFO_JSONS[0]);
    DeviceTypeRecords table;
    table.setDecoderPolicy(DeviceTypeRecords::DecoderPolicy::OnIdentify, UINT32_MAX);
    for (uint32_t i = 0; i < NUM_TYPES; i++)
    {
        const char* pDevInfoJson = LARGE_DEV_INFO_JSONS[i % NUM_LAYOUTS];
        result.largestJson = std::max(result.largestJson, (uint32_t)strlen(pDevInfoJson));
        uint16_t pollBytes = RaftJson(pDevInfoJson, false).getLong("resp/b", 0);
        DeviceTypeRecordDynamic dyn((String("Large") + String(i)).c_str(), "0x50", "0x0f=0x33", "",
                    R"({"c":"0x00=r6","i":100,"s":1})", pollBytes, pDevInfoJson, nullptr);
        DeviceTypeIndexType idx = 0;
        table.addRecord(dyn, idx);
    }
    uint32_t numBase = DeviceTypeRecords::getNumBaseRecords();
    for (uint32_t pos = 0; pos < table.getExtendedRecordStats().count; pos++)
    {
        uint64_t startUs = micros();
        bool made = table.ensureDecoder(numBase + pos);
        uint32_t elapsedUs = (uint32_t)(micros() - startUs);
        result.made += made ? 1 : 0;
        result.totalUs += elapsedUs;
        result.maxUs = std::max(result.maxUs, elapsedUs);
    }
    result.minFreeStack = uxTaskGetStackHighWaterMark(nullptr);
    xSemaphoreGive(result.done);
    vTaskDelete(nullptr);
}

TEST_CASE("runtime decoders: stack and time to make them", "[DeviceTypeTable]")
{
    static const uint32_t TASK_STACK_BYTES = 5000;
    DecoderTaskResult result;
    result.done = xSemaphoreCreateBinary();
    TEST_ASSERT_EQUAL(pdPASS, xTaskCreate(decoderTask, "decTest", TASK_STACK_BYTES, &result, 5, nullptr));
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(result.done, pdMS_TO_TICKS(10000)));
    vSemaphoreDelete(result.done);
    uint32_t stackUsed = TASK_STACK_BYTES - result.minFreeStack;
    LOG_I(MODULE_PREFIX, "decoders made on a %d byte stack: %d (largest devInfoJson %d bytes); stack used %d bytes "
                "(%d free); time mean %d us max %d us", (int)TASK_STACK_BYTES, (int)result.made,
                (int)result.largestJson, (int)stackUsed, (int)result.minFreeStack,
                result.made ? (int)(result.totalUs / result.made) : 0, (int)result.maxUs);
    TEST_ASSERT_EQUAL_UINT32(10, result.made);
    // Measured 3.3 KB here (the test's own frames included) and 10 ms at most: a regression shows here
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(4000, stackUsed);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(20000, result.maxUs);
}

