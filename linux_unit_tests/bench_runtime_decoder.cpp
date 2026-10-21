/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// bench_runtime_decoder (make bench - not part of make check)
//
// Time per decode of the runtime decoder (DeviceTypeRuntimeDecoder, and the code interpreter for custom decode code)
// against the compiled decoder, for every compiled record, on the same random poll buffers. Built with -O2.
//
// Usage: bench_runtime_decoder [record key] [iterations]
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <string.h>
#include <chrono>
#include <random>
#include <string>
#include <vector>
#include "DeviceTypeRecords.h"
#include "DeviceTypeRuntimeDecoder.h"
#include "RaftJson.h"

static uint8_t randomByte(std::mt19937& rng)
{
    static const uint8_t EDGES[] = {0x00, 0xff, 0x7f, 0x80, 0x01, 0xfe};
    return (rng() % 10 < 4) ? EDGES[rng() % sizeof(EDGES)] : (uint8_t)rng();
}

int main(int argc, char** argv)
{
    const char* pOnly = (argc > 1) ? argv[1] : nullptr;
    uint32_t iterations = (argc > 2) ? atoi(argv[2]) : 20000;
    static const uint32_t NUM_BUFS = 64;
    static const uint16_t MAX_RECS = 32;

    DeviceTypeRecords table;
    std::mt19937 rng(99);
    double totalCompiledCustom = 0, totalRuntimeCustom = 0, totalCompiledStd = 0, totalRuntimeStd = 0;
    printf("%-20s %6s %10s %10s %7s %8s %8s %8s\n", "record", "code", "compiled", "runtime", "ratio", "recs", "nodes", "ns/node");
    for (uint32_t idx = 0; idx < DeviceTypeRecords::getNumBaseRecords(); idx++)
    {
        const DeviceTypeRecord* pRec = table.getRecord(idx);
        if (!pRec || !pRec->pollResultDecodeFn || !pRec->devInfoJson || (pRec->pollStructSize == 0))
            continue;
        if (pOnly && (strcmp(pOnly, pRec->deviceType) != 0))
            continue;
        String problem;
        DeviceTypeRuntimeDecoder* pDecoder = DeviceTypeRuntimeDecoder::create(pRec->devInfoJson, problem, false);
        if (!pDecoder)
            continue;
        RaftJson devInfo(pRec->devInfoJson, false);
        bool isCustom = devInfo.contains("resp/c");
        uint32_t recBytes = devInfo.getLong("resp/b", 0) + DevicePollingInfo::POLL_RESULT_TIMESTAMP_SIZE;

        // The same buffers for both: whole records, as a poll gives
        std::vector<std::vector<uint8_t>> bufs(NUM_BUFS);
        for (auto& buf : bufs)
        {
            buf.resize(recBytes);
            for (auto& b : buf)
                b = randomByte(rng);
        }
        std::vector<uint8_t> out(pRec->pollStructSize * MAX_RECS);
        RaftBusDeviceDecodeState state;

        uint64_t recs = 0;
        auto t0 = std::chrono::steady_clock::now();
        for (uint32_t i = 0; i < iterations; i++)
            recs += pRec->pollResultDecodeFn(bufs[i % NUM_BUFS].data(), recBytes, out.data(), out.size(), MAX_RECS, state);
        auto t1 = std::chrono::steady_clock::now();
        uint64_t nodesBefore = pDecoder->getNodesRun();
        for (uint32_t i = 0; i < iterations; i++)
            pDecoder->decode(bufs[i % NUM_BUFS].data(), recBytes, out.data(), out.size(), MAX_RECS, state);
        auto t2 = std::chrono::steady_clock::now();

        double compiledNs = std::chrono::duration<double, std::nano>(t1 - t0).count() / iterations;
        double runtimeNs = std::chrono::duration<double, std::nano>(t2 - t1).count() / iterations;
        double nodes = (double)(pDecoder->getNodesRun() - nodesBefore) / iterations;
        printf("%-20s %6s %8.0fns %8.0fns %6.1fx %8.2f", pRec->deviceType, isCustom ? "custom" : "", compiledNs,
                    runtimeNs, runtimeNs / compiledNs, (double)recs / iterations);
        if (nodes > 0)
            printf(" %8.0f %8.1f", nodes, runtimeNs / nodes);
        printf("\n");
        (isCustom ? totalCompiledCustom : totalCompiledStd) += compiledNs;
        (isCustom ? totalRuntimeCustom : totalRuntimeStd) += runtimeNs;
        delete pDecoder;
    }
    if (!pOnly)
        printf("\nTotal: custom code %.1fx slower, standard attributes %.1fx slower\n",
                    totalRuntimeCustom / totalCompiledCustom, totalRuntimeStd / totalCompiledStd);
    return 0;
}
