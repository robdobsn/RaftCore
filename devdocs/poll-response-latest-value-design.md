# Poll Response Access Modes: Latest-Value vs Queued-Data Design

## Problem Statement

`DeviceManager::getBusDeviceNamedValue()` (Phase 3 of the RaftScript orchestrator plan) calls `getDecodedPollResponses()`, which in turn calls `BusStatusMgr::getBusElemPollResponses()` with `maxResponsesToReturn=0` — meaning "drain all queued samples." This **destructively consumes** the ring buffer, starving the publish pipeline (`getQueuedDeviceDataJson` / `getQueuedDeviceDataBinary`) of data.

The fundamental tension: a script calling `sensor.ax` every tick wants the **latest value** (non-destructive peek), while the publish pipeline wants to **drain all queued samples** so they can be forwarded to subscribers.

---

## Current Architecture

### Data Flow

```
I2C poll task
  │
  ▼
BusStatusMgr::handlePollResult()
  │
  ├──► PollDataAggregator::put()          ──► ring buffer (N slots, destructive drain)
  │                                        ──► _latestValue (single-slot, non-destructive)
  │
  ├──► RaftDeviceDataChangeCB              ──► push callback (throttled by minTimeBetweenReportsMs)
  │     [fires outside mutex]                   └─ used by DataLogger registrations
  │
  ▼
Consumer pulls (periodic tick or on-demand)
  ├──► getQueuedDeviceDataJson/Binary()   ──► drains ring buffer (destructive)
  │     [publish pipeline: devjson/devbin]
  │
  ├──► getDecodedPollResponses()          ──► drains ring buffer (destructive, maxResp=0)
  │     [used by getBusDeviceNamedValue]
  │
  └──► getLatestValue()                   ──► reads _latestValue (non-destructive)
        [exists on PollDataAggregator but NOT surfaced through bus interfaces]
```

### Key Existing Structures

| Layer | Class | Relevant Method |
|-------|-------|-----------------|
| Storage | `PollDataAggregator` | `put()` writes to ring buffer AND `_latestValue` |
| Storage | `PollDataAggregator` | `get()` drains ring buffer (destructive) |
| Storage | `PollDataAggregator` | `getLatestValue()` reads single-slot cache (non-destructive) |
| Device | `DeviceStatus` | `getPollResponses()` → delegates to aggregator `get()` |
| Bus mgr | `BusStatusMgr` | `getBusElemPollResponses()` → `DeviceStatus::getPollResponses()` |
| Bus mgr | `BusStatusMgr` | `handlePollResult()` → stores + fires callback |
| Interface | `RaftBusDevicesIF` | `getDecodedPollResponses()` → drains via getBusElemPollResponses |
| Interface | `RaftBusDevicesIF` | No `getLatestDecodedPollResponse()` exists |
| DeviceMgr | `DeviceManager` | `getDevicesDataJSON/Binary()` → drains via getQueued* |
| DeviceMgr | `DeviceManager` | `getBusDeviceNamedValue()` → drains via getDecodedPollResponses ← **THE PROBLEM** |

### The _latestValue Cache

`PollDataAggregator` already maintains a `_latestValue` single-slot buffer that is:
- **Updated on every `put()`** — always holds the most recent raw poll data
- **Non-destructive on read** — `getLatestValue()` copies data, clears only a "newness" flag
- **Thread-safe** — protected by the same mutex as the ring buffer
- **NOT surfaced** through `DeviceStatus`, `BusStatusMgr`, `RaftBusDevicesIF`, or `RaftBus`

This is exactly what `getBusDeviceNamedValue()` needs, but it's currently unreachable from `DeviceManager`.

---

## Three Use Cases

### 1. Data Logging (push callback)

**Requirement:** Every poll result must reach the log — no gaps.

**Mechanism:** `registerForDeviceData()` installs a `RaftDeviceDataChangeCB` on the `BusAddrRecord`. The callback fires immediately (outside mutex) on every poll completion, throttled by `minTimeBetweenReportsMs`. The raw poll data is passed directly to the callback — it does not come from the ring buffer.

**Impact of this design:** None. Logging is already on a separate path — it receives data via push, not by draining the ring buffer. The ring buffer could be empty or full; logging is unaffected.

**Status:** Working correctly. No changes needed.

### 2. Subscription Requesting All Messages (drain queue)

**Requirement:** Subscribers receive every queued poll result — suitable for experiment data collection, high-fidelity recording.

**Mechanism:** The publish pipeline (`getDevicesDataJSON`/`getDevicesDataBinary`) periodically drains the ring buffer via `getQueuedDeviceDataJson()`/`getQueuedDeviceDataBinary()`. Each call returns all accumulated samples since the last call and removes them from the buffer. If the subscriber can't keep up, the ring buffer overwrites the oldest samples (FIFO overflow).

**Impact of this design:** This is the current default behavior. Works well when the consumer matches the producer rate. Risk of data loss under backpressure is already managed by the ring buffer size (`numPollResultsToStore` from device type config).

**Status:** Working correctly. No changes needed for this use case, but see interaction concern below.

### 3. Subscription/Script Requesting Only the Latest Value (non-destructive peek)

**Requirement:** Scripts, robot control loops, and UI dashboards want the most recent sensor reading. They don't care about intermediate samples. Reading should not consume data needed by other consumers.

**Mechanism needed:** Access to the `_latestValue` cache already maintained by `PollDataAggregator`, surfaced through the bus interface chain so `DeviceManager::getBusDeviceNamedValue()` can use it.

**Example use case — robot control:**
```
loop {
    angle = imu.ax          // just the latest reading, non-destructive
    motor.speed = pid(angle) // control loop at script tick rate
}
```

**Status:** The storage exists (`PollDataAggregator::_latestValue`) but is not reachable from DeviceManager. This is the gap.

---

## Interaction Concern

Even if use case #2 (drain queue) and use case #3 (latest peek) are both working correctly, there is a subtle interaction to consider:

If `getBusDeviceNamedValue()` currently drains the ring buffer (via `getDecodedPollResponses`), and the publish pipeline also drains it, they compete. Whichever runs first gets the samples; the other gets nothing. This means:

- A script reading `sensor.ax` at 50Hz would drain the ring buffer, and subscribers would receive empty publishes
- Conversely, if the publish pipeline drains first, `getBusDeviceNamedValue()` might return stale or empty data

**This is why `getBusDeviceNamedValue()` must use the latest-value path, not the queue-drain path.**

---

## Proposed Design

### Core Change: Surface `getLatestValue()` Through the Interface Chain

Add a non-destructive latest-value accessor at each layer:

#### 1. `DeviceStatus` — add `getLatestPollResponse()`

```cpp
// DeviceStatus.h
bool getLatestPollResponse(uint64_t& dataTimeUs, std::vector<uint8_t>& data) const
{
    if (pDataAggregator)
        return pDataAggregator->getLatestValue(dataTimeUs, data);
    return false;
}
```

#### 2. `BusStatusMgr` — add `getBusElemLatestPollResponse()`

```cpp
// BusStatusMgr.h / .cpp
bool getBusElemLatestPollResponse(BusElemAddrType address,
        DeviceOnlineState& onlineState, uint16_t& deviceTypeIndex,
        uint64_t& dataTimeUs, std::vector<uint8_t>& data)
{
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return false;

    BusAddrRecord* pAddrStatus = findAddrStatusRecordEditable(address);
    bool result = false;
    if (pAddrStatus)
    {
        onlineState = pAddrStatus->onlineState;
        deviceTypeIndex = pAddrStatus->deviceStatus.getDeviceTypeIndex();
        result = pAddrStatus->deviceStatus.getLatestPollResponse(dataTimeUs, data);
    }
    RaftMutex_unlock(_busElemStatusMutex);
    return result;
}
```

#### 3. `RaftBusDevicesIF` — add `getLatestDecodedPollResponse()`

```cpp
// RaftBusDevicesIF.h
virtual bool getLatestDecodedPollResponse(BusElemAddrType address,
        void* pStructOut, uint32_t structOutSize,
        RaftBusDeviceDecodeState& decodeState) const
{
    return false; // default: not implemented
}
```

#### 4. `DeviceIdentMgr` — implement `getLatestDecodedPollResponse()`

```cpp
// DeviceIdentMgr.cpp
bool DeviceIdentMgr::getLatestDecodedPollResponse(BusElemAddrType address,
        void* pStructOut, uint32_t structOutSize,
        RaftBusDeviceDecodeState& decodeState) const
{
    DeviceOnlineState onlineState;
    uint16_t deviceTypeIndex;
    uint64_t dataTimeUs;
    std::vector<uint8_t> latestData;

    if (!_busStatusMgr.getBusElemLatestPollResponse(address, onlineState,
            deviceTypeIndex, dataTimeUs, latestData))
        return false;

    if (latestData.empty())
        return false;

    // Decode single response
    uint32_t numDecoded = decodePollResponses(deviceTypeIndex,
            latestData.data(), latestData.size(),
            pStructOut, structOutSize, 1, decodeState);
    return numDecoded > 0;
}
```

#### 5. `DeviceManager::getBusDeviceNamedValue()` — use latest path

Change the current call:
```cpp
// BEFORE (destructive — drains queue)
uint32_t numDecoded = pDevicesIF->getDecodedPollResponses(addr, decodedBuf,
        devTypeRec.pollStructSize, 1, decodeState);

// AFTER (non-destructive — reads latest cache)
bool decoded = pDevicesIF->getLatestDecodedPollResponse(addr, decodedBuf,
        devTypeRec.pollStructSize, decodeState);
```

### Why This is the Right Design

1. **No new data structures** — `_latestValue` already exists and is already updated on every `put()`. We're just plumbing it through the interface chain.

2. **No impact on existing consumers** — The publish pipeline continues draining the ring buffer. Logging continues via push callbacks. Nothing changes for use cases #1 and #2.

3. **Thread-safe** — `getLatestValue()` is already protected by the same mutex in `PollDataAggregator`. The `BusStatusMgr` wrapper adds the bus-level mutex. The pattern mirrors the existing `getBusElemPollResponses()` call chain.

4. **Consistent with existing patterns** — The new methods follow the exact same structure as the existing `getBusElemPollResponses()` → `getPollResponses()` → `get()` chain, just using `getLatestValue()` at the bottom.

5. **Efficient** — No ring buffer manipulation, no allocation beyond copying the latest value. Suitable for high-frequency script access.

---

## StatePublisher Analysis

### Architecture

`StatePublisher` (in `RaftSysMods/components/StatePublisher/`) is the RaftSysMod responsible for periodic data publishing to connected clients. It uses a two-layer model:

- **PubSource**: Registered data sources. Each has a `pubTopic` name, a `topicIndex`, and two callbacks:
  - `SysMod_publishMsgGenFn _msgGenFn`: generates a message (`bool(uint16_t topicIndex, CommsChannelMsg& msg)`)
  - `SysMod_stateDetectCB _stateDetectFn`: produces a state hash for change detection (`void(uint16_t topicIndex, std::vector<uint8_t>& stateHash)`)

- **Subscription**: Per (topic + channelID) pair, created via the `/subscription` REST API or `createSubscription()`. Stores its own copies of the PubSource's callbacks (avoiding lookup-per-loop), plus:
  - `_trigger`: `TRIGGER_ON_TIME_INTERVALS`, `TRIGGER_ON_STATE_CHANGE`, or `TRIGGER_ON_TIME_OR_CHANGE`
  - `_rateHz` / `_betweenPubsMs`: publish rate
  - `_minTimeBetweenMsgsMs`: floor on publish interval
  - `_lastStateHash`: for change detection
  - `_isPending` / `_lastCheckMs`: timer state

### Current Data Flow

```
DeviceManager::postSetup()
  │
  ├── registerDataSource("Publish", "devjson", msgGenFn, stateDetectFn)
  │     msgGenFn calls getDevicesDataJSON() → getQueuedDeviceDataJson()  ──► DRAINS ring buffer
  │
  └── registerDataSource("Publish", "devbin", msgGenFn, stateDetectFn)
        msgGenFn calls getDevicesDataBinary() → getQueuedDeviceDataBinary() ──► DRAINS ring buffer

StatePublisher::loop()
  │
  for each Subscription:
  ├── check interval elapsed or state hash changed
  ├── if shouldPublish → attemptPublish(sub)
  │     └── sub._msgGenFn(topicIndex, msg)   ──► calls DeviceManager's lambda ──► DRAINS
  └── send msg via CommsCoreIF::outboundHandleMsg()
```

### Key Finding: No Delivery Mode Exists

StatePublisher has **no concept** of "deliver all queued data" vs "deliver only the latest value". The `msgGenFn` callback always drains the ring buffer because that's what `getDevicesDataJSON()` / `getDevicesDataBinary()` do internally. There's no way for a subscriber to request a non-destructive snapshot.

### The Conflict With Multiple Consumers

If two subscriptions exist for "devjson" (same PubSource), they share the same `msgGenFn`. Whichever subscription's publish fires first drains the ring buffer. The second subscription gets empty data. Adding `getBusDeviceNamedValue()` as a third consumer makes this worse — scripts and subscribers compete for the same destructive drain.

---

## Subscription Delivery Mode Design

### Naming: `DeliveryMode_t`

Two delivery modes for subscriptions:

| Mode | Enum Value | Config String | Behavior | Use Case |
|------|-----------|---------------|----------|----------|
| **All Data** | `DELIVERY_ALL_DATA` | `"all"` | Drain ring buffer, deliver all accumulated samples since last publish | Experiment recording, high-fidelity data collection |
| **Latest Only** | `DELIVERY_LATEST_ONLY` | `"latest"` | Deliver only the most recent sample (non-destructive) | Robot control loops, UI dashboards, scripts |

`DELIVERY_ALL_DATA` is the default (preserves current behavior).

### Approach: Dual Callback Registration

The cleanest design adds an optional second `msgGenFn` for latest-only mode, without changing the existing callback signature or breaking any current code.

#### 1. Extend `registerDataSource` (RaftSysMod.h)

```cpp
// New signature with optional latest-mode callback
virtual uint16_t registerDataSource(const char* pubTopic,
        SysMod_publishMsgGenFn msgGenCB,
        SysMod_stateDetectCB stateDetectCB,
        SysMod_publishMsgGenFn latestMsgGenCB = nullptr)
{
    return UINT16_MAX;
}
```

#### 2. Extend PubSource (StatePublisher.h)

```cpp
class PubSource
{
public:
    String _pubTopic;
    uint16_t _topicIndex = UINT16_MAX;
    SysMod_publishMsgGenFn _msgGenFn = nullptr;         // drain mode (existing)
    SysMod_publishMsgGenFn _latestMsgGenFn = nullptr;    // latest-only mode (new)
    SysMod_stateDetectCB _stateDetectFn = nullptr;
};
```

#### 3. Add DeliveryMode to Subscription (StatePublisher.h)

```cpp
// In RaftSysMod.h alongside TriggerType_t
enum DeliveryMode_t
{
    DELIVERY_ALL_DATA,       // Drain queue, deliver all accumulated samples (default)
    DELIVERY_LATEST_ONLY     // Deliver only the most recent value (non-destructive peek)
};

// In Subscription class
DeliveryMode_t _deliveryMode = DELIVERY_ALL_DATA;
SysMod_publishMsgGenFn _latestMsgGenFn = nullptr;   // copied from PubSource
```

#### 4. Subscription Publish Logic (StatePublisher.cpp)

```cpp
// In publishData() — select callback based on delivery mode
bool msgOk = false;
if (sub._deliveryMode == DELIVERY_LATEST_ONLY && sub._latestMsgGenFn)
    msgOk = sub._latestMsgGenFn(sub._topicIndex, endpointMsg);
else if (sub._msgGenFn)
    msgOk = sub._msgGenFn(sub._topicIndex, endpointMsg);
```

#### 5. DeviceManager Registers Both Callbacks

```cpp
// In DeviceManager::postSetup()
getSysManager()->registerDataSource("Publish", "devjson",
    // All-data callback (existing — drains queue)
    [this](uint16_t topicIndex, CommsChannelMsg& msg) {
        String statusStr = getDevicesDataJSON(topicIndex);
        msg.setFromBuffer((uint8_t*)statusStr.c_str(), statusStr.length());
        return true;
    },
    // State detect (existing)
    [this](uint16_t topicIndex, std::vector<uint8_t>& stateHash) {
        return getDevicesHash(stateHash);
    },
    // Latest-only callback (new — non-destructive snapshot)
    [this](uint16_t topicIndex, CommsChannelMsg& msg) {
        String statusStr = getDevicesLatestDataJSON(topicIndex);  // new method using getLatestValue()
        msg.setFromBuffer((uint8_t*)statusStr.c_str(), statusStr.length());
        return statusStr.length() > 0;
    }
);
```

#### 6. Subscription API Extension

```json
{
    "action": "update",
    "pubRecs": [
        {
            "topic": "devjson",
            "rateHz": 10.0,
            "trigger": "time",
            "deliveryMode": "latest"
        }
    ]
}
```

Parse `"deliveryMode"` in `apiSubscription()` and `createSubscription()`:
```cpp
String deliveryModeStr = pubRecConf.getString("deliveryMode", "all");
DeliveryMode_t deliveryMode = parseDeliveryMode(deliveryModeStr);
```

### Why Dual Callback vs Other Approaches

| Approach | Pros | Cons |
|----------|------|------|
| **Dual callback** (chosen) | No signature change to existing callback. Backward-compatible. Each data source controls its own latest-only implementation. | Slightly more registration boilerplate |
| Separate topics (`devjson-latest`) | Zero StatePublisher changes | Leaks implementation detail into topic naming. Confusing API. Doubles topic count |
| Modified callback signature | Clean single callback | Breaking change to all existing data source registrations across all SysMods |
| Mode arg via capture/context | No API changes | Fragile coupling, hard to test |

### Interaction: All-Data Subscribers Are Not Starved

With this design, a `DELIVERY_LATEST_ONLY` subscription uses a completely separate code path (`_latestMsgGenFn`) that calls `getLatestValue()` — it never touches the ring buffer. `DELIVERY_ALL_DATA` subscriptions continue using `_msgGenFn` which drains the ring buffer as before. The two modes are fully independent and do not compete for data.

---

## Implementation Scope

### Phase A: Surface `getLatestValue()` Through Interface Chain (RaftCore + RaftI2C)

| File | Change |
|------|--------|
| `DeviceStatus.h` | Add `getLatestPollResponse()` inline method |
| `RaftBusDevicesIF.h` | Add virtual `getLatestDecodedPollResponse()` with default `return false` |
| `DeviceManager.cpp` | Change `getBusDeviceNamedValue()` to use new non-destructive path |
| `BusStatusMgr.h/.cpp` (RaftI2C) | Add `getBusElemLatestPollResponse()` |
| `DeviceIdentMgr.h/.cpp` (RaftI2C) | Override `getLatestDecodedPollResponse()` |

This phase alone fixes the `getBusDeviceNamedValue()` ring-buffer drain problem for script access.

### Phase B: Add Delivery Mode to StatePublisher (RaftCore + RaftSysMods)

| File | Change |
|------|--------|
| `RaftSysMod.h` (RaftCore) | Add `DeliveryMode_t` enum. Extend `registerDataSource()` signature with optional `latestMsgGenCB` parameter. Add `deliveryMode` parameter to `createSubscription()` |
| `SysManagerIF.h` / `SysManager.h/.cpp` (RaftCore) | Pass through new `latestMsgGenCB` parameter |
| `DeviceManager.cpp` (RaftCore) | Add `getDevicesLatestDataJSON()` / `getDevicesLatestDataBinary()` methods. Register latest-only callbacks in `postSetup()` |
| `StatePublisher.h` (RaftSysMods) | Add `_latestMsgGenFn` to `PubSource`. Add `_deliveryMode` and `_latestMsgGenFn` to `Subscription` |
| `StatePublisher.cpp` (RaftSysMods) | Parse `deliveryMode` in `apiSubscription()` and `createSubscription()`. Select callback in `publishData()`. Add `parseDeliveryMode()` helper |

### Notes

- Phase A is self-contained — scripts work correctly after Phase A alone.
- Phase B extends the subscriber API so that connected clients (apps, dashboards) can also request latest-only delivery without draining the queue.
- The `PollDataAggregatorIF::getLatestValue()` interface already exists — no changes needed at the aggregator level.
- The RaftI2C changes are in `raftdevlibs/RaftI2C`. Other bus types get the default `return false` from `RaftBusDevicesIF`.
- The `RaftBusDeviceDecodeState` parameter in `getBusDeviceNamedValue()` is currently a local variable (timestamp reconstruction restarts each call). A per-device `decodeState` cache would improve timestamp accuracy — deferrable enhancement.
