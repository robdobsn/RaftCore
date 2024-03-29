/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RingBufferRTOS
// Template-based ring bufffer
//
// Rob Dobson 2012-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>
#include "RingBufferPosnRTOS.h"

template<typename ElemT, uint32_t bufferSize>
class RingBufferRTOS
{
public:
    RingBufferRTOS() :
        _bufPos(bufferSize)
    {
    }

    bool put(const ElemT& elem)
    {
        if (_bufPos.canPut())
        {
            _buffer[_bufPos.posToPut()] = elem;
            _bufPos.hasPut();
            return true;
        }
        return false;
    }

    bool get(ElemT& elem)
    {
        if (_bufPos.canGet())
        {
            unsigned int getPos = _bufPos.posToGet();
            elem = _buffer[getPos];
            _buffer[getPos].clear();
            _bufPos.hasGot();
            return true;
        }
        return false;
    }

    void clear()
    {
        _bufPos.clear();
    }

    uint32_t count()
    {
        return _bufPos.count();
    }

    uint32_t maxLen()
    {
        return bufferSize;
    }

private:
    RingBufferPosnRTOS _bufPos;
    ElemT _buffer[bufferSize];
};
