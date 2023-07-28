/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// ProtocolRawMsg
//
// Rob Dobson 2016-2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>
#include <stdint.h>
#include <RaftArduino.h>
#include <RaftJson.h>
#include <SpiramAwareAllocator.h>

// TODO - determine if this has a positive impact on memory use or has some unknown negative impact
// #define IMPLEMENT_USE_PSRAM_FOR_PROTOCOL_RAW_MSG

class ProtocolRawMsg
{
public:
    ProtocolRawMsg()
    {
    }

    ProtocolRawMsg(const uint8_t* pBuf, uint32_t bufLen)
    {
        _cmdVector.assign(pBuf, pBuf+bufLen);
    }

    void clear()
    {
        _cmdVector.resize(0);
        _cmdVector.shrink_to_fit();
    }

    void setFromBuffer(const uint8_t* pBuf, uint32_t bufLen)
    {
        _cmdVector.assign(pBuf, pBuf+bufLen);
    }

    void setBufferSize(uint32_t bufSize)
    {
        _cmdVector.resize(bufSize);
    }

    void setPartBuffer(uint32_t startPos, const uint8_t* pBuf, uint32_t len)
    {
        uint32_t reqSize = startPos + len;
        if (_cmdVector.size() < reqSize)
            _cmdVector.resize(reqSize);
        memcpy(_cmdVector.data() + startPos, pBuf, len);
    }

    // Access to command vector
    const uint8_t* getBuf()
    {
        return _cmdVector.data();
    }
    uint32_t getBufLen()
    {
        return _cmdVector.size();
    }
#ifdef IMPLEMENT_USE_PSRAM_FOR_PROTOCOL_RAW_MSG
    std::vector<uint8_t, SpiramAwareAllocator<uint8_t>>& getCmdVector()
#else
    std::vector<uint8_t>& getCmdVector()
#endif
    {
        return _cmdVector;
    }

private:
#ifdef IMPLEMENT_USE_PSRAM_FOR_PROTOCOL_RAW_MSG
    std::vector<uint8_t, SpiramAwareAllocator<uint8_t>> _cmdVector;
#else
    std::vector<uint8_t> _cmdVector;
#endif
};
