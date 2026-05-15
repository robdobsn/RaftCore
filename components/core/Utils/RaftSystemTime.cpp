/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftSystemTime - "system time changed" notification hub
//
// Rob Dobson 2026
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "RaftSystemTime.h"
#include "RaftThreading.h"

#include <vector>
#include <utility>

namespace
{
    struct Entry
    {
        uint32_t handle;
        RaftSystemTime::ChangedCB cb;
    };

    // Singleton state - lazily initialised so we work correctly even if
    // registerChangedCB is called before any global ctor would have run.
    struct State
    {
        RaftMutex mutex;
        std::vector<Entry> entries;
        uint32_t nextHandle = 1;
        State()
        {
            RaftMutex_init(mutex);
        }
    };

    State& getState()
    {
        static State s;
        return s;
    }
}

namespace RaftSystemTime
{

uint32_t registerChangedCB(ChangedCB cb)
{
    if (!cb)
        return 0;
    State& s = getState();
    if (!RaftMutex_lock(s.mutex, RAFT_MUTEX_WAIT_FOREVER))
        return 0;
    uint32_t handle = s.nextHandle++;
    if (s.nextHandle == 0)
        s.nextHandle = 1;
    s.entries.push_back({ handle, std::move(cb) });
    RaftMutex_unlock(s.mutex);
    return handle;
}

void unregisterChangedCB(uint32_t handle)
{
    if (handle == 0)
        return;
    State& s = getState();
    if (!RaftMutex_lock(s.mutex, RAFT_MUTEX_WAIT_FOREVER))
        return;
    for (auto it = s.entries.begin(); it != s.entries.end(); ++it)
    {
        if (it->handle == handle)
        {
            s.entries.erase(it);
            break;
        }
    }
    RaftMutex_unlock(s.mutex);
}

void notifyChanged(const char* source)
{
    State& s = getState();
    // Take a snapshot of the callbacks under the lock so we don't hold the
    // mutex while invoking arbitrary user code (which might try to register
    // or unregister and would otherwise deadlock).
    std::vector<ChangedCB> snapshot;
    if (RaftMutex_lock(s.mutex, RAFT_MUTEX_WAIT_FOREVER))
    {
        snapshot.reserve(s.entries.size());
        for (const auto& e : s.entries)
            snapshot.push_back(e.cb);
        RaftMutex_unlock(s.mutex);
    }
    const char* srcTag = source ? source : "";
    for (auto& cb : snapshot)
    {
        if (cb)
            cb(srcTag);
    }
}

} // namespace RaftSystemTime
