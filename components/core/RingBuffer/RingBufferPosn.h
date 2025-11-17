/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RingBufferPosn
// Generic ring buffer pointer class
// Each pointer is only updated by one source
//
// Rob Dobson 2012-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftThreading.h"

class RingBufferPosn
{
public:
    volatile unsigned int _putPos;
    volatile unsigned int _getPos;
    unsigned int _bufLen;
    RaftMutex _rbMutex;
    static const int PUT_TICKS_TO_WAIT = 1;
    static const int GET_TICKS_TO_WAIT = 1;
    static const int CLEAR_TICKS_TO_WAIT = 2;

    RingBufferPosn(int maxLen)
    {
        init(maxLen);
        RaftMutex_init(_rbMutex);
    }

    virtual ~RingBufferPosn()
    {
        RaftMutex_destroy(_rbMutex);
    }

    void init(int maxLen)
    {
        _bufLen = maxLen;
        _putPos = 0;
        _getPos = 0;
    }

    void clear()
    {
        if (RaftMutex_lock(_rbMutex, CLEAR_TICKS_TO_WAIT))
        {
            _getPos = _putPos = 0;
            RaftMutex_unlock(_rbMutex);
        }
    }

    inline unsigned int posToGet()
    {
        return _getPos;
    }

    inline unsigned int posToPut()
    {
        return _putPos;
    }

    bool canPut()
    {
        if (_bufLen == 0)
            return false;
        if (_putPos == _getPos)
            return true;
        unsigned int gp = _getPos;
        if (_putPos > gp)
        {
            if ((_putPos != _bufLen - 1) || (gp != 0))
                return true;
        }
        else
        {
            if (gp - _putPos > 1)
                return true;
        }
        return false;
    }

    bool canGet()
    {
        return _putPos != _getPos;
    }

    void hasPut()
    {
        // Put
        _putPos++;
        if (_putPos >= _bufLen)
            _putPos = 0;
        
        // Return the mutex
        RaftMutex_unlock(_rbMutex);
    }

    void hasGot()
    {
        // Get
        _getPos++;
        if (_getPos >= _bufLen)
            _getPos = 0;

        // Return the mutex
        RaftMutex_unlock(_rbMutex);
    }

    unsigned int count()
    {
        unsigned int retVal = 0;
        if (RaftMutex_lock(_rbMutex, GET_TICKS_TO_WAIT))
        {
            unsigned int posToGet = _getPos;
            if (posToGet <= _putPos)
                retVal = _putPos - posToGet;
            else
                retVal = _bufLen - posToGet + _putPos;
            RaftMutex_unlock(_rbMutex);
        }
        return retVal;
    }

    // Get Nth element prior to the put position
    // 0 is the last element put in the queue
    // 1 is the one put in before that
    // Returns -1 if invalid
    int getNthFromPut(unsigned int N)
    {
        if (!canGet())
            return -1;
        if (N >= _bufLen)
            return -1;
        int nthPos = _putPos - 1 - N;
        if (nthPos < 0)
            nthPos += _bufLen;
        if (((unsigned int)(nthPos + 1) == _getPos) || ((unsigned int)(nthPos + 1) == _bufLen && _getPos == 0))
            return -1;
        return nthPos;
    }

    // Get Nth element from the get position
    // 0 is the element next got from the queue
    // 1 is the one got after that
    // returns -1 if invalid
    int getNthFromGet(unsigned int N)
    {
        if (!canGet())
            return -1;
        if (N >= _bufLen)
            return -1;
        unsigned int nthPos = _getPos + N;
        if (nthPos >= _bufLen)
            nthPos -= _bufLen;
        if (nthPos == _putPos)
            return -1;
        return nthPos;
    }
};
