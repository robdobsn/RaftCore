/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// test_runtime_decoder
//
// The runtime decoder (DeviceTypeRuntimeDecoder, RAFT_RUNTIME_DECODE) against the compiled decoders: for every
// compiled record with a standard decode, a decoder made from the record's own devInfoJson must give the same
// struct layout and field descriptions, and decode random poll results to the same bytes. Then its use by
// DeviceTypeRecords: a record with no compiled decoder (a type file record) gets a runtime one.
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <string.h>
#include <random>
#include <string>
#include <vector>
#include "testhdr.h"
#include "DeviceTypeRecords.h"
#include "DeviceTypeRuntimeDecoder.h"
#include "DeviceTypeFileLoader.h"
#include "RaftJson.h"

static uint32_t fieldBytes(const AttrFieldDesc& desc, const DeviceTypeRuntimeDecoder::Field* pField)
{
    (void)pField;
    switch (desc.type)
    {
        case AttrType::Float: case AttrType::Int32: case AttrType::Uint32: return 4;
        case AttrType::Int16: case AttrType::Uint16: return 2;
        default: return 1;
    }
}

// A random poll byte, biased towards the values that exercise sign, mask and range edges
static uint8_t randomByte(std::mt19937& rng)
{
    static const uint8_t EDGES[] = {0x00, 0xff, 0x7f, 0x80, 0x01, 0xfe};
    uint32_t r = rng() % 10;
    if (r < 4)
        return EDGES[rng() % sizeof(EDGES)];
    return (uint8_t)rng();
}

// Every compiled record with a standard decode: the runtime decoder made from its JSON matches the compiled one
static void testMatchesCompiledDecoders()
{
    printf("Running testMatchesCompiledDecoders...\n");
    DeviceTypeRecords table;
    std::mt19937 rng(12345);
    uint32_t numCompared = 0, numCustom = 0, numMismatched = 0;
    for (uint32_t idx = 0; idx < DeviceTypeRecords::getNumBaseRecords(); idx++)
    {
        const DeviceTypeRecord* pRec = table.getRecord(idx);
        if (!pRec || !pRec->pollResultDecodeFn || !pRec->devInfoJson)
            continue;
        RaftJson devInfo(pRec->devInfoJson, false);
        int numAttrs = 0;
        if ((devInfo.getType("resp/a", numAttrs) != RaftJsonIF::RAFT_JSON_ARRAY) || (numAttrs == 0))
            continue;   // nothing to decode (e.g. an output-only device)
        if (devInfo.contains("resp/c"))
        {
            // Custom decode code needs the code interpreter: no runtime decoder
            String problem;
            DeviceTypeRuntimeDecoder* pDecoder = DeviceTypeRuntimeDecoder::create(pRec->devInfoJson, problem);
            TEST_ASSERT(!pDecoder && (problem.indexOf("code") >= 0), (std::string(pRec->deviceType) + ": custom decode refused").c_str());
            delete pDecoder;
            numCustom++;
            continue;
        }
        String problem;
        DeviceTypeRuntimeDecoder* pDecoder = DeviceTypeRuntimeDecoder::create(pRec->devInfoJson, problem);
        std::string name = pRec->deviceType;
        TEST_ASSERT(pDecoder, (name + ": decoder made " + problem.c_str()).c_str());
        if (!pDecoder)
            continue;

        // Layout and field descriptions
        bool layoutSame = (pDecoder->getStructSize() == pRec->pollStructSize) && (pDecoder->getFieldCount() == pRec->pollFieldCount);
        for (uint32_t i = 0; layoutSame && (i < pRec->pollFieldCount); i++)
        {
            const AttrFieldDesc& a = pRec->pollFieldDescs[i];
            const AttrFieldDesc& b = pDecoder->getFieldDescs()[i];
            layoutSame = (strcmp(a.name, b.name) == 0) && (a.offset == b.offset) && (a.type == b.type) &&
                        (strcmp(a.fmtStr ? a.fmtStr : "", b.fmtStr ? b.fmtStr : "") == 0) && (a.divisor == b.divisor) &&
                        (a.addend == b.addend) && (a.count == b.count);
            if (!layoutSame)
                printf("  %s field %s: offset %u/%u type %d/%d count %u/%u\n", name.c_str(), a.name, a.offset, b.offset,
                            (int)a.type, (int)b.type, a.count, b.count);
        }
        TEST_ASSERT(layoutSame, (name + ": struct layout and field descriptions").c_str());
        if (!layoutSame)
        {
            numMismatched++;
            delete pDecoder;
            continue;
        }

        // Decode random poll results: whole records, several at once, and buffers that end part way through one
        RaftJson resp(pRec->devInfoJson, false);
        uint32_t recBytes = resp.getLong("resp/b", 0) + DevicePollingInfo::POLL_RESULT_TIMESTAMP_SIZE;
        uint32_t structSize = pRec->pollStructSize;
        bool decodesSame = true;
        for (int iter = 0; (iter < 3000) && decodesSame; iter++)
        {
            uint32_t numRecs = 1 + rng() % 3;
            uint32_t bufLen = recBytes * numRecs;
            if (rng() % 8 == 0)
                bufLen = rng() % (bufLen + 1);
            std::vector<uint8_t> buf(bufLen);
            for (auto& b : buf)
                b = randomByte(rng);
            const uint16_t maxRecs = 4;
            std::vector<uint8_t> outCompiled(structSize * maxRecs, 0xa5), outRuntime(structSize * maxRecs, 0xa5);
            RaftBusDeviceDecodeState stateCompiled, stateRuntime;
            stateCompiled.lastReportTimestampUs = stateRuntime.lastReportTimestampUs = (rng() % 2) ? 0 : 6553500;
            uint32_t nCompiled = pRec->pollResultDecodeFn(buf.data(), buf.size(), outCompiled.data(), outCompiled.size(), maxRecs, stateCompiled);
            uint32_t nRuntime = pDecoder->decode(buf.data(), buf.size(), outRuntime.data(), outRuntime.size(), maxRecs, stateRuntime);
            decodesSame = (nCompiled == nRuntime) && (stateCompiled.lastReportTimestampUs == stateRuntime.lastReportTimestampUs) &&
                        (stateCompiled.reportTimestampOffsetUs == stateRuntime.reportTimestampOffsetUs);
            for (uint32_t r = 0; decodesSame && (r < nCompiled); r++)
            {
                const uint8_t* pA = outCompiled.data() + r * structSize;
                const uint8_t* pB = outRuntime.data() + r * structSize;
                decodesSame = memcmp(pA, pB, sizeof(uint32_t)) == 0;
                for (uint32_t i = 0; decodesSame && (i < pRec->pollFieldCount); i++)
                {
                    const AttrFieldDesc& desc = pRec->pollFieldDescs[i];
                    uint32_t bytes = fieldBytes(desc, nullptr) * (desc.count ? desc.count : 1);
                    // Fields wider than their description type (int64, uint64, double) are compared in full
                    uint32_t nextOffset = (i + 1 < pRec->pollFieldCount) ? pRec->pollFieldDescs[i + 1].offset : structSize;
                    if (nextOffset - desc.offset >= 8 * (desc.count ? desc.count : 1))
                        bytes = 8 * (desc.count ? desc.count : 1);
                    if (memcmp(pA + desc.offset, pB + desc.offset, bytes) != 0)
                    {
                        decodesSame = false;
                        printf("  %s: field %s differs (iteration %d, record %u, buffer", name.c_str(), desc.name, iter, r);
                        for (uint8_t b : buf)
                            printf(" %02x", b);
                        printf(")\n");
                    }
                }
            }
            if (!decodesSame && (nCompiled != nRuntime))
                printf("  %s: %u records compiled, %u runtime\n", name.c_str(), nCompiled, nRuntime);
        }
        TEST_ASSERT(decodesSame, (name + ": decodes the same as the compiled decoder").c_str());
        if (!decodesSame)
            numMismatched++;
        numCompared++;
        delete pDecoder;
    }
    printf("  %u records compared with their compiled decoders, %u with custom decode code\n", numCompared, numCustom);
    TEST_ASSERT(numCompared >= 40, "most records compared");
    TEST_ASSERT(numMismatched == 0, "no mismatches");
    TEST_ASSERT(DeviceTypeRuntimeDecoder::getNumSlotsInUse() == 0, "slots freed with their decoders");
}

// A type file record with no compiled decoder gets a runtime decoder, used through the record's decode function
static void testTypeFileRecordGetsRuntimeDecoder()
{
    printf("Running testTypeFileRecordGetsRuntimeDecoder...\n");
    const char* file = R"({"devTypes":{"NewTemp":{"addresses":"0x31","detectionValues":"0x0f=0x42",)"
                R"("pollInfo":{"c":"0x00=r4","i":1000,"s":1},)"
                R"("devInfoJson":{"name":"New temperature","type":"NewTemp","resp":{"b":4,"a":[)"
                R"({"n":"temperature","t":">h","u":"&deg;C","r":[-40,125],"d":256,"f":".2f","o":"float"},)"
                R"({"n":"status","t":"B","f":"02x","o":"uint8","bl":{"0x01":"ready"}},)"
                R"({"n":"flag","t":"B","at":3,"m":"0x80","s":7,"f":"b","o":"bool"}]}}}}})";
    {
        DeviceTypeRecords table;
        DeviceTypeFileLoader::Result result;
        DeviceTypeFileLoader::applyJson(table, file, DeviceTypeFileLoader::Limits(), result);
        TEST_ASSERT((result.added == 1) && (result.rejected == 0), ("added: " + std::string(result.firstError.c_str())).c_str());
        DeviceTypeIndexType idx = DEVICE_TYPE_INDEX_INVALID;
        const DeviceTypeRecord* pRec = table.getRecord("NewTemp", idx);
        TEST_ASSERT(pRec && pRec->pollResultDecodeFn && pRec->pollFieldDescs && (pRec->pollFieldCount == 3),
                    "runtime decoder attached");
        TEST_ASSERT(table.getDecoderKind(idx) == DeviceTypeRecords::DecoderKind::Runtime, "decoder kind runtime");
        TEST_ASSERT(table.getDecoderKind(0) == DeviceTypeRecords::DecoderKind::Compiled, "a base record's decoder is compiled");
        if (pRec && pRec->pollResultDecodeFn)
        {
            // 0x1280 = 18.5 C, status 0x01, flag in bit 7 of byte 3
            const uint8_t poll[] = {0x00, 0x0a, 0x12, 0x80, 0x01, 0x80};
            std::vector<uint8_t> out(pRec->pollStructSize, 0);
            RaftBusDeviceDecodeState state;
            uint32_t n = pRec->pollResultDecodeFn(poll, sizeof(poll), out.data(), out.size(), 1, state);
            float temperature = 0;
            memcpy(&temperature, out.data() + pRec->pollFieldDescs[0].offset, sizeof(temperature));
            uint8_t status = out[pRec->pollFieldDescs[1].offset];
            bool flag = out[pRec->pollFieldDescs[2].offset] != 0;
            uint32_t timeMs = 0;
            memcpy(&timeMs, out.data(), sizeof(timeMs));
            TEST_ASSERT((n == 1) && (temperature == 18.5f) && (status == 1) && flag && (timeMs == 1),
                        "decoded through the record's decode function");
        }

        // The same record again is the same record (its attached decoder doesn't make it differ)
        DeviceTypeFileLoader::Result again;
        DeviceTypeFileLoader::applyJson(table, file, DeviceTypeFileLoader::Limits(), again);
        TEST_ASSERT((again.rejected == 0) && (again.added == 0), ("the same file again: " + std::string(again.firstError.c_str())).c_str());
        TEST_ASSERT(DeviceTypeRuntimeDecoder::getNumSlotsInUse() == 1, "one slot in use");
    }
    TEST_ASSERT(DeviceTypeRuntimeDecoder::getNumSlotsInUse() == 0, "slot freed with the table");

    // A type file record with custom decode code: no decoder (until the code interpreter)
    {
        DeviceTypeRecords table;
        DeviceTypeFileLoader::Result result;
        DeviceTypeFileLoader::applyJson(table, R"({"devTypes":{"NewCode":{"addresses":"0x32","detectionValues":"0x0f=0x43",)"
                    R"("pollInfo":{"c":"0x00=r2","i":1000,"s":1},)"
                    R"("devInfoJson":{"type":"NewCode","resp":{"b":2,"a":[{"n":"v","t":"<f","o":"float"}],"c":{"n":"f","c":"out.v=buf[0];"}}}}}})",
                    DeviceTypeFileLoader::Limits(), result);
        DeviceTypeIndexType idx = DEVICE_TYPE_INDEX_INVALID;
        const DeviceTypeRecord* pRec = table.getRecord("NewCode", idx);
        TEST_ASSERT(pRec && !pRec->pollResultDecodeFn && (table.getDecoderKind(idx) == DeviceTypeRecords::DecoderKind::None),
                    "custom decode code: no decoder");
    }
}

// Byte positions listed in "at" are assembled into one value
static void testAtList()
{
    printf("Running testAtList...\n");
    String problem;
    DeviceTypeRuntimeDecoder* pDecoder = DeviceTypeRuntimeDecoder::create(
                R"({"resp":{"b":4,"a":[{"n":"v","t":">H","at":[3,0],"o":"uint16"}]}})", problem);
    TEST_ASSERT(pDecoder, problem.c_str());
    if (!pDecoder)
        return;
    const uint8_t poll[] = {0x00, 0x00, 0x34, 0x00, 0x00, 0x12};
    std::vector<uint8_t> out(pDecoder->getStructSize(), 0);
    RaftBusDeviceDecodeState state;
    uint32_t n = pDecoder->decode(poll, sizeof(poll), out.data(), out.size(), 1, state);
    uint16_t v = 0;
    memcpy(&v, out.data() + pDecoder->getFieldDescs()[0].offset, sizeof(v));
    TEST_ASSERT((n == 1) && (v == 0x1234), "bytes 3 then 0 read as one big-endian value");
    delete pDecoder;
}

// Attributes the build couldn't decode are refused, with a reason
static void testRefused()
{
    printf("Running testRefused...\n");
    const char* bad[] = {
        R"({"resp":{"b":2,"a":[{"n":"v","t":"h"}]}})",                      // no endian on a 2-byte type
        R"({"resp":{"b":2,"a":[{"n":"v","t":">H"},{"n":"v","t":"B"}]}})",   // duplicate name
        R"({"resp":{"b":4,"a":[{"n":"v","t":"<f","x":"0x01"}]}})",          // XOR on a float
        R"({"resp":{"a":[{"n":"v","t":"B"}]}})",                            // no size
        R"({"name":"x"})",                                                  // no response format
    };
    for (const char* json : bad)
    {
        String problem;
        DeviceTypeRuntimeDecoder* pDecoder = DeviceTypeRuntimeDecoder::create(json, problem);
        TEST_ASSERT(!pDecoder && (problem.length() > 0), json);
        delete pDecoder;
    }
}

int main()
{
    testMatchesCompiledDecoders();
    testTypeFileRecordGetsRuntimeDecoder();
    testAtList();
    testRefused();
    return testSummary("test_runtime_decoder");
}
