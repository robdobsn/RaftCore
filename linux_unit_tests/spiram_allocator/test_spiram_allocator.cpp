/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// SpiramAwareAllocator host tests
//
// Compiles the real SpiramAwareAllocator.h with only the ESP SDK boundary shimmed (spiram_allocator/sdk), once per
// configuration (see the Makefile): CONFIG_SPIRAM on IDF 5 and 6, the legacy CONFIG_ESP32_SPIRAM_SUPPORT on IDF 4
// and 6, PSRAM missing, PSRAM disabled, PSRAM allocation failing (falls back to malloc), and a non-ESP build. Each
// checks the allocation route and the largest-block report. These are SDK-boundary checks, not evidence of where
// memory is placed on a board.
//
// Ported from the Axiom feature/device-library branch (tests/spiram-allocator, bbaa98b).
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

// Only the ESP SDK boundary is substituted. The allocator and its containers are real.
struct SdkState
{
    unsigned allocationCalls = 0;
    unsigned failedAllocations = 0;
    size_t allocationBytes = 0;
    unsigned allocationCaps = 0;
    unsigned largestBlockCalls = 0;
    unsigned largestBlockCaps = 0;
};

static SdkState sdk;

#include "SpiramAwareAllocator.h"

static int failures = 0;

static void expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::printf("FAIL: %s\n", message);
        ++failures;
    }
}

static void checkAllocationRoute(unsigned callsBefore)
{
#if TEST_EXPECT_SPIRAM
    expect(sdk.allocationCalls > callsBefore, "allocation attempts explicit PSRAM heap");
    expect(sdk.allocationCaps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
        "allocation requests SPIRAM | 8BIT capabilities");
#else
    expect(sdk.allocationCalls == callsBefore, "allocation uses ordinary malloc");
#endif
}

static void testDirectAllocation()
{
    SpiramAwareAllocator<uint32_t> allocator;
    const auto callsBefore = sdk.allocationCalls;
    auto* values = allocator.allocate(64);
    expect(values != nullptr, "direct allocation succeeds, including the malloc fallback");
    checkAllocationRoute(callsBefore);
    if (TEST_EXPECT_SPIRAM)
    {
        expect(sdk.allocationCalls == callsBefore + 1, "one capability allocation per direct request");
        expect(sdk.allocationBytes == 64 * sizeof(uint32_t), "allocation size includes element size");
    }
    if (values != nullptr)
    {
        for (uint32_t i = 0; i < 64; ++i)
        {
            values[i] = 0x12340000u + i;
        }
        for (uint32_t i = 0; i < 64; ++i)
        {
            expect(values[i] == 0x12340000u + i, "direct allocation retains written values");
        }
        allocator.deallocate(values, 64);
    }
    allocator.deallocate(nullptr, 0);
}

static void testVector()
{
    const auto callsBefore = sdk.allocationCalls;
    SpiramAwareUint8Vector values(256);
    checkAllocationRoute(callsBefore);
    for (size_t i = 0; i < values.size(); ++i)
    {
        values[i] = static_cast<uint8_t>(i);
    }
    for (size_t i = 0; i < values.size(); ++i)
    {
        expect(values[i] == static_cast<uint8_t>(i), "allocator-backed vector retains written values");
    }
}

static void testString()
{
    const auto callsBefore = sdk.allocationCalls;
    // Exceed small-string storage so this must use the production allocator.
    SpiramAwareString text(256, 'x');
    checkAllocationRoute(callsBefore);
    text[0] = 'A';
    text[255] = 'Z';
    expect(text.size() == 256, "allocator-backed string retains its length");
    expect(text.front() == 'A' && text.back() == 'Z', "allocator-backed string is writable");
    for (size_t i = 1; i < 255; ++i)
    {
        expect(text[i] == 'x', "allocator-backed string retains its contents");
    }
    expect(text.c_str()[256] == '\0', "allocator-backed string remains null-terminated");
}

static void testLargestBlock()
{
    const auto largest = SpiramAwareAllocator<uint32_t>::max_allocatable();
#ifdef ESP_PLATFORM
    const uint32_t expected = TEST_EXPECT_SPIRAM ? 4096 : 1024;
    const unsigned expectedCaps = TEST_EXPECT_SPIRAM
        ? MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        : MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    expect(largest == expected, "largest-block result uses the selected heap, in bytes");
    expect(sdk.largestBlockCalls == 1, "one largest-block SDK query");
    expect(sdk.largestBlockCaps == expectedCaps, "largest-block query requests exact capabilities");
#else
    expect(largest == UINT32_MAX, "non-ESP largest-block result is UINT32_MAX");
    expect(sdk.largestBlockCalls == 0, "non-ESP makes no largest-block SDK query");
#endif
}

int main()
{
    testDirectAllocation();
    testVector();
    testString();
    testLargestBlock();
    expect(sdk.failedAllocations == (TEST_ALLOCATION_FAILS ? sdk.allocationCalls : 0),
        "failed capability allocations fall back to usable ordinary storage");
    std::printf("%s: capability allocations=%u, failed=%u, largest-block queries=%u\n",
        failures == 0 ? "PASS" : "FAIL", sdk.allocationCalls, sdk.failedAllocations, sdk.largestBlockCalls);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
