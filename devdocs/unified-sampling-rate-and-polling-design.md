# Unified Design: Device Sampling Rate, Polling Rate, and Buffer Configuration

## 1. Introduction

This document consolidates the design for device sampling rate, polling rate, and buffer configuration into a single reference. It supersedes six previously separate design documents (now removed — see §14).

### Current State

The firmware API `/devman/devconfig` supports setting **all three parameters in a single call**:

```
GET /devman/devconfig?deviceid=1_6a&sampleRateHz=416&intervalUs=19230&numSamples=10
```

| Feature | Status |
|---------|--------|
| Poll interval (`intervalUs`) | ✅ Implemented |
| Buffer depth (`numSamples`) | ✅ Implemented |
| Sample rate (`sampleRateHz`) | ⚠️ Partially implemented (firmware needs enriched map parsing) |
| Variable-length poll data storage | ✅ Implemented (PollDataAggregator `_actualLengths`) |
| Variable-length wire format (devbin v2) | ✅ Implemented (length-prefixed, per-device sequence counters) |
| Dynamic I2C read length (`PollReadLenExpr`) | ✅ Implemented (e.g. `$0.w12:mask0FFF*2:max96`) |
| JS client `setSampleRate()` convenience | ⚠️ Partially implemented (needs enriched map support) |
| Enriched `_conf.rate` map format | ⬜ Planned (objects with `w`, `i`, `s` fields) |

### Goal

Provide a **single API call** that sets a device's sampling rate and has the framework automatically adjust polling interval, buffer sizes, and any other parameters needed to achieve the desired data throughput — or report that the desired rate cannot be achieved within system constraints.

---

## 2. Conceptual Model

### 2.1. Three Rates and a Buffer

There are three independent timing parameters and a buffer size in the data path from sensor to client:

```
┌──────────────┐     ┌──────────────┐     ┌──────────────────┐     ┌────────────┐
│   Sensor     │     │   I2C Bus    │     │   Firmware       │     │   Client   │
│              │     │              │     │   Ring Buffer    │     │   (BLE/WS) │
│ Sample Rate  │────▸│ Poll Rate    │────▸│ numSamples       │────▸│ Publish    │
│ (device ODR) │     │ (intervalUs) │     │ (buffer depth)   │     │ Rate       │
└──────────────┘     └──────────────┘     └──────────────────┘     └────────────┘
```

| Parameter | Definition | Where Configured | Default Source |
|-----------|-----------|------------------|----------------|
| **Sample Rate** | How often the device's sensor/ADC acquires new data | Device registers (device-specific) | `initValues` in DeviceTypeRecord |
| **Poll Rate** | How often the host reads data from the device via I2C | `pollInfo.i` in DeviceTypeRecord | DeviceTypeRecord JSON |
| **Buffer Depth** | How many poll results are stored in the firmware ring buffer before publishing | `pollInfo.s` in DeviceTypeRecord | DeviceTypeRecord JSON |
| **Publish Rate** | How often buffered data is sent to the client via BLE/WebSocket | StatePublisher configuration | SysType JSON |

### 2.2. Relationships Between Parameters

For data to flow reliably without loss:

1. **Poll Rate ≥ Sample Rate** (for non-FIFO devices): The host must read data at least as fast as the sensor produces it, otherwise samples are overwritten
2. **Poll Rate ≥ Sample Rate / FIFO Depth** (for FIFO devices): The host must drain the FIFO before it overflows
3. **Buffer Depth × Poll Rate ≥ Publish Rate**: The ring buffer must hold enough samples to span at least one publish interval
4. **BLE/WS Bandwidth ≥ Sample Rate × Sample Size**: The comms channel must handle the data throughput

### 2.3. Current Separately-Configurable Parameters

The existing API capabilities:

| Parameter | Current Status | API |
|-----------|---------------|-----|
| Poll Interval | ✅ Implemented | `/devman/devconfig?deviceid=X&intervalUs=Y` |
| Buffer Depth (numSamples) | ✅ Implemented (see §5) | `/devman/devconfig?deviceid=X&numSamples=Y` |
| Sample Rate (device ODR) | ✅ Implemented (see §6) | `/devman/devconfig?deviceid=X&sampleRateHz=Y` |
| Combined Setting | ✅ Implemented | `/devman/devconfig?deviceid=X&intervalUs=Y&numSamples=Z&sampleRateHz=W` |
| Coordinated Setting (auto-calc) | ⚠️ Partially implemented (see §7) | JS client `DeviceManager.setSampleRate()` |

---

## 3. Existing Architecture

### 3.1. Data Flow: Polling to Publishing

```
DeviceTypeRecord JSON
  │  ("pollInfo": { "c": "...", "i": 100, "s": 1 })
  │
  ▼  DeviceTypeRecords::getPollInfo()
DevicePollingInfo
  │  .pollIntervalUs, .numPollResultsToStore, .pollResultSizeIncTimestamp
  │
  ▼  DeviceIdentMgr::identifyDevice()
PollDataAggregator(numResultsToStore, resultSize)
  │  [fixed-stride flat ring buffer]
  │
  ▼  DevicePollingMgr::taskService()  [poll task, every intervalUs]
I2C read → PollDataAggregator::put() (actual length tracked, no padding)
  │
  ▼  getQueuedDeviceDataBinary()  [publish task]
PollDataAggregator::get() → N × resultSize bytes
  │
  ▼  RaftDevice::genBinaryDataMsg()
devbin record → BLE/WebSocket → JS client
  │
  ▼  RaftDeviceManager.handleClientMsgBinary()
Parse devbin → RaftAttributeHandler.processMsgAttrGroup() → device state
```

### 3.2. Ownership Hierarchy

```
BusStatusMgr::_addrStatus  (std::vector<BusAddrRecord>)
  └─ BusAddrRecord
       └─ DeviceStatus  deviceStatus
            ├─ DevicePollingInfo  deviceIdentPolling
            │    ├─ pollIntervalUs         (poll rate)
            │    ├─ numPollResultsToStore   (buffer depth, the "s" value)
            │    └─ pollResultSizeIncTimestamp (max result size)
            └─ shared_ptr<PollDataAggregatorIF>  pDataAggregator
                 └─ PollDataAggregator  (_maxElems, _ringBuffer, _resultSize)
```

### 3.3. Action Mechanism for Device Configuration

Actions are the existing mechanism for sending commands to I2C devices from the JS client. They are already used for servo angle control, LED pixels, actuator enable/disable, and — crucially — for the LSM6DS_FIFO ODR configuration (`_conf.rate`).

#### How Actions Work End-to-End

1. **Definition**: Actions are defined in `DeviceTypeRecords.json` under `devInfoJson.actions[]`
2. **Discovery**: The JS client fetches device type info (including actions) via `/devman/typeinfo`
3. **UI Rendering**: The dashboard `DeviceActionsForm` component renders controls from the action definitions
4. **Sending**: `RaftDeviceManager.sendAction()` encodes the value according to the action's type, applies multiplier/subtract/map transformations, prepends the write prefix, and sends via `/devman/cmdraw?bus=X&addr=Y&hexWr=Z`
5. **Firmware**: `DeviceManager` processes `cmdraw` by forwarding the hex bytes as an I2C write to the specified bus/address

#### Action Field Reference

| Field | Type | Description | Example |
|-------|------|-------------|---------|
| `n` | string | Action name | `"angle"`, `"_conf.rate"` |
| `t` | string | Struct pack type code | `"B"`, `">H"`, `"<h"` |
| `w` | string | Write prefix (hex bytes prepended to encoded value) | `"0001"`, `"10"` |
| `wz` | string | Postfix bytes appended after encoded value | `"0064"` |
| `r` | array | Valid range `[min, max]` | `[-180, 180]`, `[12.5, 6660]` |
| `d` | number | Default value | `0`, `104` |
| `mul` | number | Multiply input by this before encoding | `10` |
| `sub` | number | Subtract from input before encoding | `0` |
| `map` | object | Discrete value mapping: display value → hex bytes | `{"104": "1048&114C&0a26"}` |
| `concat` | boolean | Concatenate all values into single command | `true` |
| `f` | string | Format/display specifier | `".1f"`, `"d"`, `"LEDPIX"` |
| `desc` | string | Human-readable description | `"Sensor ODR (Hz)"` |

#### Multi-Write Map Entries

Map values can contain `&`-separated hex strings, causing multiple I2C writes in sequence. This is already used by the LSM6DS_FIFO `_conf.rate` action which writes three registers at once:

```json
"map": {
    "104": "1048&114C&0a26"
}
```

This sends: CTRL1_XL(0x10)=0x48, CTRL2_G(0x11)=0x4C, FIFO_CTRL5(0x0A)=0x26 — setting accel ODR, gyro ODR, and FIFO ODR in a single action.

---

## 4. Existing Implementation: Poll Interval (`intervalUs`)

The poll interval feature follows a clean three-layer delegation pattern that all new features should replicate.

| Layer | File | What it does |
|-------|------|--------------|
| **RaftBus** (virtual base) | `RaftBus.h` | Default no-op returning `false` |
| **BusI2C** (override) | `BusI2C.h` | Delegates to `_busStatusMgr.setDevicePollIntervalUs()` |
| **BusStatusMgr** (impl) | `BusStatusMgr.cpp` | Locks mutex, finds `BusAddrRecord*`, sets `pollIntervalUs` field |
| **DeviceManager API** | `DeviceManager.cpp` | Parses query param, calls `pBus->setDevicePollIntervalUs()` |

---

## 5. Feature: Buffer Depth (`numSamples`) — ✅ Implemented

### 5.1. Design

The `numSamples` parameter on `/devman/devconfig` controls the number of poll results stored in the `PollDataAggregator` ring buffer. This corresponds to the `"s"` field in `pollInfo`.

Changing `numSamples` at runtime clears all previously buffered data. The ring buffer is re-allocated and the head/count reset.

### 5.2. Implementation

Follows the same three-layer delegation pattern as `intervalUs`:

#### Layer 1: PollDataAggregatorIF / PollDataAggregator — Add `resize()`

```cpp
// PollDataAggregatorIF.h — new pure virtual
virtual bool resize(uint32_t numResultsToStore) = 0;

// PollDataAggregator.h — implementation
bool resize(uint32_t numResultsToStore) override
{
    if (!RaftMutex_lock(_accessMutex, RAFT_MUTEX_WAIT_FOREVER))
        return false;
    _ringBuffer.resize(numResultsToStore * _resultSize);
    _ringBufHeadOffset = 0;
    _ringBufCount = 0;
    _maxElems = numResultsToStore;
    RaftMutex_unlock(_accessMutex);
    return true;
}
```

#### Layer 2: RaftBus / BusI2C — Add `setDeviceNumSamples()` / `getDeviceNumSamples()`

Virtual methods on `RaftBus`, overridden in `BusI2C` to delegate to `BusStatusMgr`.

#### Layer 3: BusStatusMgr — Implementation

Lock mutex, find `BusAddrRecord`, update `numPollResultsToStore` field, then call `pDataAggregator->resize()`.

#### Layer 4: DeviceManager API — Parse and dispatch

Parse `numSamples` from query string, validate > 0, call `pBus->setDeviceNumSamples()`.

### 5.3. Validation

- `numSamples = 0` is rejected
- Consider an upper bound (e.g. 1000) to prevent excessive memory allocation
- Response always includes current `pollIntervalUs` and `numSamples`

### 5.4. Files Changed

| File | Change |
|------|--------|
| `RaftCore/.../Bus/PollDataAggregatorIF.h` | Add `virtual bool resize(uint32_t) = 0` |
| `RaftI2C/.../BusI2C/PollDataAggregator.h` | Implement `resize()` |
| `RaftCore/.../Bus/RaftBus.h` | Add virtual `setDeviceNumSamples()` / `getDeviceNumSamples()` |
| `RaftI2C/.../BusI2C/BusI2C.h` | Override both, delegate to `_busStatusMgr` |
| `RaftI2C/.../BusI2C/BusStatusMgr.h/.cpp` | Implement set/get methods |
| `RaftCore/.../DeviceManager/DeviceManager.cpp` | Parse `numSamples` param, call bus method, update response |

---

## 6. Feature: Sample Rate Control (`sampleRateHz`) — ✅ Implemented

### 6.1. Two Device Categories

| Category | Description | Sample Rate Mechanism |
|----------|-------------|----------------------|
| **Static Devices** | Custom `RaftDevice` subclasses registered with DeviceFactory | Override virtual methods in C++ |
| **Dynamic I2C Devices** | Auto-discovered on I2C bus, configured via DeviceTypeRecords | Use `_conf.rate` action in `devInfoJson.actions[]` |

### 6.2. Static Device Support

Add virtual methods to `RaftDevice.h`:

```cpp
virtual RaftRetCode setSampleRate(double sampleRateHz) { return RAFT_NOT_IMPLEMENTED; }
virtual double getSampleRate() const { return 0.0; }
virtual bool getSampleRateRange(double& minHz, double& maxHz, double& stepHz) const { return false; }
```

Custom device subclasses override these to write to device-specific registers.

### 6.3. Dynamic I2C Device Support — The `_conf.rate` Action

The mechanism for configuring dynamic device sample rates already exists: the reserved action name `_conf.rate`. The framework recognises this as a sample rate configuration action and can process it automatically when `sampleRateHz` is specified via the API.

#### How `_conf.rate` Works

The `_conf.rate` action is defined within a device's `devInfoJson.actions[]` array. It uses the standard action schema with a `map` of Hz values to configuration objects.

#### Enriched Map Format (Design Decision)

Each map entry is an **object** containing the I2C write codes and recommended polling parameters:

```json
{
    "n": "_conf.rate",
    "t": "B",
    "r": [12.5, 833],
    "d": 104,
    "desc": "Sensor ODR (Hz)",
    "map": {
        "12.5": { "w": "1018&111C&0a0E", "i": 100000, "s": 2 },
        "26":   { "w": "1028&112C&0a16", "i": 50000,  "s": 2 },
        "52":   { "w": "1038&113C&0a1E", "i": 50000,  "s": 5 },
        "104":  { "w": "1048&114C&0a26", "i": 50000,  "s": 10 },
        "208":  { "w": "1058&115C&0a2E", "i": 38000,  "s": 10 },
        "416":  { "w": "1068&116C&0a36", "i": 19230,  "s": 10 },
        "833":  { "w": "1078&117C&0a3E", "i": 9600,   "s": 10 }
    }
}
```

| Map Entry Field | Type | Required | Description |
|-----------------|------|----------|-------------|
| `w` | string | Yes | I2C write codes (`&`-separated for multi-register writes) |
| `i` | integer | No | Recommended poll interval in microseconds |
| `s` | integer | No | Recommended buffer depth (numSamples) |

**If `i` or `s` are absent**, the JS client falls back to auto-calculation (see §7.3). This means only devices that benefit from hand-tuned per-rate parameters need to specify them — simple devices can use objects with just `"w"`.

**Rationale for enriched map (vs. separate `pollCfg` field)**:
- Self-contained: each rate entry fully describes its configuration
- The map field was added recently and is unique to the LSM6DS — no backward compatibility concern
- Simpler to maintain: adding/removing a rate entry is a single edit, not two
- The device type author who understands the hardware specifies the exact optimal configuration per rate

#### Supported Rate Range

Rates above 833 Hz are **not supported** and have been removed. The current firmware design (I2C polling, ring buffer, BLE/WebSocket publish) cannot reliably sustain data throughput above ~833 samples/second. At 833 Hz with 12 bytes per sample, the raw data rate is ~10 KB/s — already at the limit of BLE throughput. Higher rates would require fundamental changes to the data path (DMA, streaming mode, etc.).

The `r` (range) field is updated from `[12.5, 6660]` to `[12.5, 833]` accordingly.

#### Execution Flow for `sampleRateHz` on Dynamic Devices

When `sampleRateHz` is specified via the `/devman/devconfig` API, the processing is:

**JS client side** (via `setSampleRate()`):
1. Looks up the device's `_conf.rate` action definition from cached device type info
2. Finds the closest matching Hz key in the map
3. Reads `i` and `s` from the map entry if present, otherwise auto-calculates (§7.3)
4. Sends a single coordinated API call with all parameters

**Firmware side** (in `DeviceManager::apiDevManDevConfig()`):
1. Parses `sampleRateHz` parameter
2. Looks up the device type record for the addressed device
3. Iterates `devInfoJson.actions[]` for entries with names starting with `_conf.`
4. Looks up the requested Hz value in the action's `map`
5. Extracts the `"w"` field from the map entry object (I2C write codes)
6. Handles `&`-separated multi-write values (e.g. `"1068&116C&0a36"`)
7. Sends each segment as an I2C write via `BusRequestInfo`
8. Separately processes `intervalUs` and `numSamples` parameters as before

This means a single API call sets sample rate, poll rate, and buffer depth atomically:

```
GET /devman/devconfig?deviceid=1_6a&sampleRateHz=416&intervalUs=19230&numSamples=10
```

**Note**: The JS client can also send `_conf.rate` actions independently via `sendAction()` / `cmdraw` for finer control, but the `setSampleRate()` convenience method is the recommended approach as it coordinates all parameters.

#### Firmware Map Parsing Update Required

The firmware's `sampleRateHz` handler currently treats map values as plain strings. It must be updated to handle the enriched object format — extracting the `"w"` field when the map value is a JSON object rather than a string. The `i` and `s` fields in the map are consumed by the JS client only; the firmware ignores them (it receives `intervalUs` and `numSamples` as separate query parameters).

### 6.4. Multiple Sample Rates per Device

Some devices (e.g. IMUs) have separate sample rates for different subsystems (accelerometer vs. gyroscope). This can be handled with multiple `_conf.*` actions:

```json
"actions": [
    { "n": "_conf.accel_rate", "map": { "104": "1048", ... } },
    { "n": "_conf.gyro_rate",  "map": { "104": "114C", ... } }
]
```

The primary `_conf.rate` action should configure all subsystems together (as the LSM6DS_FIFO already does).

---

## 7. Feature: Coordinated Rate Setting — ✅ Implemented

### 7.1. The Problem

Setting the sample rate without adjusting polling parameters leads to suboptimal or broken data collection:

- **High sample rate, slow poll**: FIFO overflow, data loss
- **High sample rate, small buffer**: Ring buffer overflow between publish cycles
- **Low sample rate, fast poll**: Wasted I2C bandwidth re-reading stale data

### 7.2. Coordinated API

The firmware `/devman/devconfig` supports setting all parameters in a single call:

```
GET /devman/devconfig?deviceid=1_6a&sampleRateHz=416&intervalUs=19230&numSamples=10
```

The JS client `setSampleRate()` convenience method auto-calculates `intervalUs` and `numSamples` and sends a single coordinated call.

### 7.3. Auto-Calculation Logic (Implemented in JS client)

The `DeviceManager.setSampleRate()` method:

1. Finds the closest supported rate from the device's `_conf.rate` action `map` keys
2. **Checks the map entry for recommended `i` (intervalUs) and `s` (numSamples) fields**
3. If the map entry provides `i` and/or `s`, uses those as defaults (can still be overridden via `options`)
4. If the map entry does not provide `i` and/or `s`, falls back to heuristic calculation:
   - Calculates `samplePeriodUs = 1,000,000 / actualRate`
   - Targets ~50ms poll interval: `numSamples = floor(50000 / samplePeriodUs)`, clamped to `[1, maxNumSamples]`
   - Sets `intervalUs = numSamples × samplePeriodUs × 0.8` (80% safety margin), clamped to `[5000, 1000000]`
5. Explicit `options.intervalUs` / `options.numSamples` always take highest priority

### 7.4. Implementation Location

The coordinated calculation is implemented in the **JS client** (`RaftDeviceManager.setSampleRate()`):

- The JS client has access to the device type info including `_conf.rate` action definitions with supported rate values
- The firmware's role is to faithfully execute the configured parameters; the client chooses appropriate parameters
- This avoids adding complex auto-tuning logic to the resource-constrained ESP32

### 7.5. JS Client API — ✅ Implemented

```typescript
interface SampleRateResult {
    ok: boolean;
    requestedRateHz: number;
    actualRateHz: number;           // Closest supported rate from _conf.rate map
    intervalUs: number;             // Polling interval set
    numSamples: number;             // Buffer depth set
    error?: string;
}

// RaftDeviceManager / RaftDeviceMgrIF method:
async setSampleRate(deviceKey: string, sampleRateHz: number, options?: {
    numSamples?: number;        // Explicit buffer depth override
    intervalUs?: number;        // Explicit poll interval override
    maxNumSamples?: number;     // Cap on auto-calculated numSamples (default: 20)
}): Promise<SampleRateResult>;
```

### 7.6. Future Enhancement: Bandwidth and Feasibility Checks

The `setSampleRate()` method could be extended to warn about infeasible configurations:

| Check | Condition | Warning |
|-------|-----------|---------|
| I2C bus capacity | `sampleRate × sampleSize > busCapacity` | "Requested rate exceeds I2C bus bandwidth" |
| BLE throughput | `sampleRate × sampleSize > bleMaxThroughput` | "Data rate may exceed BLE bandwidth; expect dropped samples" |
| FIFO overflow risk | `pollInterval > fifoCapacity / sampleRate` | "Poll interval too slow for FIFO capacity at this rate" |
| Minimum poll interval | `intervalUs < 1000` (1ms minimum) | "Cannot poll faster than 1ms" |

---

## 8. Variable-Length Poll Data — ✅ Implemented

### 8.1. The Problem

Currently, all poll results are stored and transmitted at a fixed size (`pollResultSizeIncTimestamp`) determined at identification time. For FIFO-based devices like the LSM6DS, actual reads are often much shorter than the maximum. Zero-padding wastes ring buffer memory and transmission bandwidth, and the trailing zeros can be misinterpreted as valid data.

### 8.2. Variable-Length I2C Reads: `PollReadLenExpr`

The `PollReadLenExpr` mechanism (already partly designed) allows poll commands to compute read lengths dynamically from earlier read results:

```json
"c": "0x3a=r4&0x3e=r{$0.w12:mask0FFF*2:max192}"
```

This reads the FIFO status (4 bytes), extracts the word count, computes the byte count, and reads only the actual data present. The `:max192` suffix provides the maximum for buffer sizing.

#### Expression Syntax

| Element | Meaning |
|---------|---------|
| `$N` | Result bytes from poll operation index N |
| `$N[B]` | Byte at offset B from operation N |
| `$N.wE` / `$N.WE` | Little/big-endian extract, E bits wide |
| `:maskHH` | Bitwise AND with hex value |
| `*N`, `/N`, `+N`, `-N` | Arithmetic |
| `:maxN`, `:minN` | Clamp to bounds |
| `:alignN` | Round down to multiple of N |

### 8.3. Variable-Length Storage and Transmission

Once `PollReadLenExpr` produces variable-length results, the downstream data path must handle them:

#### Ring Buffer (PollDataAggregator) — ✅ Implemented

The `PollDataAggregator` now has a parallel `std::vector<uint16_t> _actualLengths` tracking the actual length of each entry. The ring buffer remains fixed-stride (allocated at max size) for O(1) indexing, but each entry records its true length.

- `put()`: Accepts `data.size() <= _resultSize`. Stores actual data, zero-fills remainder, records actual length.
- `getWithLengths()`: Returns entries trimmed to actual lengths with per-entry size metadata.
- `resize()`: Also resizes `_actualLengths`.

#### Wire Format (devbin v2)

Change the per-device record to use length-prefixed samples:

```
Record header (10 bytes):
  recordLen (2B BE), statusBus (1B), address (4B BE), devTypeIdx (2B BE), deviceSeqNum (1B)

Samples (repeated until recordLen consumed):
  sampleLen (1B), sampleData (sampleLen bytes)
```

- `sampleLen` is 1 byte (max 255), sufficient for any I2C read
- The 1-byte per-sample overhead is acceptable
- A per-device `deviceSeqNum` (wrapping uint8) enables per-device drop detection
- An envelope-level `sequenceCounter` (byte 2 of the 3-byte envelope) enables frame drop detection

#### Bandwidth Savings Example

LSM6DS3 at 104 Hz, 50ms poll, 10 polls per publish:
- Current: 10 × 100 bytes (padded) = 1000 bytes + 9-byte header = 1009 bytes
- With variable-length: 10 × (1 + 64) bytes = 650 bytes + 10-byte header = 660 bytes
- **Savings: 349 bytes (35%)**

#### JS Parser Changes

Update `RaftDeviceManager.handleClientMsgBinary()`:
- `devbinEnvelopeLen` = 3 (magic + topic + seqNum)
- `recordHeaderLen` = 8 (busInfo + addr + devTypeIdx + deviceSeqNum)
- Parse length-prefixed samples within each record instead of iterating by fixed `resp.b` chunks

### 8.4. Firmware Padding Removal — ✅ Implemented

The zero-padding in `DevicePollingMgr::taskService()` has been removed. The relaxed `PollDataAggregator::put()` accepts shorter data and tracks actual lengths.

---

## 9. Implementation Plan

### Phase 1: `numSamples` Runtime Configuration — ✅ Complete

All tasks implemented:
1. ✅ `resize()` added to `PollDataAggregatorIF` / `PollDataAggregator`
2. ✅ `setDeviceNumSamples()` / `getDeviceNumSamples()` added to `RaftBus` → `BusI2C` → `BusStatusMgr`
3. ✅ `numSamples` parameter handling in `DeviceManager::apiDevManDevConfig()`
4. ✅ API help string and response JSON updated

### Phase 2: Variable-Length Poll Data Pipeline — Partially Complete

**Goal**: Support variable-length I2C reads through to the client without zero-padding.

**Completed**:
1. ✅ `_actualLengths` tracking in `PollDataAggregator`; relaxed `put()` size check
2. ✅ `getWithLengths()` added to `PollDataAggregatorIF` / `PollDataAggregator`
3. ✅ Zero-padding removed from `DevicePollingMgr::taskService()`
4. ✅ `resize()` handles `_actualLengths`
5. ✅ `PollReadLenExpr` expression evaluator (e.g. `$0.w12:mask0FFF*2:max96`)
6. ✅ `PollReadLenExpr` integrated into `DeviceTypeRecords` parsing and `DevicePollingMgr` execution
7. ✅ Per-device sequence counter (`deviceSeqNum`) in `RaftDevice`
8. ✅ Envelope sequence counter (`_devbinEnvelopeSeqCounter`) in `DeviceManager`
9. ✅ `DeviceIdentMgr::getQueuedDeviceDataBinary()` emits length-prefixed per-sample records
10. ✅ `RaftDevice::genBinaryDataMsg()` includes `deviceSeqNum`
11. ✅ JS `RaftDeviceManager.handleClientMsgBinary()` parses devbin v2 format
12. ✅ `DeviceTypeRecords.json` for LSM6DS uses `PollReadLenExpr` (`$0.w12:mask0FFF*2:max96`)

### Phase 3: Sample Rate Control — Partially Complete

**Firmware** — Partially Complete:
- ✅ `DeviceManager::apiDevManDevConfig()` parses `sampleRateHz`, looks up `_conf.*` actions from `devInfoJson`, sends multi-write I2C commands
- ✅ All three parameters (`sampleRateHz`, `intervalUs`, `numSamples`) can be set in a single API call
- ⬜ Firmware map parsing must be updated to handle enriched map objects (extract `"w"` field from object entries)

**JS Client** — Partially Complete:
1. ✅ `setSampleRate()` convenience method added to `RaftDeviceManager` and `RaftDeviceMgrIF`
2. ⬜ `setSampleRate()` must be updated to read `i` and `s` from enriched map entries before falling back to auto-calculation
3. ⬜ `sendAction()` must be updated to handle enriched map objects (extract `"w"` field for I2C writes)
4. ✅ Supports explicit overrides via `options` parameter (`numSamples`, `intervalUs`, `maxNumSamples`)

**DeviceTypeRecords** — Pending:
1. ⬜ Update LSM6DS `_conf.rate` map entries from strings to objects with `w`, `i`, `s` fields
2. ⬜ Remove rates above 833 Hz (1660, 3330, 6660)
3. ⬜ Update `r` range from `[12.5, 6660]` to `[12.5, 833]`

### Phase 4: Enhanced Features

**Goal**: Refinements and additional capabilities.

**Tasks**:
1. Query-only mode for `/devman/devconfig` (read current config without changing)
2. Per-slot coordinated rate setting (set sample rate for all devices in a slot)
3. Auto-coordination mode in firmware (for clients that don't want to calculate)
4. Multiple sample rates per device (e.g. `_conf.accel_rate` and `_conf.gyro_rate`)
5. Validation of sample rates against device-reported supported ranges

**Dependencies**: Phases 1–3.

---

## 10. API Reference Summary

### `/devman/devconfig` — Complete Specification

```
GET /devman/devconfig?<device>&<params>
```

#### Device Addressing

| Style | Example |
|-------|---------|
| Device ID | `deviceid=1_25` |
| Bus + Address | `bus=I2CA&addr=25` |

#### Parameters (all optional, at least one recommended)

| Parameter | Type | Description |
|-----------|------|-------------|
| `intervalUs` | integer | Polling interval in microseconds. Must be > 0. |
| `numSamples` | integer | Buffer depth (poll results to store). Must be > 0. Clears buffered data. |
| `sampleRateHz` | number | Device sample rate in Hz. Requires device support (`_conf.rate` action or `setSampleRate()` virtual). |
| `coordinated` | boolean | When `true` with `sampleRateHz`, auto-adjust `intervalUs` and `numSamples`. |

#### Response

```json
{
    "rslt": "ok",
    "deviceID": "1_6a",
    "pollIntervalUs": 10000,
    "numSamples": 5,
    "sampleRateHz": 104,
    "sampleRateSupported": true
}
```

#### Error Responses

| Error | Cause |
|-------|-------|
| `failAddrMissing` | No `deviceid` or `addr` parameter |
| `failBusMissing` | Using `addr` style but no `bus` parameter |
| `failBusNotFound` | Bus name/number does not exist |
| `failInvalidDeviceID` | Device ID could not be parsed |
| `failInvalidInterval` | `intervalUs` is 0 |
| `failInvalidNumSamples` | `numSamples` is 0 |
| `failInvalidSampleRate` | Sample rate outside device support range |
| `failSampleRateNotSupported` | Device has no `_conf.rate` action or `setSampleRate()` |
| `failUnsupportedBus` | Bus does not support the operation |

---

## 11. Devbin Wire Format (v2)

### Envelope (3 bytes)

```
Byte 0:   0xDB              magic+version (unchanged)
Byte 1:   topicIndex        uint8 (0x00–0xFE; 0xFF = no topic)
Byte 2:   envelopeSeqNum    uint8, wrapping (per-topic counter)
```

### Per-Device Record

```
Bytes 0–1:  recordLen       uint16 BE (body bytes that follow, min 8)
Byte  2:    statusBus       bit7=online, bit6=pendingDeletion, bits3:0=busNumber
Bytes 3–6:  address         uint32 BE
Bytes 7–8:  devTypeIdx      uint16 BE
Byte  9:    deviceSeqNum    uint8, wrapping (per-device counter)
Bytes 10+:  samples         length-prefixed sample data
```

### Sample Packing

```
[sampleLen(1B)][sampleData(sampleLen bytes)]  × N
```

Parser reads samples until `recordLen - recordHeaderLen` bytes consumed.

---

## 12. Design Decisions and Rationale

### Why enriched map objects (not separate `pollCfg`)?

The `_conf.rate` action's `map` field uses objects (`{ "w": ..., "i": ..., "s": ... }`) rather than plain strings for I2C writes with a separate `pollCfg` companion field:

- **Self-contained**: Each rate entry fully describes its complete configuration — I2C writes, poll interval, and buffer depth
- **No backward compatibility concern**: The `map` field was added recently and is unique to the LSM6DS; no deployed firmware relies on the old string format
- **Simpler maintenance**: Adding or removing a rate entry is a single edit in one place, not coordinated changes across two fields
- **Device-author-friendly**: The person who understands the hardware specifies exact optimal parameters per rate, rather than relying on heuristic formulas that may not account for FIFO alignment, bus timing, etc.
- **Graceful degradation**: If `i` and `s` are omitted, the JS client falls back to auto-calculation — only devices that benefit from hand-tuning need to provide them

### Why remove rates above 833 Hz?

The current system architecture imposes practical limits:

- At 833 Hz with 12 bytes/sample (6-axis IMU), raw data rate is ~10 KB/s — already near BLE throughput limits
- I2C polling at sub-millisecond intervals competes with other bus traffic and the RTOS scheduler
- The ring buffer and devbin publish path add latency that cannot keep up at higher rates
- Rates of 1660, 3330, and 6660 Hz would require fundamentally different approaches (DMA, streaming, dedicated channels) — they can be re-added if/when the data path supports them

### Why coordination logic in JS, not firmware?

- The JS client has full access to device type metadata including action definitions and response schemas
- The firmware's ESP32 has limited memory; action map parsing and coordination logic add complexity
- The client can make informed decisions about publish rate, BLE bandwidth, and application requirements
- The firmware provides the low-level building blocks (`intervalUs`, `numSamples`, `cmdraw`); the client orchestrates

### Why `sampleRateHz` is handled in firmware via `_conf.*` action parsing

- A single `/devman/devconfig` call can atomically set `sampleRateHz`, `intervalUs`, and `numSamples` together
- The firmware parses `_conf.*` actions on-demand from `devInfoJson` — no persistent storage needed
- Multi-write map values (`&`-separated hex) are expanded and sent as individual I2C bus requests
- The JS client can still use `sendAction()` / `cmdraw` independently for finer-grained control

### Why fixed-stride ring buffer with actual-length tracking?

- Keeps O(1) indexing and bounded memory allocation (critical for ESP32)
- Adding a small `_actualLengths` array (2 bytes per slot) is negligible overhead
- Variable-stride ring buffers introduce fragmentation and complex wrap-around logic
- The savings from variable-length are realised at transmission time, not storage time

### Why per-sample length prefixes in devbin?

- 1-byte overhead per sample is minimal
- Eliminates the ambiguity of where one sample ends and the next begins
- Removes the dependency on `resp.b` for multi-sample iteration in the JS parser
- Works correctly for both fixed-size and variable-size devices

### Why keep devbin version at 0xDB?

- Nothing has been released since the last format change
- Both firmware and JS are updated simultaneously
- No backward compatibility is required at this point

---

## 13. Example: End-to-End Sample Rate Change for LSM6DS

### User Action

User selects "416 Hz" from the sample rate dropdown for device `1_6a`.

### JS Client Processing

```typescript
// Using the setSampleRate() convenience method:
// 1. Looks up _conf.rate action, finds "416" map entry:
//    { "w": "1068&116C&0a36", "i": 19230, "s": 10 }
// 2. Uses i=19230 and s=10 from the map entry (no heuristic calculation needed)
// 3. Sends single coordinated API call:
const result = await deviceManager.setSampleRate('1_6a', 416);
// result: { ok: true, requestedRateHz: 416, actualRateHz: 416, intervalUs: 19230, numSamples: 10 }

// Explicit overrides still take priority:
const result2 = await deviceManager.setSampleRate('1_6a', 416, { numSamples: 15 });
// Uses i=19230 from map, but numSamples=15 from override
```

### Firmware Processing

1. `DeviceManager::apiDevManDevConfig()` receives:
   `devman/devconfig?deviceid=1_6a&sampleRateHz=416&intervalUs=19230&numSamples=10`
2. Parses `sampleRateHz=416`, looks up `_conf.rate` action in device type JSON, finds map entry `"416"`, extracts `"w": "1068&116C&0a36"`, sends three I2C writes (CTRL1_XL, CTRL2_G, FIFO_CTRL5)
3. Sets poll interval to 19230 µs via `BusStatusMgr::setDevicePollIntervalUs()`
4. Resizes ring buffer to 10 slots via `BusStatusMgr::setDeviceNumSamples()`
5. Returns success with current configuration:
   ```json
   {"rslt":"ok","deviceID":"1_6a","pollIntervalUs":19230,"numSamples":10,"sampleRateHz":416}
   ```

### Data Flow (steady state)

1. LSM6DS3 samples at 416 Hz into its FIFO
2. Every ~19ms, `DevicePollingMgr` reads FIFO status (4 bytes) + FIFO data (~96 bytes for ~8 samples)
3. Result (100 bytes actual, not padded) stored in the 10-slot ring buffer
4. Every ~50ms (publish cycle), ring buffer is drained, length-prefixed records are sent via BLE
5. JS client parses devbin, extracts per-sample data, updates device state

---

## 14. References

| Document | Status |
|----------|--------|
| `variable-length-poll-reads.md` | Retained as standalone spec for `PollReadLenExpr` syntax |
| `lsm6ds-fifo-device-type-spec.md` | Retained as standalone device type spec |
| `raftcore-rest-api-endpoints.md` | Retained (general REST API guide) |

### Previously Superseded Documents (Removed)

- `devconfig-numsamples-implementation-plan.md` — Superseded by §5
- `device-sampling-rate-control-design.md` — Superseded by §6–7
- `dynamic-poll-data-size-design.md` — Superseded by §8
- `devbin-v2-variable-length-samples.md` — Superseded by §8.3, §11
- `devbin-sequence-counter-plan.md` — Superseded by §11
- `devconfig-api.md` — Superseded by §10
