/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftSystemTime - "system time changed" notification hub
//
// Lightweight registry of callbacks invoked whenever some part of the system
// changes the wall clock (via settimeofday() or equivalent). Peripherals such
// as an external RTC chip can register a callback and mirror the new time
// without each setter needing to know about every consumer.
//
// Callers should invoke notifyChanged(source) AFTER they have updated the
// system clock. Callbacks may run on a variety of threads (e.g. the lwIP
// tcpip thread for the SNTP sync callback) so implementations should keep
// the work brief and avoid blocking operations / heavy logging.
//
// Rob Dobson 2026
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <functional>
#include <stdint.h>

namespace RaftSystemTime
{
    // Callback signature - source is a short human-readable tag describing
    // what set the time (e.g. "sntp", "api", "rtc", "ble"). Callbacks should
    // read the current time themselves via time(nullptr) / gettimeofday().
    using ChangedCB = std::function<void(const char* source)>;

    // Register a callback. Returns a non-zero handle that can be passed to
    // unregisterChangedCB. Returns 0 on failure.
    uint32_t registerChangedCB(ChangedCB cb);

    // Unregister a previously-registered callback. Safe to call with an
    // invalid/stale handle.
    void unregisterChangedCB(uint32_t handle);

    // Notify all registered callbacks that the system clock has been updated.
    // source is a short tag and must remain valid for the duration of the
    // call (callbacks are invoked synchronously).
    void notifyChanged(const char* source);
}
