/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// test_poll_data_decode
//
// Decoding poll data as on-device consumers get it (DeviceTypeRecords::decodePollData and
// decodeNewestPollRecord), and reading values from decoded records (AttrFieldDesc::getValue).
//
// A data subscriber and the bus's latest value get each poll result at its actual length. A poll with a variable
// read length (an LSM6DS FIFO read) is shorter than the record size the decoders read, so it must be padded as the
// poll store pads it. The decoders themselves are unchanged: test_runtime_decoder checks the compiled, runtime
// and interpreted decoders against each other, truncated buffers included.
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <string.h>
#include <random>
#include <string>
#include <vector>
#include "testhdr.h"
#include "DeviceTypeRecords.h"
#include "RaftJson.h"

static uint8_t randomByte(std::mt19937& rng)
{
    static const uint8_t EDGES[] = {0x00, 0xff, 0x7f, 0x80, 0x01, 0xfe};
    return (rng() % 10 < 4) ? EDGES[rng() % sizeof(EDGES)] : (uint8_t)rng();
}

static const uint32_t TS_BYTES = DevicePollingInfo::POLL_RESULT_TIMESTAMP_SIZE;

// Every compiled record: data with a short last record decodes as the same data padded with zeros (what the poll
// store holds) decodes with the record's decoder
static void testShortRecordsDecodeAsPadded()
{
    printf("Running testShortRecordsDecodeAsPadded...\n");
    DeviceTypeRecords table;
    std::mt19937 rng(4321);
    uint32_t numChecked = 0;
    bool allSame = true;
    for (uint32_t idx = 0; idx < DeviceTypeRecords::getNumBaseRecords(); idx++)
    {
        const DeviceTypeRecord* pRec = table.getRecord(idx);
        if (!pRec || !pRec->pollResultDecodeFn || (pRec->pollStructSize == 0))
            continue;
        const uint32_t recBytes = pRec->pollDataSizeBytes + TS_BYTES;
        const uint32_t structSize = pRec->pollStructSize;
        bool same = true;
        for (int iter = 0; (iter < 500) && same; iter++)
        {
            // Whole records, then a last one cut short (or not)
            uint32_t numWhole = rng() % 3;
            uint32_t lastLen = (rng() % 4 == 0) ? recBytes : (rng() % (recBytes + 1));
            std::vector<uint8_t> data(numWhole * recBytes + lastLen);
            for (auto& b : data)
                b = randomByte(rng);
            // The padded reference: a last record shorter than the timestamp isn't a record
            std::vector<uint8_t> padded(data);
            if (lastLen >= TS_BYTES)
                padded.resize((numWhole + 1) * recBytes, 0);
            else
                padded.resize(numWhole * recBytes);

            const uint16_t maxRecs = 1 + rng() % 24;
            // Zeroed as decodeNewestPollRecord's working space is: decoders leave a struct's padding bytes as they are
            std::vector<uint8_t> outA(structSize * maxRecs, 0), outB(structSize * maxRecs, 0);
            RaftBusDeviceDecodeState stateA, stateB;
            uint32_t nA = table.decodePollData(idx, data.data(), data.size(), outA.data(), outA.size(), maxRecs, stateA);
            uint32_t nB = padded.empty() ? 0 :
                        pRec->pollResultDecodeFn(padded.data(), padded.size(), outB.data(), outB.size(), maxRecs, stateB);
            same = (nA == nB) && (memcmp(outA.data(), outB.data(), nA * structSize) == 0) &&
                        (stateA.lastReportTimestampUs == stateB.lastReportTimestampUs);
            if (!same)
                printf("  %s: %u records vs %u padded (iteration %d, length %u)\n", pRec->deviceType, nA, nB, iter,
                            (unsigned)data.size());

            // The newest record is the last of them
            std::vector<uint8_t> newest(structSize, 0);
            RaftBusDeviceDecodeState stateC;
            uint32_t nC = table.decodeNewestPollRecord(idx, data.data(), data.size(), newest.data(), newest.size(), stateC);
            if (nB > 0 && nB < maxRecs)
                same = same && (nC == nB) && (memcmp(newest.data(), outB.data() + (nB - 1) * structSize, structSize) == 0);
        }
        TEST_ASSERT(same, (std::string(pRec->deviceType) + ": short records decode as padded").c_str());
        allSame = allSame && same;
        numChecked++;
    }
    printf("  %u records checked\n", numChecked);
    TEST_ASSERT(numChecked >= 55, "every compiled record checked");
    TEST_ASSERT(allSame, "all the same");
}

// A real LSM6DS3TR-C FIFO poll as a subscriber gets it: unpadded it decodes to nothing, through decodePollData it
// gives every sample, and decodeNewestPollRecord gives the newest
static void testLsm6dsFifoPoll()
{
    printf("Running testLsm6dsFifoPoll...\n");
    DeviceTypeRecords table;
    DeviceTypeIndexType idx = 0;
    const DeviceTypeRecord* pRec = table.getRecord("LSM6DS3TRC", idx);
    TEST_ASSERT(pRec && pRec->pollResultDecodeFn, "LSM6DS3TRC compiled");
    if (!pRec || !pRec->pollResultDecodeFn)
        return;

    // Poll: timestamp, OUT_TEMP (2), FIFO_STATUS1-4 (4), then 5 samples of 6 words (gx gy gz ax ay az)
    const uint32_t NUM_SAMPLES = 5;
    std::vector<uint8_t> poll = {0x00, 0x64,                // timestamp
                                 0x00, 0x02,                // temperature: 512 / 256 + 25 = 27 degC
                                 NUM_SAMPLES * 6, 0x00,     // FIFO words
                                 0x00, 0x00};               // pattern 0 (aligned)
    for (uint32_t i = 0; i < NUM_SAMPLES; i++)
    {
        const int16_t words[6] = {(int16_t)(100 * i), -50, 7, (int16_t)(-8192 + (int16_t)i), 0, 8192};
        for (int16_t w : words)
        {
            poll.push_back((uint8_t)(w & 0xff));
            poll.push_back((uint8_t)((uint16_t)w >> 8));
        }
    }
    TEST_ASSERT(poll.size() < pRec->pollDataSizeBytes + TS_BYTES, "the poll is shorter than the record size");

    std::vector<uint8_t> out(pRec->pollStructSize * 32, 0);
    RaftBusDeviceDecodeState state1, state2, state3;
    TEST_ASSERT(pRec->pollResultDecodeFn(poll.data(), poll.size(), out.data(), out.size(), 32, state1) == 0,
                "the decoder alone decodes nothing from the unpadded poll");
    uint32_t n = table.decodePollData(idx, poll.data(), poll.size(), out.data(), out.size(), 32, state2);
    TEST_ASSERT(n == NUM_SAMPLES, "decodePollData gives every sample");

    // Field values from the newest sample
    std::vector<uint8_t> newest(pRec->pollStructSize, 0);
    uint32_t nNewest = table.decodeNewestPollRecord(idx, poll.data(), poll.size(), newest.data(), newest.size(), state3);
    TEST_ASSERT(nNewest == NUM_SAMPLES, "decodeNewestPollRecord counts the samples");
    auto value = [&](const char* name, const uint8_t* pRecord) {
        for (uint32_t i = 0; i < pRec->pollFieldCount; i++)
        {
            double v = 0;
            if ((strcmp(pRec->pollFieldDescs[i].name, name) == 0) &&
                        pRec->pollFieldDescs[i].getValue(pRecord, pRec->pollStructSize, 0, v))
                return v;
        }
        return -9999.0;
    };
    const double gyroLsb = 1.0 / 14.285714;
    TEST_ASSERT(fabs(value("gx", newest.data()) - 400 * gyroLsb) < 1e-3, "newest gx (sample 4)");
    TEST_ASSERT(fabs(value("ax", newest.data()) - (-8188.0 / 8192)) < 1e-6, "newest ax");
    TEST_ASSERT(fabs(value("az", newest.data()) - 1.0) < 1e-6, "az 1 g");
    TEST_ASSERT(fabs(value("temp", newest.data()) - 27.0) < 1e-6, "temperature");
    TEST_ASSERT(fabs(value("gx", out.data()) - 0.0) < 1e-6, "first sample gx");
}

// Reading values: every compiled record's fields, every element of array attributes, against the struct bytes
static void testFieldValues()
{
    printf("Running testFieldValues...\n");
    DeviceTypeRecords table;
    std::mt19937 rng(99);
    uint32_t numArrays = 0, numFields = 0;
    bool allRight = true;
    for (uint32_t idx = 0; idx < DeviceTypeRecords::getNumBaseRecords(); idx++)
    {
        const DeviceTypeRecord* pRec = table.getRecord(idx);
        if (!pRec || !pRec->pollFieldDescs || (pRec->pollStructSize == 0))
            continue;
        std::vector<uint8_t> rec(pRec->pollStructSize);
        for (auto& b : rec)
            b = (uint8_t)rng();
        for (uint32_t f = 0; f < pRec->pollFieldCount; f++)
        {
            const AttrFieldDesc& desc = pRec->pollFieldDescs[f];
            const uint32_t elemBytes = desc.elemBytes();
            for (uint16_t e = 0; e < desc.elemCount(); e++)
            {
                // The reference: the element's bytes as the field's type, then the divisor and addend
                const uint8_t* p = rec.data() + desc.offset + e * elemBytes;
                double ref = 0;
                switch (desc.type)
                {
                    case AttrType::Float:  { float v; memcpy(&v, p, 4); ref = v; break; }
                    case AttrType::Int32:  { int32_t v; memcpy(&v, p, 4); ref = v; break; }
                    case AttrType::Uint32: { uint32_t v; memcpy(&v, p, 4); ref = v; break; }
                    case AttrType::Int16:  { int16_t v; memcpy(&v, p, 2); ref = v; break; }
                    case AttrType::Uint16: { uint16_t v; memcpy(&v, p, 2); ref = v; break; }
                    case AttrType::Int8:   ref = (int8_t)*p; break;
                    case AttrType::Uint8:  ref = *p; break;
                    case AttrType::Bool:   ref = *p ? 1 : 0; break;
                }
                if ((desc.divisor != 0.0f) && (desc.divisor != 1.0f))
                    ref /= desc.divisor;
                ref += desc.addend;
                double v = 0;
                bool ok = desc.getValue(rec.data(), rec.size(), e, v);
                bool right = ok && ((v == ref) || (std::isnan(v) && std::isnan(ref)));
                if (!right)
                    printf("  %s %s[%u]: %g, expected %g\n", pRec->deviceType, desc.name, e, v, ref);
                allRight = allRight && right;
                numFields++;
            }
            if (desc.elemCount() > 1)
                numArrays++;
            double v = 0;
            TEST_ASSERT(!desc.getValue(rec.data(), rec.size(), desc.elemCount(), v), "an element past the end is refused");
            TEST_ASSERT(!desc.getValue(rec.data(), desc.offset, 0, v), "a field past the record's end is refused");
        }
    }
    printf("  %u values read, %u array attributes\n", numFields, numArrays);
    TEST_ASSERT(allRight, "every value as its struct bytes");
    TEST_ASSERT(numArrays >= 5, "array attributes read element by element");

    // Integral: an integer with nothing to scale
    AttrFieldDesc d = {"x", 0, AttrType::Uint16, "", 1.0f, 0.0f, 1};
    TEST_ASSERT(d.isIntegral(), "uint16 integral");
    d.divisor = 8192.0f;
    TEST_ASSERT(!d.isIntegral(), "scaled not integral");
    d = {"x", 0, AttrType::Float, "", 1.0f, 0.0f, 1};
    TEST_ASSERT(!d.isIntegral(), "float not integral");
}

int main()
{
    testShortRecordsDecodeAsPadded();
    testLsm6dsFifoPoll();
    testFieldValues();
    return testSummary("test_poll_data_decode");
}
