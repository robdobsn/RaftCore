/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RingBufferPosn
// Lock-free SPSC (Single-Producer Single-Consumer) ring buffer pointer class
// Uses atomic operations with acquire/release memory ordering for thread-safety
// Producer thread updates _putPos, Consumer thread updates _getPos
//
// Rob Dobson 2012-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftThreading.h"

class RingBufferPosn
{
public:
    RingBufferPosn(int maxLen)
    {
        init(maxLen);
    }

    virtual ~RingBufferPosn()
    {
    }

    void init(int maxLen)
    {
        _bufLen = maxLen;
        RaftAtomicUint32_init(_putPos, 0);
        RaftAtomicUint32_init(_getPos, 0);
    }

    void clear()
    {
        // Note: clear() is NOT thread-safe in lock-free SPSC
        // Should only be called when no concurrent access is occurring
        RaftAtomicUint32_store(_getPos, 0, RAFT_ATOMIC_SEQ_CST);
        RaftAtomicUint32_store(_putPos, 0, RAFT_ATOMIC_SEQ_CST);
    }

    inline unsigned int posToGet()
    {
        return RaftAtomicUint32_load(_getPos, RAFT_ATOMIC_ACQUIRE);
    }

    inline unsigned int posToPut()
    {
        return RaftAtomicUint32_load(_putPos, RAFT_ATOMIC_ACQUIRE);
    }

    bool canPut()
    {
        if (_bufLen == 0)
            return false;
        // Load current positions with acquire semantics
        uint32_t currentPut = RaftAtomicUint32_load(_putPos, RAFT_ATOMIC_ACQUIRE);
        uint32_t currentGet = RaftAtomicUint32_load(_getPos, RAFT_ATOMIC_ACQUIRE);
        
        if (currentPut == currentGet)
            return true;
        if (currentPut > currentGet)
        {
            if ((currentPut != _bufLen - 1) || (currentGet != 0))
                return true;
        }
        else
        {
            if (currentGet - currentPut > 1)
                return true;
        }
        return false;
    }

    bool canGet()
    {
        // Load positions with acquire semantics
        uint32_t currentPut = RaftAtomicUint32_load(_putPos, RAFT_ATOMIC_ACQUIRE);
        uint32_t currentGet = RaftAtomicUint32_load(_getPos, RAFT_ATOMIC_ACQUIRE);
        return currentPut != currentGet;
    }

    void hasPut()
    {
        // Producer owns _putPos - read with relaxed ordering
        uint32_t currentPut = RaftAtomicUint32_load(_putPos, RAFT_ATOMIC_RELAXED);
        uint32_t nextPut = currentPut + 1;
        if (nextPut >= _bufLen)
            nextPut = 0;
        // Write with release semantics - ensures data write completes before position update
        RaftAtomicUint32_store(_putPos, nextPut, RAFT_ATOMIC_RELEASE);
    }

    void hasGot()
    {
        // Consumer owns _getPos - read with relaxed ordering
        uint32_t currentGet = RaftAtomicUint32_load(_getPos, RAFT_ATOMIC_RELAXED);
        uint32_t nextGet = currentGet + 1;
        if (nextGet >= _bufLen)
            nextGet = 0;
        // Write with release semantics
        RaftAtomicUint32_store(_getPos, nextGet, RAFT_ATOMIC_RELEASE);
    }

    unsigned int count()
    {
        // Load positions with acquire semantics for accurate count
        uint32_t currentPut = RaftAtomicUint32_load(_putPos, RAFT_ATOMIC_ACQUIRE);
        uint32_t currentGet = RaftAtomicUint32_load(_getPos, RAFT_ATOMIC_ACQUIRE);
        if (currentGet <= currentPut)
            return currentPut - currentGet;
        return _bufLen - currentGet + currentPut;
    }

    // Get Nth element prior to the put position
    // 0 is the last element put in the queue
    // 1 is the one put in before that
    // Returns -1 if invalid
    // Note: This provides a snapshot view but positions may change during calculation
    int getNthFromPut(unsigned int N)
    {
        if (!canGet())
            return -1;
        if (N >= _bufLen)
            return -1;
        // Load positions with acquire semantics
        uint32_t currentPut = RaftAtomicUint32_load(_putPos, RAFT_ATOMIC_ACQUIRE);
        uint32_t currentGet = RaftAtomicUint32_load(_getPos, RAFT_ATOMIC_ACQUIRE);
        
        int nthPos = (int)currentPut - 1 - (int)N;
        if (nthPos < 0)
            nthPos += _bufLen;
        if (((unsigned int)(nthPos + 1) == currentGet) || ((unsigned int)(nthPos + 1) == _bufLen && currentGet == 0))
            return -1;
        return nthPos;
    }

    // Get Nth element from the get position
    // 0 is the element next got from the queue
    // 1 is the one got after that
    // returns -1 if invalid
    // Note: This provides a snapshot view but positions may change during calculation
    int getNthFromGet(unsigned int N)
    {
        if (!canGet())
            return -1;
        if (N >= _bufLen)
            return -1;
        // Load positions with acquire semantics
        uint32_t currentPut = RaftAtomicUint32_load(_putPos, RAFT_ATOMIC_ACQUIRE);
        uint32_t currentGet = RaftAtomicUint32_load(_getPos, RAFT_ATOMIC_ACQUIRE);
        
        unsigned int nthPos = currentGet + N;
        if (nthPos >= _bufLen)
            nthPos -= _bufLen;
        if (nthPos == currentPut)
            return -1;
        return nthPos;
    }
private:
    RaftAtomicUint32 _putPos;
    RaftAtomicUint32 _getPos;
    unsigned int _bufLen;

};
