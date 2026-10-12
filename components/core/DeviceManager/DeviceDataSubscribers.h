/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// DeviceDataSubscribers.h
//
// Fan-out of device data to any number of subscribers.
//
// A bus keeps ONE data callback per device address (BusAddrRecord), so two
// subscribers to the same device used to overwrite each other silently.  The
// DeviceManager now owns that single slot on every device and this class
// dispatches each sample to every matching subscriber - by device ID or by
// device type index - with a per-subscriber, per-device rate limit.
//
// Threading: dispatch() runs on a bus (or device) task; add() and remove()
// run on any task.  The subscriber list is guarded by a mutex that is never
// held while a subscriber is called.  remove() does not return while that
// subscriber is being called on another task (so the subscriber may be
// destroyed afterwards), matching the guarantee the bus gives.  A subscriber
// that removes itself (or another) from inside its own callback does not
// wait - it would wait on itself - and the entry is erased when the dispatch
// finishes.
//
// Dispatch uses no heap: matching subscribers are collected into a fixed
// array (MAX_PER_DISPATCH; more are counted in dispatchOverflows()).
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <list>
#include <vector>
#include <utility>
#include "RaftThreading.h"
#include "RaftDeviceConsts.h"

class DeviceDataSubscribers
{
public:
    static const uint32_t MAX_PER_DISPATCH = 8;
    static const uint32_t MAX_DISPATCH_TASKS = 8;
    static const uint32_t QUIESCE_MAX_MS = 500;

    DeviceDataSubscribers()
    {
        RaftMutex_init(_mutex);
    }
    ~DeviceDataSubscribers()
    {
        RaftMutex_destroy(_mutex);
    }
    DeviceDataSubscribers(const DeviceDataSubscribers&) = delete;
    DeviceDataSubscribers& operator=(const DeviceDataSubscribers&) = delete;

    /// @brief Subscribe to one device
    void addForDevice(RaftDeviceID deviceID, RaftDeviceDataChangeCB cb, uint32_t minTimeBetweenReportsMs, const void* pCallbackInfo)
    {
        add(Sub::Kind::DEVICE_ID, deviceID, DEVICE_TYPE_INDEX_INVALID, cb, minTimeBetweenReportsMs, pCallbackInfo);
    }

    /// @brief Subscribe to every device of a type
    void addForType(DeviceTypeIndexType deviceTypeIndex, RaftDeviceDataChangeCB cb, uint32_t minTimeBetweenReportsMs, const void* pCallbackInfo)
    {
        add(Sub::Kind::DEVICE_TYPE, RaftDeviceID(), deviceTypeIndex, cb, minTimeBetweenReportsMs, pCallbackInfo);
    }

    /// @brief Unsubscribe from one device (matched by device ID and callback info)
    /// @return number of subscriptions removed
    uint32_t removeForDevice(RaftDeviceID deviceID, const void* pCallbackInfo)
    {
        return remove([&](const Sub& s) {
            return s.kind == Sub::Kind::DEVICE_ID && s.deviceID == deviceID && s.pCallbackInfo == pCallbackInfo;
        });
    }

    /// @brief Unsubscribe from a type (matched by type index and callback info)
    /// @return number of subscriptions removed
    uint32_t removeForType(DeviceTypeIndexType deviceTypeIndex, const void* pCallbackInfo)
    {
        return remove([&](const Sub& s) {
            return s.kind == Sub::Kind::DEVICE_TYPE && s.deviceTypeIndex == deviceTypeIndex && s.pCallbackInfo == pCallbackInfo;
        });
    }

    /// @brief Deliver one sample to every matching subscriber
    /// @param deviceID the device the sample came from
    /// @param deviceTypeIndex its type (DEVICE_TYPE_INDEX_INVALID if not yet identified)
    /// @param data the sample; moved to the last subscriber, copied for the others
    /// @param timeNowMs for rate limiting
    /// @return number of subscribers called
    uint32_t dispatch(RaftDeviceID deviceID, DeviceTypeIndexType deviceTypeIndex, std::vector<uint8_t>&& data, uint32_t timeNowMs)
    {
        Sub* toCall[MAX_PER_DISPATCH];
        uint32_t numToCall = 0;
        const void* thisTask = currentTask();
        if (!RaftMutex_lock(_mutex, RAFT_MUTEX_WAIT_FOREVER))
            return 0;
        for (Sub& s : _subs)
        {
            if (s.removed || !s.matches(deviceID, deviceTypeIndex))
                continue;
            if (!s.rateAllows(deviceID, timeNowMs))
                continue;
            if (numToCall >= MAX_PER_DISPATCH)
            {
                _dispatchOverflows++;
                continue;
            }
            s.inProgress++;
            toCall[numToCall++] = &s;
        }
        if (numToCall)
            noteDispatchTask(thisTask, +1);
        RaftMutex_unlock(_mutex);

        // Entries are not erased while inProgress > 0, so these pointers stay valid
        for (uint32_t i = 0; i < numToCall; i++)
        {
            if (i + 1 < numToCall)
                toCall[i]->cb(deviceTypeIndex, data, toCall[i]->pCallbackInfo);
            else
                toCall[i]->cb(deviceTypeIndex, std::move(data), toCall[i]->pCallbackInfo);
        }

        if (numToCall)
        {
            RaftMutex_lock(_mutex, RAFT_MUTEX_WAIT_FOREVER);
            for (uint32_t i = 0; i < numToCall; i++)
                toCall[i]->inProgress--;
            noteDispatchTask(thisTask, -1);
            eraseFinishedRemovals();
            RaftMutex_unlock(_mutex);
        }
        return numToCall;
    }

    /// @brief True if any live subscription matches the device
    bool hasSubscriber(RaftDeviceID deviceID, DeviceTypeIndexType deviceTypeIndex)
    {
        bool found = false;
        if (!RaftMutex_lock(_mutex, RAFT_MUTEX_WAIT_FOREVER))
            return false;
        for (const Sub& s : _subs)
            if (!s.removed && s.matches(deviceID, deviceTypeIndex))
                found = true;
        RaftMutex_unlock(_mutex);
        return found;
    }

    uint32_t count()
    {
        uint32_t n = 0;
        if (!RaftMutex_lock(_mutex, RAFT_MUTEX_WAIT_FOREVER))
            return 0;
        for (const Sub& s : _subs)
            n += s.removed ? 0 : 1;
        RaftMutex_unlock(_mutex);
        return n;
    }
    uint32_t dispatchOverflows() const { return _dispatchOverflows; }

private:
    struct LastReport
    {
        RaftDeviceID deviceID;
        uint32_t timeMs = 0;
    };
    struct Sub
    {
        enum class Kind { DEVICE_ID, DEVICE_TYPE };
        Kind kind = Kind::DEVICE_ID;
        RaftDeviceID deviceID;
        DeviceTypeIndexType deviceTypeIndex = DEVICE_TYPE_INDEX_INVALID;
        RaftDeviceDataChangeCB cb = nullptr;
        uint32_t minTimeBetweenReportsMs = 0;
        const void* pCallbackInfo = nullptr;
        uint32_t inProgress = 0;
        bool removed = false;
        // One entry per device this subscription has reported (a type
        // subscription covers several); allocated the first time a device
        // reports, not per sample
        std::vector<LastReport> lastReports;

        bool matches(RaftDeviceID id, DeviceTypeIndexType typeIdx) const
        {
            if (kind == Kind::DEVICE_ID)
                return deviceID == id;
            return (typeIdx != DEVICE_TYPE_INDEX_INVALID) && (deviceTypeIndex == typeIdx);
        }
        bool rateAllows(RaftDeviceID id, uint32_t timeNowMs)
        {
            if (minTimeBetweenReportsMs == 0)
                return true;
            for (LastReport& r : lastReports)
            {
                if (!(r.deviceID == id))
                    continue;
                if (timeNowMs - r.timeMs < minTimeBetweenReportsMs)
                    return false;
                r.timeMs = timeNowMs;
                return true;
            }
            lastReports.push_back({id, timeNowMs});
            return true;
        }
    };
    struct DispatchTask
    {
        const void* task = nullptr;
        uint32_t depth = 0;
    };

    std::list<Sub> _subs;
    RaftMutex _mutex;
    DispatchTask _dispatchTasks[MAX_DISPATCH_TASKS];
    uint32_t _dispatchOverflows = 0;

    void add(Sub::Kind kind, RaftDeviceID deviceID, DeviceTypeIndexType typeIdx,
             RaftDeviceDataChangeCB cb, uint32_t minTimeBetweenReportsMs, const void* pCallbackInfo)
    {
        if (!cb)
            return;
        Sub s;
        s.kind = kind;
        s.deviceID = deviceID;
        s.deviceTypeIndex = typeIdx;
        s.cb = cb;
        s.minTimeBetweenReportsMs = minTimeBetweenReportsMs;
        s.pCallbackInfo = pCallbackInfo;
        if (!RaftMutex_lock(_mutex, RAFT_MUTEX_WAIT_FOREVER))
            return;
        _subs.push_back(std::move(s));
        RaftMutex_unlock(_mutex);
    }

    template <typename Match>
    uint32_t remove(Match match)
    {
        uint32_t numRemoved = 0;
        bool mustWait = false;
        if (!RaftMutex_lock(_mutex, RAFT_MUTEX_WAIT_FOREVER))
            return 0;
        for (auto it = _subs.begin(); it != _subs.end();)
        {
            if (it->removed || !match(*it))
            {
                ++it;
                continue;
            }
            numRemoved++;
            if (it->inProgress == 0)
            {
                it = _subs.erase(it);
                continue;
            }
            // Being called right now: no new call can start (removed), and the
            // dispatch that holds it erases it when it finishes
            it->removed = true;
            mustWait = true;
            ++it;
        }
        // Waiting from inside a dispatch on this task would wait on ourselves
        const bool calledFromDispatch = isDispatchTask(currentTask());
        RaftMutex_unlock(_mutex);

        if (mustWait && !calledFromDispatch)
        {
            const uint32_t startMs = nowMs();
            while (true)
            {
                bool stillThere = false;
                RaftMutex_lock(_mutex, RAFT_MUTEX_WAIT_FOREVER);
                for (const Sub& s : _subs)
                    if (s.removed && match(s))
                        stillThere = true;
                RaftMutex_unlock(_mutex);
                if (!stillThere)
                    break;
                if (nowMs() - startMs > QUIESCE_MAX_MS)
                    break;
                RaftThread_sleep(1);
            }
        }
        return numRemoved;
    }

    void eraseFinishedRemovals()
    {
        for (auto it = _subs.begin(); it != _subs.end();)
        {
            if (it->removed && it->inProgress == 0)
                it = _subs.erase(it);
            else
                ++it;
        }
    }

    void noteDispatchTask(const void* task, int delta)
    {
        for (DispatchTask& d : _dispatchTasks)
        {
            if (d.task == task && d.depth > 0)
            {
                d.depth += delta;
                if (d.depth == 0)
                    d.task = nullptr;
                return;
            }
        }
        if (delta > 0)
        {
            for (DispatchTask& d : _dispatchTasks)
            {
                if (d.depth == 0)
                {
                    d.task = task;
                    d.depth = 1;
                    return;
                }
            }
        }
    }
    bool isDispatchTask(const void* task) const
    {
        for (const DispatchTask& d : _dispatchTasks)
            if (d.depth > 0 && d.task == task)
                return true;
        return false;
    }

    static const void* currentTask()
    {
#if defined(FREERTOS_CONFIG_H) || defined(FREERTOS_H) || defined(ESP_PLATFORM)
        return (const void*)xTaskGetCurrentTaskHandle();
#elif defined(__linux__)
        return (const void*)(uintptr_t)pthread_self();
#else
        return nullptr;
#endif
    }
    static uint32_t nowMs()
    {
#if defined(FREERTOS_CONFIG_H) || defined(FREERTOS_H) || defined(ESP_PLATFORM)
        return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
#elif defined(__linux__)
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
#else
        return 0;
#endif
    }
};
