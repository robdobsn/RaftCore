/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// SpiramAwareAllocator
//
// Rob Dobson 2021-2022
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstddef>
#include <string>
#include <vector>
#include <cstdint>

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#endif

#ifdef CONFIG_ESP32_SPIRAM_SUPPORT

#include "sdkconfig.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include <vector>
#include <string>

#ifdef __cplusplus
extern "C" {
#endif
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
#include "esp32/spiram.h"
#else
#include "esp_psram.h"
#endif
#ifdef __cplusplus
}
#endif
#endif

template <class T>
class SpiramAwareAllocator
{
public:
    typedef size_t    size_type;
    typedef ptrdiff_t difference_type;
    typedef T*        pointer;
    typedef const T*  const_pointer;
    typedef T&        reference;
    typedef const T&  const_reference;
    typedef T         value_type;

    SpiramAwareAllocator() {};
    SpiramAwareAllocator(const SpiramAwareAllocator&) {};
    T* allocate(size_type n, const void* = 0)
    {

#ifdef CONFIG_ESP32_SPIRAM_SUPPORT
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
        if (esp_spiram_get_chip_size() != ESP_SPIRAM_SIZE_INVALID)
            return (T*) (heap_caps_malloc(n * sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
        if (esp_psram_get_size() != 0)
            return (T*) (heap_caps_malloc(n * sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#endif
#endif

        return (T*) (malloc(n * sizeof(T)));
    }
    void deallocate(void* p, size_type)
    {
        if (p)
            free(p);
    }
    pointer address(reference x) const { return &x; }
    const_pointer address(const_reference x) const { return &x; }
    SpiramAwareAllocator<T>& operator=(const SpiramAwareAllocator&) { return *this; }
    void construct(pointer p, const T& val) { new ((T*) p) T(val); }
    void destroy(pointer p) { p->~T(); }
    size_type max_size() const { return size_t(-1); }

    template <class U>
    struct rebind { typedef SpiramAwareAllocator<U> other; };

    template <class U>
    SpiramAwareAllocator(const SpiramAwareAllocator<U>&) {}

    template <class U>
    SpiramAwareAllocator& operator=(const SpiramAwareAllocator<U>&) { 
        return *this; }

    static uint32_t max_allocatable()
    {
#ifdef CONFIG_ESP32_SPIRAM_SUPPORT
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
        if (esp_spiram_get_chip_size() != ESP_SPIRAM_SIZE_INVALID)
            return heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
        if (esp_psram_get_size() != 0)
            return heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
#endif
#ifdef ESP_PLATFORM
        return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#else
        return UINT32_MAX;
#endif
    }
};

template <class U, class V>
bool operator==(const SpiramAwareAllocator<U>&, const SpiramAwareAllocator<V>&) { return true; }

template <class U, class V>
bool operator!=(const SpiramAwareAllocator<U>&, const SpiramAwareAllocator<V>&) { return false; }

// Type for SpiramAware vectors
using SpiramAwareUint8Vector = std::vector<uint8_t, SpiramAwareAllocator<uint8_t>>;

// Type for SpiramAware strings
using SpiramAwareString = std::basic_string<char, std::char_traits<char>, SpiramAwareAllocator<char>>;
