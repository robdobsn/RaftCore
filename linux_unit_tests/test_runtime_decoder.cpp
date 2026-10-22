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
#include "DeviceTypeCodeInterpreter.h"
#include "DeviceTypeDecodeVerifier.h"
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
static void testMatchesCompiledDecoders(bool bytecode)
{
    printf("Running testMatchesCompiledDecoders (bytecode %s)...\n", bytecode ? "on" : "off");
#ifdef RAFT_RUNTIME_DECODE_CODE
    DeviceTypeCodeProgram::setBytecodeEnabled(bytecode);
#endif
    DeviceTypeRecords table;
    std::mt19937 rng(12345);
    uint32_t numCompared = 0, numCustom = 0, numMismatched = 0, numBytecode = 0;
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
            numCustom++;
#ifndef RAFT_RUNTIME_DECODE_CODE
            // Custom decode code needs the code interpreter: no runtime decoder
            String problem;
            DeviceTypeRuntimeDecoder* pDecoder = DeviceTypeRuntimeDecoder::create(pRec->devInfoJson, problem);
            TEST_ASSERT(!pDecoder && (problem.indexOf("code") >= 0), (std::string(pRec->deviceType) + ": custom decode refused").c_str());
            delete pDecoder;
            continue;
#endif
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
            static const uint16_t MAX_RECS[] = {1, 4, 32};
            const uint16_t maxRecs = MAX_RECS[rng() % 3];
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
        if (pDecoder->getMaxEvalDepth() > 0)
            printf("  %s: interpreter depth eval %u exec %u\n", name.c_str(), pDecoder->getMaxEvalDepth(), pDecoder->getMaxExecDepth());
        if (pDecoder->usesBytecode())
        {
            printf("  %s: bytecode\n", name.c_str());
            numBytecode++;
        }
        TEST_ASSERT(decodesSame, (name + ": decodes the same as the compiled decoder").c_str());
        if (!decodesSame)
            numMismatched++;
        numCompared++;
        delete pDecoder;
    }
    printf("  %u records compared with their compiled decoders, %u of them with custom decode code, %u as bytecode\n",
                numCompared, numCustom, numBytecode);
#ifdef RAFT_RUNTIME_DECODE_CODE
    TEST_ASSERT(numCompared >= 55, "all records compared, custom decode code included");
#else
    TEST_ASSERT(numCompared >= 40, "most records compared");
#endif
    TEST_ASSERT(numMismatched == 0, "no mismatches");
    TEST_ASSERT(DeviceTypeRuntimeDecoder::getNumSlotsInUse() == 0, "slots freed with their decoders");
#ifdef RAFT_RUNTIME_DECODE_CODE
    // All custom decode code runs as bytecode, unless it is turned off
    if (bytecode)
        TEST_ASSERT(numBytecode == numCustom, "custom decode code runs as bytecode");
    else
        TEST_ASSERT(numBytecode == 0, "bytecode turned off");
#endif
}

// A type file record with no compiled decoder gets a runtime decoder when a device of the type is identified
// (ensureDecoder, from DeviceManager), used through the record's decode function
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

        // Checked when added, made when a device of the type is identified
        TEST_ASSERT(pRec && !pRec->pollResultDecodeFn && !pRec->pollFieldDescs, "no decoder until identified");
        TEST_ASSERT(table.getDecoderKind(idx) == DeviceTypeRecords::DecoderKind::RuntimePending, "decoder kind runtimePending");
        TEST_ASSERT(DeviceTypeRuntimeDecoder::getNumSlotsInUse() == 0, "no slot until identified");
        DevicePollingInfo pollingInfo;
        table.getPollInfo(0x31, idx, pollingInfo);
        TEST_ASSERT(pRec && !pRec->pollResultDecodeFn, "polling info doesn't make it (the bus task's stack is too small)");
        TEST_ASSERT(table.ensureDecoder(idx), "ensureDecoder");
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
        TEST_ASSERT(table.ensureDecoder(idx), "ensureDecoder again");
        TEST_ASSERT(DeviceTypeRuntimeDecoder::getNumSlotsInUse() == 1, "one slot in use");
        TEST_ASSERT(table.getExtendedRecordStats().decodersMade == 1, "one decoder made");
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
#ifdef RAFT_RUNTIME_DECODE_CODE
        // Decoded by the code interpreter
        TEST_ASSERT(pRec && pRec->pollResultDecodeFn && (table.getDecoderKind(idx) == DeviceTypeRecords::DecoderKind::Runtime),
                    "custom decode code: runtime decoder");
        if (pRec && pRec->pollResultDecodeFn)
        {
            const uint8_t poll[] = {0x00, 0x01, 0x2a, 0x00};
            std::vector<uint8_t> out(pRec->pollStructSize, 0);
            RaftBusDeviceDecodeState state;
            uint32_t n = pRec->pollResultDecodeFn(poll, sizeof(poll), out.data(), out.size(), 1, state);
            float v = 0;
            memcpy(&v, out.data() + pRec->pollFieldDescs[0].offset, sizeof(v));
            TEST_ASSERT((n == 1) && (v == 42.0f), "custom decode code run");
        }
#else
        TEST_ASSERT(pRec && !pRec->pollResultDecodeFn && (table.getDecoderKind(idx) == DeviceTypeRecords::DecoderKind::None),
                    "custom decode code: no decoder");
#endif
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

#ifdef RAFT_RUNTIME_DECODE_CODE
// The interpreter against the same code compiled as C++: each case's code assigns out.r (a double field, so the
// value isn't narrowed) and the expected value is the C++ expression
static void testInterpreterSemantics()
{
    printf("Running testInterpreterSemantics...\n");
    std::vector<DeviceTypeCodeProgram::OutField> fields(1);
    fields[0].name = "r";
    fields[0].type = DeviceTypeRuntimeDecoder::NumType::Double;
    fields[0].offset = 0;
    const uint8_t buf[] = {0xff, 0x80, 0x01, 0x7f};
    struct Case
    {
        const char* code;
        double expected;
    };
    int i1 = -7, i2 = 2;
    unsigned int u1 = 1;
    float f1 = 0.1f;
    int g1 = 23;
    uint16_t c16 = (uint16_t)70000;
    int sh = (buf[0] << 24) >> 24;
    int x = 5; int pre = ++x; int post = x++;
    int y = 10; y += 3; y <<= 2; y %= 7;
    float fl = 1.0f; fl = fl / 3.0;
    float fl2 = 1.0f; fl2 = fl2 / 3;
    const Case cases[] = {
        {"out.r=-7/2;", (double)(i1 / i2)},
        {"out.r=-7%2;", (double)(i1 % i2)},
        {"out.r=-1<1;", (double)(-1 < 1)},
        {"uint32_t u=1;out.r=-1<u;", (double)(-1 < u1)},               // unsigned comparison: false
        {"out.r=0x7fffffff+1;", (double)(int32_t)((uint32_t)0x7fffffff + 1)},
        {"out.r=0xffffffff+1;", (double)(0xffffffffu + 1)},             // unsigned int wraps
        {"float f=0.1;out.r=f;", (double)f1},                           // a double literal stored to a float
        {"int g1=23;out.r=g1/16.0+49.0;", (double)(g1 / 16.0 + 49.0)},  // double arithmetic
        {"float v=1.0;v=v/3.0;out.r=v;", (double)fl},
        {"float v=1.0;v=v/3;out.r=v;", (double)fl2},
        {"out.r=(uint16_t)70000;", (double)c16},
        {"out.r=(buf[0]<<24)>>24;", (double)sh},                        // arithmetic right shift of a negative int
        {"out.r=buf[1]|(buf[2]<<8);", (double)(buf[1] | (buf[2] << 8))},
        {"int x=5;int pre=++x;int post=x++;out.r=pre*100+post*10+x;", (double)(pre * 100 + post * 10 + x)},
        {"int y=10;y+=3;y<<=2;y%=7;out.r=y;", (double)y},
        {"int n=0;int i=0;while(i<5){if(i==2){n=n+10;}else{n=n+1;}i++;}out.r=n;", 14.0},
        {"out.r=!0+!5+(3&&0)+(0||2)+~0;", (double)(!0 + !5 + (3 && 0) + (0 || 2) + ~0)},
        {"out.r=buf[100];", 0.0},                                       // outside buf: 0
        {"out.r=7/0;", 0.0},                                            // integer division by zero: 0
        {"int8_t c=200;out.r=c;", (double)(int8_t)200},
        // Bytecode paths: combined instructions, narrowing, compound assignment, && and || as values
        {"int16_t s=40000;out.r=s;", (double)(int16_t)40000},
        {"uint8_t b=0;b-=1;out.r=b;", 255.0},
        {"uint16_t w=65535;w++;out.r=w;", 0.0},
        {"int k=0;k^=0x55;k|=0x100;k&=0x1F0;k>>=2;k*=3;k-=1;out.r=k;", (double)(((((0 ^ 0x55) | 0x100) & 0x1F0) >> 2) * 3 - 1)},
        {"int k=1;int m=buf[2];k<<=m+4;k/=3;k%=5;out.r=k;", (double)(((1 << 5) / 3) % 5)},
        {"out.r=(buf[0]&&buf[2])+(buf[3]||0)*2+(0&&buf[0])*4;", 3.0},
        {"int i=0;int s=0;while(i<10&&s<20){s+=i;i++;}out.r=s*100+i;", 2107.0},
        {"int k=1;out.r=buf[k+1]*1000+buf[k-1]*10+buf[k];", (double)(buf[2] * 1000 + buf[0] * 10 + buf[1])},
        {"int k=-1;out.r=buf[k]+buf[k+5];", 0.0},                       // outside buf: 0
        {"out.r=-buf[0]*3-(-7);", (double)(-buf[0] * 3 + 7)},
        {"int z=-2147483647-1;out.r=z/-1;", (double)INT32_MIN},         // wraps (as the interpreter defines it)
        {"out.r=5%-1;", 0.0},
        {"int a=4;int b=a++;int c=++a;int d=a--;int e=--a;out.r=b*1000+c*100+d*10+e;", 4000.0 + 600 + 60 + 4},
        {"int a=2;int b=(a=7)+1;out.r=a*10+b;", 78.0},
        {"int x=0;if(buf[0]>200){if(buf[1]==128){x=1;}else{x=2;}}else{x=3;}out.r=x;", 1.0},
        {"int x=~buf[3];out.r=!x+x;", (double)(~buf[3])},
    };
    // Each case from the tree and, where it is within the bytecode's subset, as bytecode
    for (bool bytecode : {false, true})
    {
    DeviceTypeCodeProgram::setBytecodeEnabled(bytecode);
    uint32_t numBytecode = 0;
    for (const Case& c : cases)
    {
        String problem;
        DeviceTypeCodeProgram* pProg = DeviceTypeCodeProgram::compile(c.code, DeviceTypeCodeProgram::Kind::Decode, fields, problem);
        TEST_ASSERT(pProg, (std::string(c.code) + " compiles: " + problem.c_str()).c_str());
        if (!pProg)
            continue;
        numBytecode += pProg->usesBytecode() ? 1 : 0;
        double out = -12345;
        DeviceTypeCodeProgram::RunContext ctx;
        ctx.pBuf = buf;
        ctx.bufLen = sizeof(buf);
        ctx.pOut = (uint8_t*)&out;
        ctx.budget = 10000;
        DeviceTypeCodeProgram::Status status = pProg->run(ctx);
        bool ok = (status == DeviceTypeCodeProgram::Status::Done) && (memcmp(&out, &c.expected, sizeof(out)) == 0);
        if (!ok)
            printf("  %s gave %.17g, expected %.17g\n", c.code, out, c.expected);
        TEST_ASSERT(ok, c.code);
        delete pProg;
    }

    // A loop that doesn't end is stopped by the budget; return gives the function's result
    {
        String problem;
        DeviceTypeCodeProgram* pProg = DeviceTypeCodeProgram::compile("int i=0;while(1){i++;}", DeviceTypeCodeProgram::Kind::Decode,
                    fields, problem);
        DeviceTypeCodeProgram::RunContext ctx;
        ctx.budget = 5000;
        TEST_ASSERT(pProg && (pProg->run(ctx) == DeviceTypeCodeProgram::Status::OutOfBudget), "endless loop stopped");
        delete pProg;
        pProg = DeviceTypeCodeProgram::compile("return 3;", DeviceTypeCodeProgram::Kind::Decode, fields, problem);
        DeviceTypeCodeProgram::RunContext ctx2;
        ctx2.budget = 100;
        TEST_ASSERT(pProg && (pProg->run(ctx2) == DeviceTypeCodeProgram::Status::Return) && (ctx2.returnValue == 3), "return");
        TEST_ASSERT(pProg && (pProg->usesBytecode() == bytecode), "return as bytecode");
        delete pProg;
    }
    printf("  %s: %u of %u cases as bytecode\n", bytecode ? "bytecode on" : "bytecode off", numBytecode, (unsigned)(sizeof(cases) / sizeof(cases[0])));
    if (bytecode)
        TEST_ASSERT(numBytecode >= 25, "integer cases run as bytecode");
    else
        TEST_ASSERT(numBytecode == 0, "bytecode turned off");
    }
    DeviceTypeCodeProgram::setBytecodeEnabled(true);

    // Code outside the language is refused
    const char* bad[] = {
        "out.r=foo(1);", "out.r=x;", "out.nope=1;", "float f=1.0;out.r=f%2;", "out.r=1?2:3;", "init.v=1;",
        "out.r=1", "int a=1;int a=2;", "out.r=1.5f;", "while(1){",
    };
    for (const char* code : bad)
    {
        String problem;
        DeviceTypeCodeProgram* pProg = DeviceTypeCodeProgram::compile(code, DeviceTypeCodeProgram::Kind::Decode, fields, problem);
        TEST_ASSERT(!pProg && (problem.length() > 0), (std::string("refused: ") + code).c_str());
        delete pProg;
    }
}
#endif

#ifdef RAFT_RUNTIME_DECODE_CODE
// Every compiled record with initCode: the interpreter running its code gives what its compiled init function gives
static void testInitCodeMatchesCompiled(const char* pCataloguePath, bool bytecode)
{
    printf("Running testInitCodeMatchesCompiled (bytecode %s)...\n", bytecode ? "on" : "off");
    DeviceTypeCodeProgram::setBytecodeEnabled(bytecode);
    FILE* pFile = fopen(pCataloguePath, "rb");
    TEST_ASSERT(pFile, "catalogue read");
    if (!pFile)
        return;
    std::string text;
    char chunk[4096];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), pFile)) > 0)
        text.append(chunk, n);
    fclose(pFile);
    RaftJson catalogue(text.c_str());
    std::vector<String> keys;
    catalogue.getKeys("devTypes", keys);
    DeviceTypeRecords table;
    std::mt19937 rng(777);
    uint32_t numCompared = 0;
    for (const String& key : keys)
    {
        String base = "devTypes/" + key;
        String code = catalogue.getString((base + "/initCode/c").c_str(), "");
        if (code.length() == 0)
            continue;
        DeviceTypeIndexType idx = DEVICE_TYPE_INDEX_INVALID;
        const DeviceTypeRecord* pRec = table.getRecord(key.c_str(), idx);
        if (!pRec || !pRec->initValuesFn)
            continue;
        String problem;
        DeviceTypeRuntimeInit* pInit = DeviceTypeRuntimeInit::create(code.c_str(), pRec->initValues, problem);
        std::string name = key.c_str();
        TEST_ASSERT(pInit, (name + ": init code compiled " + problem.c_str()).c_str());
        if (!pInit)
            continue;
        TEST_ASSERT(strcmp(pInit->getNamesStr(), pRec->initValueNames ? pRec->initValueNames : "") == 0,
                    (name + ": same value names").c_str());
        TEST_ASSERT(pInit->usesBytecode() == bytecode, (name + ": bytecode as set").c_str());
        bool same = true;
        for (int iter = 0; (iter < 5000) && same; iter++)
        {
            uint32_t len = rng() % 24;
            std::vector<uint8_t> read(len + 8);
            for (auto& b : read)
                b = randomByte(rng);
            int32_t valuesCompiled[8], valuesRuntime[8];
            for (int i = 0; i < 8; i++)
                valuesCompiled[i] = valuesRuntime[i] = 0x5a5a5a5a;
            bool okCompiled = pRec->initValuesFn(read.data(), len, valuesCompiled, 8);
            bool okRuntime = pInit->run(read.data(), len, valuesRuntime, 8);
            same = (okCompiled == okRuntime) && (memcmp(valuesCompiled, valuesRuntime, sizeof(valuesCompiled)) == 0);
            if (!same)
                printf("  %s: compiled %d [%d %d] runtime %d [%d %d] (len %u)\n", name.c_str(), okCompiled, valuesCompiled[0],
                            valuesCompiled[1], okRuntime, valuesRuntime[0], valuesRuntime[1], len);
        }
        TEST_ASSERT(same, (name + ": init code gives the compiled values").c_str());
        numCompared++;
        delete pInit;
    }
    printf("  %u records' init code compared\n", numCompared);
    TEST_ASSERT(numCompared >= 4, "records with init code compared");
    TEST_ASSERT(DeviceTypeRuntimeInit::getNumSlotsInUse() == 0, "init slots freed");
    DeviceTypeCodeProgram::setBytecodeEnabled(true);
}
#endif

#if defined(RAFT_RUNTIME_DECODE_VERIFY) && defined(RAFT_RUNTIME_DECODE_CODE)
// A compiled decoder that gets one byte wrong (the verifier must report it)
static DeviceTypeRecordDecodeFn g_realDecodeFn = nullptr;
static uint16_t g_brokenOffset = 0;
static uint32_t brokenDecode(const uint8_t* pBufIn, uint32_t bufLen, void* pStructOut, uint32_t structOutSize,
            uint16_t maxRecCount, RaftBusDeviceDecodeState& decodeState)
{
    uint32_t n = g_realDecodeFn(pBufIn, bufLen, pStructOut, structOutSize, maxRecCount, decodeState);
    if (n > 0)
        ((uint8_t*)pStructOut)[g_brokenOffset] ^= 0x01;
    return n;
}

// The decode verifier (RAFT_RUNTIME_DECODE_VERIFY): every compiled record's poll results decode the same both ways,
// a decoder that differs is caught with the field it differs in, and init values are compared
// The index of a type in the verifier's results, or -1
static int verifiedTypeIdx(const RaftJson& results, const char* pTypeKey)
{
    int numTypes = 0;
    results.getType("types", numTypes);
    for (int i = 0; i < numTypes; i++)
        if (results.getString(("types[" + String(i) + "]/type").c_str(), "") == pTypeKey)
            return i;
    return -1;
}

// Make the verifier's pending decoders (one per loop call)
static void runVerifierLoop(DeviceTypeDecodeVerifier& verifier, const DeviceTypeRecords& table)
{
    for (int i = 0; i < 100; i++)
        verifier.loop(table);
}

static void testDecodeVerifier()
{
    printf("Running testDecodeVerifier...\n");
    DeviceTypeRecords table;
    DeviceTypeDecodeVerifier verifier;
    std::mt19937 rng(4242);

    // A poll result before the runtime decoder is made isn't compared; loop() makes it
    for (uint32_t idx = 0; idx < DeviceTypeRecords::getNumBaseRecords(); idx++)
    {
        const DeviceTypeRecord* pRec = table.getRecord(idx);
        if (pRec && pRec->pollResultDecodeFn && (pRec->pollStructSize > 0))
            verifier.checkDecode(RaftDeviceID(1, 0x100 + idx), idx, pRec, true, std::vector<uint8_t>(64, 0));
    }
    {
        RaftJson before("{" + verifier.getJson() + "}");
        TEST_ASSERT((before.getLong("polls", -1) == 0) && (before.getString("types[0]/decoder", "") == "pending") &&
                    (before.getLong("types[0]/notReady", 0) == 1), "not compared before loop() makes the decoder");
    }
    runVerifierLoop(verifier, table);
    verifier.reset();

    uint32_t numTypes = 0;
    for (uint32_t idx = 0; idx < DeviceTypeRecords::getNumBaseRecords(); idx++)
    {
        const DeviceTypeRecord* pRec = table.getRecord(idx);
        if (!pRec || !pRec->pollResultDecodeFn || (pRec->pollStructSize == 0))
            continue;
        RaftJson resp(pRec->devInfoJson, false);
        uint32_t recBytes = resp.getLong("resp/b", 0) + DevicePollingInfo::POLL_RESULT_TIMESTAMP_SIZE;
        // Two devices of the type, so each keeps its own decode state
        for (int iter = 0; iter < 200; iter++)
        {
            std::vector<uint8_t> buf(recBytes * (1 + rng() % 3));
            for (auto& b : buf)
                b = randomByte(rng);
            verifier.checkDecode(RaftDeviceID(1, 0x100 * (1 + iter % 2) + idx), idx, pRec, true, buf);
        }
        numTypes++;
    }
    RaftJson results("{" + verifier.getJson() + "}");
    TEST_ASSERT(numTypes >= 55, "compiled records verified");
    TEST_ASSERT(results.getLong("mismatches", -1) == 0, "no mismatches between compiled and runtime decoders");
    TEST_ASSERT(results.getLong("polls", 0) >= (int)(numTypes * 200), "every poll compared");
    int numReported = 0;
    results.getType("types", numReported);
    bool allCompared = (numReported == (int)numTypes);
    for (int i = 0; i < numReported; i++)
    {
        String decoder = results.getString(("types[" + String(i) + "]/decoder").c_str(), "");
        if (results.getLong(("types[" + String(i) + "]/polls").c_str(), 0) == 0)
        {
            numReported--;      // a record with init code but no poll data
            continue;
        }
        if (decoder != "compared")
        {
            allCompared = false;
            printf("  %s: %s %s\n", results.getString(("types[" + String(i) + "]/type").c_str(), "").c_str(), decoder.c_str(),
                        results.getString(("types[" + String(i) + "]/problem").c_str(), "").c_str());
        }
    }
    TEST_ASSERT(allCompared, "every type has a runtime decoder beside its compiled one");
    TEST_ASSERT(DeviceTypeRuntimeDecoder::getNumSlotsInUse() == 0, "verifier decoders take no slots");

    // A decoder that differs: the type, the field and both decoders' bytes are reported
    DeviceTypeIndexType tmpIdx = DEVICE_TYPE_INDEX_INVALID;
    const DeviceTypeRecord* pTmp = table.getRecord("TMP117", tmpIdx);
    TEST_ASSERT(pTmp && pTmp->pollResultDecodeFn && (pTmp->pollFieldCount > 0), "TMP117 record");
    if (pTmp)
    {
        DeviceTypeRecord broken = *pTmp;
        g_realDecodeFn = pTmp->pollResultDecodeFn;
        g_brokenOffset = pTmp->pollFieldDescs[0].offset;
        broken.pollResultDecodeFn = brokenDecode;
        DeviceTypeDecodeVerifier verifier2;
        std::vector<uint8_t> poll = {0x12, 0x34, 0x0c, 0x80};
        verifier2.checkDecode(RaftDeviceID(1, 0x48), tmpIdx, &broken, true, poll);
        runVerifierLoop(verifier2, table);
        for (int i = 0; i < 3; i++)
            verifier2.checkDecode(RaftDeviceID(1, 0x48), tmpIdx, &broken, true, poll);
        // Not the compiled decoder: counted, not compared
        verifier2.checkDecode(RaftDeviceID(1, 0x49), 200, pTmp, false, poll);
        RaftJson r2("{" + verifier2.getJson() + "}");
        String tmp = "types[" + String(verifiedTypeIdx(r2, "TMP117")) + "]/";
        TEST_ASSERT(r2.getLong("mismatches", 0) == 3, "broken decoder: every poll differs");
        TEST_ASSERT(r2.getString((tmp + "first/field").c_str(), "") == String(pTmp->pollFieldDescs[0].name), "the field that differs");
        TEST_ASSERT(r2.getString((tmp + "first/compiled").c_str(), "") != r2.getString((tmp + "first/runtime").c_str(), ""), "both decoders' bytes");
        TEST_ASSERT(r2.getString((tmp + "first/poll").c_str(), "") == "12340c80", "the poll data");
        int notCompiledIdx = -1;
        for (int i = 0; i < 100; i++)
            if (r2.getLong(("types[" + String(i) + "]/idx").c_str(), -1) == 200)
                notCompiledIdx = i;
        TEST_ASSERT(r2.getString(("types[" + String(notCompiledIdx) + "]/decoder").c_str(), "") == "notCompiled", "a runtime decoder isn't compared");
        verifier2.reset();
        RaftJson r3("{" + verifier2.getJson() + "}");
        TEST_ASSERT((r3.getLong("mismatches", -1) == 0) && (r3.getLong((tmp + "polls").c_str(), -1) == 0), "reset clears the results");
    }

    // Init values: the compiled function against the record's init code
    uint32_t initChecked = 0;
    for (uint32_t idx = 0; idx < DeviceTypeRecords::getNumBaseRecords(); idx++)
    {
        const DeviceTypeRecord* pRec = table.getRecord(idx);
        const char* pCode = table.getBaseInitCode(idx);
        if (!pRec || !pRec->initValuesFn || !pCode)
            continue;
        String names = pRec->initValueNames;
        uint32_t numValues = 1;
        for (char c : std::string(names.c_str()))
            numValues += (c == ',') ? 1 : 0;
        for (int iter = 0; iter < 500; iter++)
        {
            std::vector<uint8_t> read(rng() % 24);
            for (auto& b : read)
                b = randomByte(rng);
            std::vector<int32_t> values(numValues, 0);
            bool ok = pRec->initValuesFn(read.data(), read.size(), values.data(), values.size());
            verifier.checkInit(idx, pRec, pCode, read, ok, values);
        }
        initChecked++;
        // Values the code wouldn't compute are caught
        std::vector<uint8_t> read(24, 0x55);
        std::vector<int32_t> values(numValues, 0);
        bool ok = pRec->initValuesFn(read.data(), read.size(), values.data(), values.size());
        values[0] ^= 1;
        DeviceTypeDecodeVerifier verifier3;
        verifier3.loop(table);
        verifier3.checkInit(idx, pRec, pCode, read, ok, values);
        RaftJson r4("{" + verifier3.getJson() + "}");
        TEST_ASSERT(r4.getLong("initMismatches", 0) == 1, (std::string(pRec->deviceType) + ": wrong init values caught").c_str());
    }
    RaftJson r5("{" + verifier.getJson() + "}");
    TEST_ASSERT(initChecked >= 4, "records with init code verified");
    TEST_ASSERT((r5.getLong("initRuns", 0) >= (int)(initChecked * 500)) && (r5.getLong("initMismatches", -1) == 0),
                "init values the same both ways");
    TEST_ASSERT(DeviceTypeRuntimeInit::getNumSlotsInUse() == 0, "verifier init functions take no slots");
}
#endif

int main(int argc, char** argv)
{
    testMatchesCompiledDecoders(false);
    testMatchesCompiledDecoders(true);
    testTypeFileRecordGetsRuntimeDecoder();
    testAtList();
    testRefused();
#ifdef RAFT_RUNTIME_DECODE_CODE
    testInterpreterSemantics();
    if (argc > 1)
    {
        testInitCodeMatchesCompiled(argv[1], false);
        testInitCodeMatchesCompiled(argv[1], true);
    }
#endif
#if defined(RAFT_RUNTIME_DECODE_VERIFY) && defined(RAFT_RUNTIME_DECODE_CODE)
    testDecodeVerifier();
#endif
    return testSummary("test_runtime_decoder");
}
