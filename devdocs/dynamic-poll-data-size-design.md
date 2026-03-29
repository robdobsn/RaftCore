# Dynamic Poll Data Size Design

## 1. Problem Statement

Currently, poll data from I2C devices is stored and transmitted using a fixed record size determined at device identification time. This size is derived from the `resp.b` field in the device type JSON and represents the maximum possible read size for all poll operations combined, plus a 2-byte timestamp.

This creates inefficiency when:
- A device uses `PollReadLenExpr` (dynamic read expressions like `$0.w12:mask0FFF*2:max96`) where actual reads are often shorter than the maximum
- A device with FIFO returns varying numbers of samples per poll
- A device has conditional reads where some operations may return no data

In these cases, every poll result is padded to the maximum size before storage, wasting ring buffer memory and transmission bandwidth.

## 2. Current Architecture

### 2.1. Data Flow Overview

```
DeviceTypeRecord JSON ("resp.b", "resp.c.c", "resp.a")
  │
  ▼  (compile time: ProcessDevTypeJsonToC.py)
Generated C header (pollDataSizeBytes = resp.b)
  │
  ▼  (runtime: DeviceTypeRecords::getPollInfo())
DevicePollingInfo.pollResultSizeIncTimestamp = Σ(read sizes) + 2
  │
  ▼  (runtime: DeviceIdentMgr, on device identification)
PollDataAggregator(numResults, _resultSize = pollResultSizeIncTimestamp)
  │  [fixed-stride flat ring buffer]
  │
  ▼  (poll task: DevicePollingMgr::taskService())
I2C read → pad to pollResultSizeIncTimestamp → PollDataAggregator::put()
  │
  ▼  (publish: getQueuedDeviceDataBinary())
PollDataAggregator::get() → N × _resultSize bytes as flat buffer
  │
  ▼  (RaftDevice::genBinaryDataMsg())
devbin record: [recordLen(2B)][statusBus(1B)][addr(4B)][devTypeIdx(2B)][pollData...]
  │
  ▼  (BLE / WebSocket)
JS client
  │
  ▼  (RaftDeviceManager::handleClientMsgBinary())
Parse devbin envelope → per-device records using recordLen
  │
  ▼  (RaftAttributeHandler::processMsgAttrGroup())
Decode attributes, iterate by resp.b-sized chunks
```

### 2.2. Fixed-Size Constraint Points

There are **four layers** where fixed-size assumptions exist:

#### Layer 1: PollDataAggregator Ring Buffer (hard constraint)

`PollDataAggregator.h` uses a flat byte array with fixed-stride indexing:

```cpp
_ringBuffer.resize(numResultsToStore * resultSize);
```

The `put()` method **rejects** data that doesn't match `_resultSize`:

```cpp
bool put(uint64_t timeNowUs, const std::vector<uint8_t>& data) override
{
    if (data.size() != _resultSize)  // ← HARD REJECT
        return false;
    memcpy(_ringBuffer.data() + _ringBufHeadOffset, data.data(), _resultSize);
    // ...
}
```

The multi-element `get()` returns `numResponses × _resultSize` bytes and reports `responseSize = _resultSize`, which callers rely on to know where one result ends and the next begins.

#### Layer 2: Firmware Padding (explicit workaround)

In `DevicePollingMgr::taskService()`, after all I2C operations complete, the result is explicitly padded:

```cpp
if (pollDataResult.size() < pollInfo.pollResultSizeIncTimestamp)
    pollDataResult.resize(pollInfo.pollResultSizeIncTimestamp, 0);
```

This ensures `put()` will accept the result, but wastes space.

#### Layer 3: Wire Format (already variable-length)

`RaftDevice::genBinaryDataMsg()` uses the **actual** payload size:

```cpp
uint32_t msgLen = deviceMsgData.size() + 7;  // actual, not max
```

The 2-byte `recordLen` header tells the JS parser exactly how many bytes follow. **This layer is not a constraint.**

#### Layer 4: JS Attribute Parsing (mixed)

**Standard mode** (`processMsgAttrGroup` without custom decoder): Advances position by the actual bytes consumed by attribute parsing, with a fallback to `resp.b`:

```typescript
let pollRespSizeBytes = msgBufIdx - msgDataStartIdx;
if (pollRespSizeBytes < pollRespMetadata.b) {
    pollRespSizeBytes = pollRespMetadata.b;  // fallback to declared size
}
return msgDataStartIdx + pollRespSizeBytes;
```

This means shorter data works fine as long as attributes are at fixed offsets (`at` field) or parsed sequentially. The fallback to `resp.b` only matters when multiple samples are concatenated in one devbin record.

**Custom decoder mode** (pseudocode `resp.c.c`): The custom handler returns attribute values but does **not** advance the buffer position. Advancement is entirely driven by the `resp.b` fallback. **Custom decoders cannot iterate over variable-sized data.**

### 2.3. How Multiple Samples Are Packed

When the publish cycle fires:

1. `getQueuedDeviceDataBinary()` drains ALL results from `PollDataAggregator::get()` → flat buffer of `N × _resultSize`
2. Calls `genBinaryDataMsg()` once with the full concatenated payload
3. JS receives one devbin record per device with `recordLen = 7 + N × resultSize`
4. JS inner loop iterates through the payload, parsing one `resp.b`-sized chunk per iteration

If results were variable-sized, the JS parser wouldn't know where one sample ends and the next begins — the concatenation boundary is implicit from the fixed size.

## 3. Analysis: Where Variable Length Already Works

### 3.1. PollReadLenExpr (Dynamic I2C Read Length)

The existing `PollReadLenExpr` mechanism already supports reading variable amounts of data from I2C devices. For example:

```
$0.w12:mask0FFF*2:max96
```

This reads the first 2 bytes of a previous operation, masks the lower 12 bits, multiplies by 2, and caps at 96. The `:max96` suffix is used to size the `PollDataAggregator` buffer for the worst case.

The actual read may produce fewer bytes than the max. Currently these shorter results are **zero-padded** to the max before storage.

### 3.2. Wire Protocol

The devbin wire format has no fixed-size assumption — `recordLen` encodes the actual number of bytes. A shorter payload would transmit correctly.

### 3.3. JS Single-Sample Parsing

When a devbin record contains exactly one sample, the JS parser handles variable-length data correctly. The attribute parser advances through actual data using struct sizes from attribute definitions. The `resp.b` fallback only matters when iterating multiple samples.

## 4. Design Options

### Option A: Variable-Length Ring Buffer

**Approach**: Replace the flat fixed-stride ring buffer with a structure that supports variable-length entries. Each entry would carry a length prefix.

**Implementation**:
- Add a parallel `std::vector<uint16_t> _entryLengths` ring alongside the data ring
- Or use a more complex structure (linked list, length-prefixed entries)
- `put()` stores actual length data; `get()` returns entries with their individual sizes

**Pros**: Minimal wasted memory in the ring buffer; true variable-length support

**Cons**:
- More complex ring buffer logic (fragmentation, wrap-around with variable entries)
- Extra memory for length tracking metadata
- `get()` for multiple entries must communicate per-entry sizes to callers
- Risk of memory fragmentation on ESP32 (constrained heap)
- Changes propagate through `DeviceStatus`, `BusStatusMgr`, `getQueuedDeviceDataBinary`

### Option B: Fixed Max Buffer + Per-Entry Actual Length

**Approach**: Keep the ring buffer at max size (`_resultSize` = max possible read) but track the actual length of each stored entry. Trim to actual size only at publish time.

**Implementation**:
- Add `std::vector<uint16_t> _actualLengths` parallel ring (one `uint16_t` per slot)
- `put()` accepts data up to `_resultSize`, stores actual length; pads remainder with zeros
- New `get()` variant returns entries trimmed to their actual lengths
- When building devbin messages, each sample is written at its actual length with a per-sample length prefix

**Pros**: Minimal change to ring buffer indexing (O(1) stays O(1)); bounded memory use
```
Memory overhead: numResultsToStore × 2 bytes (for length tracking)
For 10 results: 20 bytes extra — negligible
```

**Cons**: Still wastes ring buffer space (padded to max); only saves on transmission
- Requires changes to the devbin format to communicate per-sample sizes when multiple samples are packed in one record
- JS parser needs updating for length-prefixed samples

### Option C: One Devbin Record Per Sample

**Approach**: Instead of concatenating N samples into one devbin record per device, emit one devbin record per sample. Each record already has a `recordLen` that encodes its actual size.

**Implementation**:
- Change `getQueuedDeviceDataBinary()` to call `genBinaryDataMsg()` once per drained sample instead of once per device
- Remove padding in `taskService()` (or keep it but track actual length)
- JS parser already handles multiple records per device (loops over records, not samples-within-record)

**Pros**:
- Wire format already supports this — no protocol changes needed
- JS parser already iterates by devbin records — attribute parsing uses exact data, no `resp.b` iteration needed
- Cleanest separation of concerns: each record is self-describing

**Cons**:
- Per-record header overhead: 9 bytes (2 recordLen + 1 statusBus + 4 addr + 2 devTypeIdx) per sample vs per batch
- For a device polled at 50ms with 10-result buffer: up to 9 × 10 = 90 bytes extra per publish cycle
- BLE MTU pressure (typical 244 bytes effective payload); more records mean more overhead in constrained bandwidth
- Still needs ring buffer changes to store actual lengths (or stop rejecting short data)

### Option D: Hybrid — Variable-Length Ring + Batched Per-Sample-Length Records

**Approach**: Store actual-length data in the ring buffer. When publishing, pack multiple samples into one devbin record but add a per-sample length prefix so the JS parser can iterate correctly.

**Implementation**:
- Ring buffer stores entries with actual lengths (similar to Option A or B)
- New devbin sub-format within a record: `[sampleLen(2B)][sampleData...]` repeated for each sample
- JS parser iterates by reading `sampleLen` first, then parsing `sampleLen` bytes of attribute data

**Pros**: Saves both buffer memory and wire bytes; batching preserves bandwidth efficiency

**Cons**: Requires a new devbin sub-format version (backwards compatibility); most complex change; multiple layers affected

## 5. Recommended Approach

### Option B+C Hybrid: Track Actual Lengths, One Record Per Sample

This combines the simplest parts of Options B and C:

1. **Track actual lengths in PollDataAggregator** (from Option B): Add a small parallel array to record the actual data length of each entry. Keep the ring buffer at max size for simplicity.

2. **Emit one devbin record per sample** (from Option C): When draining the aggregator, emit separate devbin records for each sample, trimmed to their actual length.

This avoids protocol changes (devbin format unchanged), avoids complex ring buffer refactoring, and works with the existing JS parser with minimal changes.

### Bandwidth Impact Assessment

For the LSM6DS3 FIFO case (the main use case for dynamic read lengths):
- FIFO read expression: `$0.w12:mask0FFF*2:max96`
- Typical FIFO data at 104 Hz polled every 50ms: ~5 samples × 12 bytes = 60 bytes; max is 96 bytes
- Savings per poll: 96 - 60 = 36 bytes payload + 9 bytes per-record overhead
- Net savings: 36 - 9 = 27 bytes per poll (~540 bytes/sec at 20 Hz publish)
- At 416 Hz polled every 50ms: ~21 samples × 12 = 252 bytes (over max 96, existing decoder caps at 8)

For these data volumes, the bandwidth savings are modest. **The bigger win is accurate data representation** — currently the trailing zeros from padding are interpreted as valid data by the JS parser, producing spurious zero-valued samples.

## 6. Detailed Change Points

### 6.1. Firmware Changes

#### PollDataAggregator.h (RaftI2C)

```
File: raftdevlibs/RaftI2C/components/RaftI2C/BusI2C/PollDataAggregator.h
```

- Add `std::vector<uint16_t> _actualLengths` alongside `_ringBuffer`
- Modify constructor: `_actualLengths.resize(numResultsToStore)`
- Modify `put()`: Accept `data.size() <= _resultSize` (not strict equality). Copy only `data.size()` bytes, zero-fill remainder, store actual length in `_actualLengths[slotIndex]`
- Add new `get()` variant or modify the multi-element `get()` to return per-entry actual lengths
- Modify `resize()`: Also resize `_actualLengths`

#### PollDataAggregatorIF.h (RaftCore)

```
File: raftdevlibs/RaftCore/components/core/Bus/PollDataAggregatorIF.h
```

- Add new virtual method: `virtual uint32_t getPerSample(std::vector<uint8_t>& data, std::vector<uint16_t>& lengths, uint32_t maxResponsesToReturn) = 0`

#### DevicePollingMgr::taskService() (RaftI2C)

```
File: raftdevlibs/RaftI2C/components/RaftI2C/BusI2C/DevicePollingMgr.cpp
```

- **Remove** the padding logic: `if (pollDataResult.size() < pollInfo.pollResultSizeIncTimestamp) pollDataResult.resize(...)`
- Pass actual-length result directly to `handlePollResult()`

#### DeviceIdentMgr::getQueuedDeviceDataBinary() (RaftI2C)

```
File: raftdevlibs/RaftI2C/components/RaftI2C/BusI2C/DeviceIdentMgr.cpp
```

- Instead of draining all results into one flat buffer and calling `genBinaryDataMsg` once:
  - Drain results one at a time (or use the new per-sample `get()` variant)
  - Call `genBinaryDataMsg()` for each sample, trimmed to its actual length

#### DeviceStatus.h / DeviceStatus.cpp (RaftCore)

```
File: raftdevlibs/RaftCore/components/core/Bus/DeviceStatus.h
```

- Add method: `uint32_t getPollResponsesPerSample(...)` that calls the new aggregator `getPerSample()`

#### BusStatusMgr (RaftI2C)

```
File: raftdevlibs/RaftI2C/components/RaftI2C/BusI2C/BusStatusMgr.cpp
```

- Add/modify `getBusElemPollResponses()` to support per-sample retrieval

### 6.2. JS Changes

#### RaftDeviceManager.ts — handleClientMsgBinary()

```
File: raftjs/src/RaftDeviceManager.ts
```

- **Minimal changes needed.** The inner `while` loop already iterates within a devbin record's poll data. If each record contains only one sample, the inner loop executes once and `processMsgAttrGroup` returns after parsing the actual data.
- The outer `while` loop already handles multiple devbin records for the same device address.
- May need to aggregate device state across multiple records for the same device in one message.

#### RaftAttributeHandler.ts — processMsgAttrGroup()

```
File: raftjs/src/RaftAttributeHandler.ts
```

- **No changes needed for standard mode** — attribute parsing already advances by actual bytes consumed.
- **Custom decoder mode**: Currently relies on `resp.b` fallback for advancement. If only one sample per record, the custom decoder processes it and the outer loop advances to the next record. No change needed.
- The `resp.b` fallback logic becomes unnecessary when each record is one sample but is harmless to keep for backwards compatibility.

### 6.3. No Changes Needed

- **genBinaryDataMsg()**: Already uses actual `deviceMsgData.size()`
- **DeviceTypeRecords JSON / code generation**: `resp.b` still needed as buffer sizing hint
- **PollReadLenExpr**: Already produces actual-length results
- **devbin wire format**: Already self-describing via `recordLen`

## 7. Implementation Sequence

1. **PollDataAggregator**: Add `_actualLengths` tracking, relax `put()` size check
2. **PollDataAggregatorIF**: Add `getPerSample()` pure virtual
3. **DeviceStatus**: Add `getPollResponsesPerSample()` delegation
4. **BusStatusMgr**: Add per-sample retrieval path
5. **DevicePollingMgr**: Remove padding in `taskService()`
6. **DeviceIdentMgr**: Change `getQueuedDeviceDataBinary()` to emit per-sample records
7. **JS (if needed)**: Verify multi-record-per-device handling works correctly
8. **Testing**: Verify with LSM6DS3 FIFO (variable-length reads) and standard fixed-size devices

## 8. Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Backwards compatibility — older JS clients expect one record per device with concatenated samples | JS parser may produce fewer samples per update | JS already handles multiple records per device in the outer loop; no multi-record assumption exists |
| BLE bandwidth overhead from per-record headers | More overhead when many samples buffered | For typical use (1-5 samples per device per publish), overhead is small (9-45 bytes). Monitor and add batching option later if needed |
| RTOS thread safety — `_actualLengths` must be protected by existing mutex | Data corruption if not synchronized | Use the existing `_accessMutex` in PollDataAggregator for all accesses |
| Legacy devices with exact-size poll results | Must continue to work unchanged | Relaxing `put()` to accept `<= _resultSize` is backwards compatible — exact-size data still works |
| Custom decoders that rely on multiple-samples-in-one-record iteration | Would get one invocation per sample instead of iterating by `resp.b` | Custom decoders process one sample per call regardless — no multi-sample iteration in the custom handler code path |

## 9. Alternative Considered and Rejected

**In-place padding trimming at publish time** — Keep everything as-is but trim trailing zeros when building devbin records. Rejected because trailing zeros may be valid data (e.g., sensor returning actual zero values). Without actual-length tracking, there's no way to distinguish padding from real data.

## 10. Estimated Complexity

- Firmware: ~150 lines changed across 5-6 files
- JS: Minimal — verification only; possibly 0 lines changed
- Testing: Moderate — need to test with both dynamic-length (FIFO) and fixed-length devices
