# Implementation Plan: Add `numSamples` Parameter to `devman/devconfig` API

## Overview

Add a new optional `numSamples` query parameter to the existing `devman/devconfig` REST API endpoint, allowing runtime configuration of the number of samples stored in the `PollDataAggregator` for a device. This corresponds to the `"s"` field in the `pollInfo` section of a `DeviceTypeRecord`.

### Current API

```
devman/devconfig?deviceid=<busNum_hexAddr>&intervalUs=<microseconds>
devman/devconfig?bus=<busName>&addr=<addr>&intervalUs=<microseconds>
```

### Proposed API

```
devman/devconfig?deviceid=<busNum_hexAddr>&numSamples=<count>
devman/devconfig?bus=<busName>&addr=<addr>&numSamples=<count>
```

Both `intervalUs` and `numSamples` may be supplied together or independently.

---

## Background: How the `"s"` Parameter Currently Works

### DeviceTypeRecord JSON

Each device type declares a `pollInfo` block in its `DeviceTypeRecord`:

```json
"pollInfo": {
    "c": "=r7",
    "i": 100,
    "s": 1
}
```

- `"c"` — poll command (write/read specification)
- `"i"` — poll interval in milliseconds
- `"s"` — number of poll results to store (ring buffer capacity)

### Parsing Chain

1. **`DeviceTypeRecords::getPollInfo()`** ([DeviceTypeRecords.cpp:271](raftdevlibs/RaftCore/components/core/DeviceTypes/DeviceTypeRecords.cpp))
   extracts `"s"` into `pollingInfo.numPollResultsToStore`:
   ```cpp
   pollingInfo.numPollResultsToStore = pollInfo.getLong("s", 0);
   ```

2. **`DeviceIdentMgr::identifyDevice()`** ([DeviceIdentMgr.cpp:116–122](raftdevlibs/RaftI2C/components/RaftI2C/BusI2C/DeviceIdentMgr.cpp))
   constructs a `PollDataAggregator` with the parsed value:
   ```cpp
   deviceTypeRecords.getPollInfo(address, &devTypeRec, deviceStatus.deviceIdentPolling);
   auto pDataAggregator = std::make_shared<PollDataAggregator>(
           deviceStatus.deviceIdentPolling.numPollResultsToStore,
           deviceStatus.deviceIdentPolling.pollResultSizeIncTimestamp);
   deviceStatus.setAndOwnPollDataAggregator(pDataAggregator);
   ```

3. **`PollDataAggregator` constructor** ([PollDataAggregator.h:21–31](raftdevlibs/RaftI2C/components/RaftI2C/BusI2C/PollDataAggregator.h))
   sizes an internal ring buffer:
   ```cpp
   _ringBuffer.resize(numResultsToStore * resultSize);
   _maxElems = numResultsToStore;
   ```

### Ownership Hierarchy

`BusAddrStatus` is a lightweight notification struct (address, online state, flags).
The full per-device record is `BusAddrRecord`, which holds the `DeviceStatus`:

```
BusStatusMgr::_addrStatus  (std::vector<BusAddrRecord>)
  └─ BusAddrRecord
       └─ DeviceStatus  deviceStatus
            ├─ DevicePollingInfo  deviceIdentPolling
            │    └─ numPollResultsToStore   (uint32_t — the "s" value)
            └─ shared_ptr<PollDataAggregatorIF>  pDataAggregator
                 └─ PollDataAggregator  (_maxElems, _ringBuffer)
```

---

## Existing Pattern: `setDevicePollIntervalUs`

The `intervalUs` feature follows a clean three-layer delegation pattern. The new `numSamples` feature should follow exactly the same pattern.

| Layer | File | What it does |
|-------|------|--------------|
| **RaftBus** (virtual base) | [RaftBus.h:220](raftdevlibs/RaftCore/components/core/Bus/RaftBus.h) | Default no-op returning `false` |
| **BusI2C** (override) | [BusI2C.h:189](raftdevlibs/RaftI2C/components/RaftI2C/BusI2C/BusI2C.h) | Delegates to `_busStatusMgr.setDevicePollIntervalUs()` |
| **BusStatusMgr** (impl) | [BusStatusMgr.cpp:795](raftdevlibs/RaftI2C/components/RaftI2C/BusI2C/BusStatusMgr.cpp) | Locks mutex, finds `BusAddrRecord*`, sets `pollIntervalUs` field |
| **DeviceManager API** | [DeviceManager.cpp:956](raftdevlibs/RaftCore/components/core/DeviceManager/DeviceManager.cpp) | Parses query param, calls `pBus->setDevicePollIntervalUs()` |

---

## Implementation Steps

### Step 1: Add `resize()` method to `PollDataAggregatorIF` and `PollDataAggregator`

**Files to change:**
- [PollDataAggregatorIF.h](raftdevlibs/RaftCore/components/core/Bus/PollDataAggregatorIF.h)
- [PollDataAggregator.h](raftdevlibs/RaftI2C/components/RaftI2C/BusI2C/PollDataAggregator.h)

Add a virtual method to resize the ring buffer:

```cpp
// In PollDataAggregatorIF:
/// @brief Resize the aggregator to store a different number of results
/// @param numResultsToStore New number of results to store
/// @return true if resized
virtual bool resize(uint32_t numResultsToStore) = 0;
```

```cpp
// In PollDataAggregator:
bool resize(uint32_t numResultsToStore) override
{
    if (!RaftMutex_lock(_accessMutex, RAFT_MUTEX_WAIT_FOREVER))
        return false;

    // Resize (this clears existing data — acceptable trade-off)
    _ringBuffer.resize(numResultsToStore * _resultSize);
    _ringBufHeadOffset = 0;
    _ringBufCount = 0;
    _maxElems = numResultsToStore;

    RaftMutex_unlock(_accessMutex);
    return true;
}
```

> **Design decision:** Resizing clears existing buffered data. This is the simplest and safest approach since `_resultSize` does not change and we avoid complex partial-copy logic. Callers should expect that changing `numSamples` resets the buffer.

### Step 2: Add `setDeviceNumSamples()` and `getDeviceNumSamples()` to the bus layer

Following the exact same three-layer pattern as `setDevicePollIntervalUs`:

#### 2a. `RaftBus` — virtual base

**File:** [RaftBus.h](raftdevlibs/RaftCore/components/core/Bus/RaftBus.h) (after the existing `getDevicePollIntervalUs` method, around line 232)

```cpp
/// @brief Set number of poll result samples to store for an address
/// @param address Composite address
/// @param numSamples Number of samples to store
/// @return true if applied
virtual bool setDeviceNumSamples(BusElemAddrType address, uint32_t numSamples)
{
    return false;
}

/// @brief Get number of poll result samples stored for an address
/// @param address Composite address
/// @return Number of samples (0 if not supported)
virtual uint32_t getDeviceNumSamples(BusElemAddrType address) const
{
    return 0;
}
```

#### 2b. `BusI2C` — override

**File:** [BusI2C.h](raftdevlibs/RaftI2C/components/RaftI2C/BusI2C/BusI2C.h) (after the `getDevicePollIntervalUs` override, around line 201)

```cpp
virtual bool setDeviceNumSamples(BusElemAddrType address, uint32_t numSamples) override final
{
    return _busStatusMgr.setDeviceNumSamples(address, numSamples);
}

virtual uint32_t getDeviceNumSamples(BusElemAddrType address) const override final
{
    return _busStatusMgr.getDeviceNumSamples(address);
}
```

#### 2c. `BusStatusMgr` — implementation

**Files:**
- [BusStatusMgr.h](raftdevlibs/RaftI2C/components/RaftI2C/BusI2C/BusStatusMgr.h) — declare methods
- [BusStatusMgr.cpp](raftdevlibs/RaftI2C/components/RaftI2C/BusI2C/BusStatusMgr.cpp) — implement

```cpp
// Declaration in BusStatusMgr.h:
bool setDeviceNumSamples(BusElemAddrType address, uint32_t numSamples);
uint32_t getDeviceNumSamples(BusElemAddrType address) const;
```

```cpp
// Implementation in BusStatusMgr.cpp:
bool BusStatusMgr::setDeviceNumSamples(BusElemAddrType address, uint32_t numSamples)
{
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return false;

    bool updated = false;
    BusAddrRecord* pAddrRecord = findAddrStatusRecordEditable(address);
    if (pAddrRecord)
    {
        // Update the stored config value
        pAddrRecord->deviceStatus.deviceIdentPolling.numPollResultsToStore = numSamples;
        // Resize the live aggregator
        if (pAddrRecord->deviceStatus.pDataAggregator)
            updated = pAddrRecord->deviceStatus.pDataAggregator->resize(numSamples);
    }

    RaftMutex_unlock(_busElemStatusMutex);
    return updated;
}

uint32_t BusStatusMgr::getDeviceNumSamples(BusElemAddrType address) const
{
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return 0;

    uint32_t numSamples = 0;
    const BusAddrRecord* pAddrRecord = findAddrStatusRecord(address);
    if (pAddrRecord)
        numSamples = pAddrRecord->deviceStatus.deviceIdentPolling.numPollResultsToStore;

    RaftMutex_unlock(_busElemStatusMutex);
    return numSamples;
}
```

### Step 3: Add `numSamples` parameter handling to `apiDevManDevConfig`

**File:** [DeviceManager.cpp:956–1005](raftdevlibs/RaftCore/components/core/DeviceManager/DeviceManager.cpp)

After the existing `intervalUs` handling block (around line 988), add:

```cpp
// Check if numSamples is provided
String numSamplesStr = jsonParams.getString("numSamples", "");
if (numSamplesStr.length() > 0)
{
    uint32_t numSamples = strtoul(numSamplesStr.c_str(), nullptr, 10);
    if (numSamples == 0)
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failInvalidNumSamples");

    if (!pBus->setDeviceNumSamples(addr, numSamples))
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failUnsupportedBus");
}
```

Update the response to include the current `numSamples` value:

```cpp
// Read back (existing + new)
uint64_t pollIntervalUs = pBus->getDevicePollIntervalUs(addr);
uint32_t numSamplesResult = pBus->getDeviceNumSamples(addr);

String extra = "\"deviceID\":\"" + deviceID.toString() +
               "\",\"pollIntervalUs\":" + String(pollIntervalUs) +
               ",\"numSamples\":" + String(numSamplesResult);
return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, extra.c_str());
```

> **Note:** The existing code returns a `failUnsupportedBus` error if `getDevicePollIntervalUs` returns 0 even when only `numSamples` is being set. This read-back logic should be adjusted to only fail on zero interval when `intervalUs` was the parameter being modified—or the zero check should be removed entirely since the read-back is just informational.

### Step 4: Update API help string

**File:** [DeviceManager.cpp:695](raftdevlibs/RaftCore/components/core/DeviceManager/DeviceManager.cpp)

Update the endpoint description from:

```
" devman/devconfig?deviceid=<deviceId>&intervalUs=<microseconds> - device configuration,"
```

to:

```
" devman/devconfig?deviceid=<deviceId>&intervalUs=<microseconds>&numSamples=<count> - device configuration,"
```

---

## Files Changed (Summary)

| # | File | Change |
|---|------|--------|
| 1 | `raftdevlibs/RaftCore/components/core/Bus/PollDataAggregatorIF.h` | Add `virtual bool resize(uint32_t) = 0` |
| 2 | `raftdevlibs/RaftI2C/components/RaftI2C/BusI2C/PollDataAggregator.h` | Implement `resize()` |
| 3 | `raftdevlibs/RaftCore/components/core/Bus/RaftBus.h` | Add `virtual setDeviceNumSamples()` and `getDeviceNumSamples()` |
| 4 | `raftdevlibs/RaftI2C/components/RaftI2C/BusI2C/BusI2C.h` | Override both, delegating to `_busStatusMgr` |
| 5 | `raftdevlibs/RaftI2C/components/RaftI2C/BusI2C/BusStatusMgr.h` | Declare `setDeviceNumSamples()` and `getDeviceNumSamples()` |
| 6 | `raftdevlibs/RaftI2C/components/RaftI2C/BusI2C/BusStatusMgr.cpp` | Implement both (lock, find record, resize aggregator) |
| 7 | `raftdevlibs/RaftCore/components/core/DeviceManager/DeviceManager.cpp` | Parse `numSamples` param, call bus method, update response JSON, update help string |

---

## Design Considerations

### Clearing data on resize
Resizing the `PollDataAggregator` clears all buffered samples. This is the simplest safe approach — trying to preserve data across a resize would require complex ring-buffer copy logic that isn't worth the added risk, especially since a `numSamples` change is an infrequent configuration operation.

### Validation
- `numSamples` of 0 is rejected as invalid (mirrors the `intervalUs == 0` check).
- Very large values are not explicitly capped; consider adding an upper bound (e.g. 1000) to prevent excessive memory allocation. This could be enforced at the `BusStatusMgr` or `PollDataAggregator` level.

### Thread safety
The `PollDataAggregator::resize()` method acquires `_accessMutex` before modifying the ring buffer. The `BusStatusMgr` methods also acquire `_busElemStatusMutex`. This double-locking (outer BusStatusMgr mutex → inner PollDataAggregator mutex) follows the same ordering pattern as existing poll result storage and should be deadlock-free, but care should be taken to maintain consistent lock ordering throughout the codebase.

### Read-back adjustment
The current `apiDevManDevConfig` unconditionally calls `getDevicePollIntervalUs()` and returns an error if it returns 0. When only `numSamples` is being configured (without `intervalUs`), this could incorrectly fail for devices that don't have a poll interval set. The read-back error logic should be adjusted so that it only fails if neither a valid `intervalUs` nor a valid `numSamples` was configured for the device.

### Backward compatibility
- The REST API change is backward compatible: `numSamples` is optional, and existing callers omitting it will see no change in behaviour.
- The response JSON gains a new `numSamples` field; consumers that ignore unknown fields are unaffected.

---

## Testing

1. **Unit test `PollDataAggregator::resize()`** — add a test case in [test_data_aggregator.cpp](raftdevlibs/RaftI2C/unit_tests/main/test_data_aggregator.cpp) verifying that after `resize()`:
   - Buffer count is 0
   - New capacity matches the requested `numResultsToStore`
   - `put()` and `get()` work correctly with the new size

2. **Integration test via REST API** — issue:
   ```
   GET devman/devconfig?deviceid=1_25&numSamples=10
   ```
   Verify the response includes `"numSamples":10` and subsequent poll data reflects the new buffer size.

3. **Combined test** — set both parameters simultaneously:
   ```
   GET devman/devconfig?deviceid=1_25&intervalUs=50000&numSamples=5
   ```
