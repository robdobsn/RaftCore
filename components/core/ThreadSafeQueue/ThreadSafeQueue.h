/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// ThreadSafeQueue
// Template-based queue
//
// Rob Dobson 2012-2022
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <queue>
#include <atomic>
#include "RaftThreading.h"

// Notes on thread-safety:
// - put/get/peek/clear block for up to maxMsToWait (default DEFAULT_MAX_MS_TO_WAIT) to obtain the queue mutex
//   and return false if the mutex could not be obtained - so a false return does NOT necessarily mean
//   full (put) or empty (get/peek) and the result must always be checked
// - count() and canAcceptData() are lock-free and never fail (the value may be stale by the time it is used)
// - the critical sections are short (one element copy) so the default wait is only hit if something is badly wrong

template<typename ElemT>
class ThreadSafeQueue
{
public:
    // Default max time to wait for the queue mutex
    static const uint32_t DEFAULT_MAX_MS_TO_WAIT = 10;

    ThreadSafeQueue(uint32_t maxLen = DEFAULT_MAX_QUEUE_LEN)
    {
        // Mutex for ThreadSafeQueue
        RaftMutex_init(_queueMutex);
        _maxLen = maxLen;
    }

    virtual ~ThreadSafeQueue()
    {
        RaftMutex_destroy(_queueMutex);
    }

    // Not copyable (owns a mutex)
    ThreadSafeQueue(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;

    void setMaxLen(uint32_t maxLen)
    {
        _maxLen = maxLen;
    }

    [[nodiscard]] bool put(const ElemT& elem, uint32_t maxMsToWait = DEFAULT_MAX_MS_TO_WAIT)
    {
        // Get mutex
        if (RaftMutex_lock(_queueMutex, maxMsToWait))
        {
            // Check if queue is full
            if (_queue.size() >= _maxLen)
            {
                // Return mutex
                RaftMutex_unlock(_queueMutex);
                return false;
            }

            // Queue up the item
            _queue.push(elem);
            _count = _queue.size();

            // Return mutex
            RaftMutex_unlock(_queueMutex);
            return true;
        }
        return false;
    }

    [[nodiscard]] bool get(ElemT& elem, uint32_t maxMsToWait = DEFAULT_MAX_MS_TO_WAIT)
    {
        // Avoid taking the mutex if empty
        if (_count == 0)
            return false;

        // Get Mutex
        if (RaftMutex_lock(_queueMutex, maxMsToWait))
        {
            if (_queue.empty())
            {
                // Return mutex
                RaftMutex_unlock(_queueMutex);
                return false;
            }

            // read the item and remove
            elem = _queue.front();
            _queue.pop();
            _count = _queue.size();

            // Return mutex
            RaftMutex_unlock(_queueMutex);
            return true;
        }
        return false;
    }

    [[nodiscard]] bool peek(ElemT& elem, uint32_t maxMsToWait = DEFAULT_MAX_MS_TO_WAIT)
    {
        // Avoid taking the mutex if empty
        if (_count == 0)
            return false;

        // Get Mutex
        if (RaftMutex_lock(_queueMutex, maxMsToWait))
        {
            if (_queue.empty())
            {
                // Return mutex
                RaftMutex_unlock(_queueMutex);
                return false;
            }

            // read the item (but do not remove)
            elem = _queue.front();

            // Return mutex
            RaftMutex_unlock(_queueMutex);
            return true;
        }
        return false;
    }

    // Remove the front item without copying it (e.g. after a successful peek by the only consumer)
    [[nodiscard]] bool pop(uint32_t maxMsToWait = DEFAULT_MAX_MS_TO_WAIT)
    {
        // Get Mutex
        if (RaftMutex_lock(_queueMutex, maxMsToWait))
        {
            bool removed = !_queue.empty();
            if (removed)
            {
                _queue.pop();
                _count = _queue.size();
            }

            // Return mutex
            RaftMutex_unlock(_queueMutex);
            return removed;
        }
        return false;
    }

    // Returns false if the queue mutex could not be obtained (queue not cleared)
    bool clear(uint32_t maxMsToWait = DEFAULT_MAX_MS_TO_WAIT)
    {
        if (RaftMutex_lock(_queueMutex, maxMsToWait))
        {
            // Clear queue
            while(!_queue.empty())
                _queue.pop();
            _count = 0;

            // Return mutex
            RaftMutex_unlock(_queueMutex);
            return true;
        }
        return false;
    }

    // Lock-free (maxMsToWait is unused and retained for backward compatibility)
    uint32_t count(uint32_t maxMsToWait = 0)
    {
        (void)maxMsToWait;
        return _count;
    }

    uint32_t maxLen()
    {
        return _maxLen;
    }

    // Lock-free
    bool canAcceptData()
    {
        return _count < _maxLen;
    }

private:
    std::queue<ElemT> _queue;
    static const uint32_t DEFAULT_MAX_QUEUE_LEN = 50;
    std::atomic<uint32_t> _maxLen{DEFAULT_MAX_QUEUE_LEN};

    // Count of items in queue (mirrors _queue.size() and is updated under the mutex)
    std::atomic<uint32_t> _count{0};

    // Mutex for queue
    RaftMutex _queueMutex;
};
