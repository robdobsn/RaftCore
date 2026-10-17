#pragma once

#define MALLOC_CAP_8BIT 1
#define MALLOC_CAP_SPIRAM 2
#define MALLOC_CAP_INTERNAL 4

inline void* heap_caps_malloc(size_t bytes, unsigned caps)
{
    ++sdk.allocationCalls;
    sdk.allocationBytes = bytes;
    sdk.allocationCaps = caps;
    if (TEST_ALLOCATION_FAILS)
    {
        ++sdk.failedAllocations;
        return nullptr;
    }
    return std::malloc(bytes);
}

inline size_t heap_caps_get_largest_free_block(unsigned caps)
{
    ++sdk.largestBlockCalls;
    sdk.largestBlockCaps = caps;
    return (caps & MALLOC_CAP_SPIRAM) ? 4096 : 1024;
}
