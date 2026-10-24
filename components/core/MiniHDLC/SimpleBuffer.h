/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// SimpleBuffer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <vector>
#include <algorithm>
#include "SpiramAwareAllocator.h"
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

// A byte buffer of up to a maximum length. It grows as needed, checking the memory is there first: an allocation
// that fails can't be detected (exceptions are disabled) and would crash, so resize() and setAt() return false
// instead. clear() releases the memory of a buffer grown by an unusually long frame.
class SimpleBuffer
{
public:
#ifdef IMPLEMENT_NO_PSRAM_FOR_HDLC_BUFFERS
    static const unsigned DEFAULT_MAX_LEN = 5000;
#else
    static const unsigned DEFAULT_MAX_LEN = 200000;
#endif
    // Memory left free when the buffer grows
    static constexpr unsigned HEAP_MARGIN_BYTES = 16384;
    // Smallest step when the buffer grows
    static constexpr unsigned MIN_GROW_BYTES = 256;
    // Memory kept by clear() - a buffer grown larger (by an unusually long frame) is released
    static constexpr unsigned RETAIN_CAPACITY_MAX = 32768;

    SimpleBuffer(unsigned maxFrameLen = DEFAULT_MAX_LEN)
    {
        _bufMaxLen = maxFrameLen;
    }

    virtual ~SimpleBuffer()
    {
    }

    void clear()
    {
        _buffer.clear();
        if (_buffer.capacity() > RETAIN_CAPACITY_MAX)
            _buffer.shrink_to_fit();
    }

    void setMaxLen(unsigned maxLen)
    {
        _bufMaxLen = maxLen;
    }

    bool resize(unsigned size)
    {
        if (size > _bufMaxLen)
            return false;
        if (!reserveFor(size))
            return false;
        _buffer.resize(size);
        return true;
    }

    bool setAt(unsigned idx, uint8_t val)
    {
        if (idx >= _bufMaxLen)
            return false;
        if (idx >= _buffer.size())
        {
            if (!reserveFor(idx+1))
                return false;
            _buffer.resize(idx+1);
        }
        _buffer[idx] = val;
        return true;
    }

    uint8_t getAt(unsigned idx) const
    {
        if (idx >= _buffer.size())
            return 0;
        return _buffer[idx];
    }

    uint8_t* data()
    {
        return _buffer.data();
    }

    unsigned size()
    {
        return _buffer.size();
    }

    unsigned capacity() const
    {
        return _buffer.capacity();
    }

protected:
    // Largest block of memory the buffer could be allocated from
    virtual uint32_t largestFreeBlock() const
    {
#if defined(IMPLEMENT_NO_PSRAM_FOR_HDLC_BUFFERS) && defined(ESP_PLATFORM)
        return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
#else
        return SpiramAwareAllocator<uint8_t>::max_allocatable();
#endif
    }

private:
#ifdef IMPLEMENT_NO_PSRAM_FOR_HDLC_BUFFERS
    std::vector<uint8_t> _buffer;
#else
    SpiramAwareUint8Vector _buffer;
#endif
    unsigned _bufMaxLen;

    // Make room for size bytes - growing geometrically (up to the maximum length), or to just what is needed when
    // memory is short. Returns false if the memory isn't there
    bool reserveFor(unsigned size)
    {
        if (size <= _buffer.capacity())
            return true;
        unsigned growTo = std::max((unsigned)_buffer.capacity() * 2, MIN_GROW_BYTES);
        unsigned newCapacity = std::max(size, std::min(growTo, _bufMaxLen));
        uint32_t largestFree = largestFreeBlock();
        if (newCapacity + HEAP_MARGIN_BYTES > largestFree)
            newCapacity = size;
        if (newCapacity + HEAP_MARGIN_BYTES > largestFree)
            return false;
        _buffer.reserve(newCapacity);
        return true;
    }
};
