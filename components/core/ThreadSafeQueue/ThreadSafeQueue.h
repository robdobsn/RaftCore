/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// ThreadSafeQueue
// Template-based queue
//
// Rob Dobson 2012-2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <queue>
#include "RaftThreading.h"

template<typename ElemT>
class ThreadSafeQueue
{
public:
    ThreadSafeQueue(uint32_t maxLen = DEFAULT_MAX_QUEUE_LEN)
        : _queueMutex()
    {
        // Initialize the mutex
        RaftMutex_init(_queueMutex);
        _maxLen = maxLen;
        _maxMsToWaitDefault = DEFAULT_MAX_MS_TO_WAIT;
    }

    virtual ~ThreadSafeQueue()
    {
        // Destroy the mutex
        RaftMutex_destroy(_queueMutex);
    }

    void setMaxLen(uint32_t maxLen)
    {
        _maxLen = maxLen;
    }

    bool put(const ElemT& elem, uint32_t maxMsToWait = 0)
    {
        // Lock the mutex
        if (RaftMutex_lock(_queueMutex, maxMsToWait))
        {
            // Check if queue is full
            if (_queue.size() >= _maxLen)
            {
                // Unlock the mutex
                RaftMutex_unlock(_queueMutex);
                return false;
            }

            // Queue up the item
            _queue.push(elem);

            // Unlock the mutex
            RaftMutex_unlock(_queueMutex);
            return true;
        }
        return false;
    }

    bool get(ElemT& elem, uint32_t maxMsToWait = 0)
    {
        // Lock the mutex
        if (RaftMutex_lock(_queueMutex, maxMsToWait))
        {
            if (_queue.empty())
            {
                // Unlock the mutex
                RaftMutex_unlock(_queueMutex);
                return false;
            }

            // Read the item and remove
            elem = _queue.front();
            _queue.pop();

            // Unlock the mutex
            RaftMutex_unlock(_queueMutex);
            return true;
        }
        return false;
    }

    bool peek(ElemT& elem, uint32_t maxMsToWait = 0)
    {
        // Lock the mutex
        if (RaftMutex_lock(_queueMutex, maxMsToWait))
        {
            if (_queue.empty())
            {
                // Unlock the mutex
                RaftMutex_unlock(_queueMutex);
                return false;
            }

            // Read the item (but do not remove)
            elem = _queue.front();

            // Unlock the mutex
            RaftMutex_unlock(_queueMutex);
            return true;
        }
        return false;
    }

    void clear(uint32_t maxMsToWait = 0)
    {
        if (RaftMutex_lock(_queueMutex, maxMsToWait))
        {
            // Clear the queue
            while (!_queue.empty())
                _queue.pop();

            // Unlock the mutex
            RaftMutex_unlock(_queueMutex);
        }
    }

    uint32_t count(uint32_t maxMsToWait = 0)
    {
        if (RaftMutex_lock(_queueMutex, maxMsToWait))
        {
            int qSize = _queue.size();
            // Unlock the mutex
            RaftMutex_unlock(_queueMutex);
            return qSize;
        }
        return 0;
    }

    uint32_t maxLen()
    {
        return _maxLen;
    }

    bool canAcceptData()
    {
        return _queue.size() < _maxLen;
    }

    void setMaxMsToWait(uint32_t maxMsToWait)
    {
        _maxMsToWaitDefault = maxMsToWait;
    }

private:
    std::queue<ElemT> _queue;
    static const uint16_t DEFAULT_MAX_QUEUE_LEN = 50;
    uint16_t _maxLen = DEFAULT_MAX_QUEUE_LEN;
    static const uint16_t DEFAULT_MAX_MS_TO_WAIT = 1;
    uint16_t _maxMsToWaitDefault = DEFAULT_MAX_MS_TO_WAIT;

    // Mutex for queue
    RaftMutex _queueMutex;
};
