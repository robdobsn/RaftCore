# Devbin Variable-Length Samples with Per-Device Sequence Counter

## Overview

Change devbin per-device records from a flat concatenation of fixed-size poll results to a sequence of length-prefixed samples. A per-device 1-byte wrapping sequence counter is added for drop detection. The version stays at `0xDB` — nothing has been released so no backward compatibility is needed.

This enables dynamic-length I2C reads (e.g. FIFO drains) to be transmitted at their actual size rather than padded to the maximum, saving BLE bandwidth.

## Wire Format

### Envelope (3 bytes)

```
Byte 0:   0xDB              magic+version (unchanged)
Byte 1:   topicIndex        uint8, 0x00–0xFE; 0xFF = no topic
Byte 2:   envelopeSeqNum    uint8, wrapping (per devbin-sequence-counter-plan.md)
```

The envelope sequence counter (byte 2) is per the existing plan in `devbin-sequence-counter-plan.md`. It detects whole-frame drops.

### Per-Device Record

```
Bytes 0–1:  recordLen       uint16 BE — body bytes that follow (min 8)
Byte  2:    statusBus       bit7=online, bit6=pendingDeletion, bits3:0=busNumber
Bytes 3–6:  address         uint32 BE
Bytes 7–8:  devTypeIdx      uint16 BE
Byte  9:    deviceSeqNum    uint8, wrapping — NEW, per-device counter
Bytes 10+:  samples         length-prefixed sample data (see below)
```

`recordLen` minimum is 8. The `recordHeaderLen` constant becomes 8 (was 7 — the extra byte is `deviceSeqNum`).

### Sample Packing Within a Record

Poll results for the device are concatenated as length-prefixed samples:

```
[sampleLen(1B)][sampleData(sampleLen bytes)]  × N
```

- `sampleLen`: uint8, 1–255. Max I2C read per poll is capped at 255 bytes (including the 2-byte timestamp).
- N is determined implicitly: the parser reads samples until it has consumed `recordLen - recordHeaderLen` bytes.
- For fixed-size devices, every sample has the same `sampleLen`. The 1-byte overhead per sample is accepted.
- For variable-size devices (FIFO reads), each sample carries its actual read length — no padding.

### Example: LSM6DS3 at 104 Hz, 50ms Poll

Previously: Each poll reads ~64 bytes (4 status + ~60 FIFO data), padded to 100 bytes. With `"s": 20`, a publish burst of 10 buffered polls = 10 × 100 = 1000 bytes payload, plus one 9-byte record header = 1009 bytes.

With this change: Same 10 polls at actual sizes. Payload = 10 × (1 + 64) = 650 bytes, plus one 10-byte record header = 660 bytes. **Saves 349 bytes (35%).**

### Example: LSM6DS3 at 416 Hz with `:max252`

Per poll: ~256 bytes actual. Without padding savings (reads near max), the overhead is 10 × 1 = 10 bytes (length prefixes) + 1 byte (deviceSeqNum) = 11 bytes extra. Negligible.

## Firmware Changes

### 1. PollDataAggregator.h (RaftI2C)

**File**: `raftdevlibs/RaftI2C/components/RaftI2C/BusI2C/PollDataAggregator.h`

Add actual-length tracking. Ring buffer stays fixed-stride.

**Private members** — add:
```cpp
std::vector<uint16_t> _actualLengths;  // one per slot
```

**Constructor** — add:
```cpp
_actualLengths.resize(numResultsToStore, 0);
```

**`put()`** — relax size check and record actual length:
```cpp
// Change: data.size() != _resultSize  →  data.size() > _resultSize || data.size() == 0
if (data.size() > _resultSize || data.size() == 0)
    return false;

// Store actual length
uint16_t slotIndex = _ringBufHeadOffset / _resultSize;
_actualLengths[slotIndex] = data.size();

// Copy actual data, zero-fill remainder
memcpy(_ringBuffer.data() + _ringBufHeadOffset, data.data(), data.size());
if (data.size() < _resultSize)
    memset(_ringBuffer.data() + _ringBufHeadOffset + data.size(), 0, _resultSize - data.size());
```

**`resize()`** — also resize `_actualLengths`:
```cpp
_actualLengths.resize(numResultsToStore, 0);
```

**`_latestValue`** — store at actual length (no change needed, already uses `_latestValue = data`).

### 2. PollDataAggregatorIF.h (RaftCore)

**File**: `raftdevlibs/RaftCore/components/core/Bus/PollDataAggregatorIF.h`

Add new virtual method for per-sample retrieval with actual lengths:

```cpp
/// @brief Get poll results with per-sample actual lengths
/// @param data (output) Concatenated sample data (trimmed to actual lengths)
/// @param lengths (output) Actual length of each sample
/// @param maxResponsesToReturn Maximum number (0 = all available)
/// @return number of samples returned
virtual uint32_t getWithLengths(std::vector<uint8_t>& data,
                                std::vector<uint16_t>& lengths,
                                uint32_t maxResponsesToReturn) = 0;
```

### 3. PollDataAggregator.h — implement `getWithLengths()`

Drains the ring buffer like the existing multi-element `get()`, but trims each entry to its actual length:

```cpp
uint32_t getWithLengths(std::vector<uint8_t>& data,
                        std::vector<uint16_t>& lengths,
                        uint32_t maxResponsesToReturn) override
{
    data.clear();
    lengths.clear();

    if (!RaftMutex_lock(_accessMutex, RAFT_MUTEX_WAIT_FOREVER))
        return 0;

    uint32_t numToReturn = (maxResponsesToReturn == 0 || _ringBufCount < maxResponsesToReturn)
                           ? _ringBufCount : maxResponsesToReturn;
    if (numToReturn == 0) {
        RaftMutex_unlock(_accessMutex);
        return 0;
    }

    // Get tail position
    uint32_t pos = (_ringBufHeadOffset + _ringBuffer.size() - _ringBufCount * _resultSize)
                   % _ringBuffer.size();

    for (uint32_t i = 0; i < numToReturn; i++) {
        uint16_t slotIndex = pos / _resultSize;
        uint16_t actualLen = _actualLengths[slotIndex];
        if (actualLen == 0) actualLen = _resultSize;  // backwards compat: 0 means legacy full-size

        data.insert(data.end(),
                    _ringBuffer.data() + pos,
                    _ringBuffer.data() + pos + actualLen);
        lengths.push_back(actualLen);

        pos += _resultSize;
        if (pos >= _ringBuffer.size())
            pos = 0;
    }

    _ringBufCount -= numToReturn;
    RaftMutex_unlock(_accessMutex);
    return numToReturn;
}
```

### 4. DevicePollingMgr.cpp — Remove Padding (RaftI2C)

**File**: `raftdevlibs/RaftI2C/components/RaftI2C/BusI2C/DevicePollingMgr.cpp` (line ~168)

Remove or guard the padding:
```cpp
// REMOVE:
// if (pollDataResult.size() < pollInfo.pollResultSizeIncTimestamp)
//     pollDataResult.resize(pollInfo.pollResultSizeIncTimestamp, 0);
```

The aggregator's relaxed `put()` now accepts shorter data.

### 5. DeviceStatus.h (RaftCore) — Add Per-Sample Retrieval

**File**: `raftdevlibs/RaftCore/components/core/Bus/DeviceStatus.h`

Add method delegating to the new aggregator API:

```cpp
uint32_t getPollResponsesWithLengths(std::vector<uint8_t>& data,
                                     std::vector<uint16_t>& lengths,
                                     uint32_t maxResponsesToReturn) const
{
    if (pDataAggregator)
        return pDataAggregator->getWithLengths(data, lengths, maxResponsesToReturn);
    return 0;
}
```

### 6. BusStatusMgr.cpp — Add Per-Sample Retrieval (RaftI2C)

**File**: `raftdevlibs/RaftI2C/components/RaftI2C/BusI2C/BusStatusMgr.h` / `.cpp`

Add new method alongside existing `getBusElemPollResponses`:

```cpp
// Declaration:
uint32_t getBusElemPollResponsesWithLengths(BusElemAddrType address,
    DeviceOnlineState& onlineState, uint16_t& deviceTypeIndex,
    std::vector<uint8_t>& devicePollResponseData,
    std::vector<uint16_t>& sampleLengths,
    uint32_t maxResponsesToReturn);

// Implementation: same pattern as getBusElemPollResponses but calls
// pAddrStatus->deviceStatus.getPollResponsesWithLengths(...)
```

### 7. Per-Device Sequence Counter Storage

The `deviceSeqNum` is per-device (per bus address). Store in `DeviceStatus`:

**File**: `raftdevlibs/RaftCore/components/core/Bus/DeviceStatus.h`

Add member:
```cpp
uint8_t publishSeqCounter = 0;
```

Increment and return in a helper:
```cpp
uint8_t getAndIncrementSeqCounter() { return publishSeqCounter++; }
```

This is accessed inside `BusStatusMgr::getBusElemPollResponsesWithLengths()` while holding the mutex, so it's thread-safe.

### 8. DeviceIdentMgr::getQueuedDeviceDataBinary() (RaftI2C)

**File**: `raftdevlibs/RaftI2C/components/RaftI2C/BusI2C/DeviceIdentMgr.cpp` (line ~414)

Change to use the new per-sample API and build v2 records:

```cpp
std::vector<uint8_t> DeviceIdentMgr::getQueuedDeviceDataBinary(uint32_t connMode)
{
    std::vector<uint8_t> binData;
    std::vector<BusElemAddrType> addresses;
    _busStatusMgr.getBusElemAddresses(addresses, false);

    for (auto address : addresses)
    {
        DeviceOnlineState onlineState = DeviceOnlineState::OFFLINE;
        uint16_t deviceTypeIndex = 0;
        std::vector<uint8_t> sampleData;
        std::vector<uint16_t> sampleLengths;
        uint8_t seqNum = 0;

        uint32_t numSamples = _busStatusMgr.getBusElemPollResponsesWithLengths(
            address, onlineState, deviceTypeIndex,
            sampleData, sampleLengths, 0);

        if (deviceTypeIndex == DEVICE_TYPE_INDEX_INVALID || numSamples == 0)
            continue;

        // Get per-device sequence counter (via busStatusMgr)
        seqNum = _busStatusMgr.getAndIncrementDeviceSeqCounter(address);

        // Build length-prefixed sample payload
        std::vector<uint8_t> payload;
        uint32_t offset = 0;
        for (uint32_t i = 0; i < numSamples; i++) {
            uint16_t len = sampleLengths[i];
            if (len > 255) len = 255;  // cap for 1-byte prefix
            payload.push_back(static_cast<uint8_t>(len));
            payload.insert(payload.end(),
                           sampleData.data() + offset,
                           sampleData.data() + offset + len);
            offset += len;
        }

        // Build record with seqNum and length-prefixed samples
        RaftDevice::genBinaryDataMsg(binData, connMode, address,
                                      deviceTypeIndex, onlineState,
                                      seqNum, payload);
    }

    // Deletion notices (seqNum 0, no samples)
    std::vector<BusStatusMgr::DeletionNotice> deletions;
    _busStatusMgr.getPendingDeletions(deletions);
    for (const auto& deletion : deletions)
    {
        std::vector<uint8_t> emptyData;
        RaftDevice::genBinaryDataMsg(binData, connMode, deletion.address,
                        deletion.deviceTypeIndex,
                        DeviceOnlineState::PENDING_DELETION, 0, emptyData);
    }
    return binData;
}
```

### 9. RaftDevice::genBinaryDataMsg() (RaftCore)

**File**: `raftdevlibs/RaftCore/components/core/RaftDevice/RaftDevice.cpp`

Modify the existing static method to add the `deviceSeqNum` parameter:

```cpp
bool RaftDevice::genBinaryDataMsg(std::vector<uint8_t>& binData,
    uint8_t busNumber, BusElemAddrType address,
    uint16_t deviceTypeIndex, DeviceOnlineState onlineState,
    uint8_t deviceSeqNum, std::vector<uint8_t> payload)
{
    // recordLen = 8 (header) + payload
    uint32_t msgLen = payload.size() + 8;
    binData.reserve(binData.size() + 2 + msgLen);

    // recordLen (2B BE)
    binData.push_back((msgLen >> 8) & 0xff);
    binData.push_back(msgLen & 0xff);

    // statusBus
    bool isOnline = (onlineState == DeviceOnlineState::ONLINE);
    bool isPendingDeletion = (onlineState == DeviceOnlineState::PENDING_DELETION);
    binData.push_back((busNumber & 0x0F) | (isOnline ? 0x80 : 0) | (isPendingDeletion ? 0x40 : 0));

    // Address (4B BE)
    binData.push_back((address >> 24) & 0xff);
    binData.push_back((address >> 16) & 0xff);
    binData.push_back((address >> 8) & 0xff);
    binData.push_back(address & 0xff);

    // devTypeIdx (2B BE)
    binData.push_back((deviceTypeIndex >> 8) & 0xff);
    binData.push_back(deviceTypeIndex & 0xff);

    // deviceSeqNum (1B) — NEW
    binData.push_back(deviceSeqNum);

    // Length-prefixed samples
    binData.insert(binData.end(), payload.begin(), payload.end());
    return true;
}
```

All callers of `genBinaryDataMsg` must be updated to pass the new `deviceSeqNum` parameter.

### 10. DeviceManager.cpp — Envelope Sequence Counter (RaftCore)

**File**: `raftdevlibs/RaftCore/components/core/DeviceManager/DeviceManager.cpp` (line ~465)

Version byte stays `0xDB`. Add the envelope sequence counter as a 3rd byte per `devbin-sequence-counter-plan.md`.

Remove `const` from `getDevicesDataBinary` signature (needed for sequence counter increment).

### 11. BLEBusDeviceManager (RaftSysMods)

**File**: `raftdevlibs/RaftSysMods/components/BLEManager/BLEBusDeviceManager.cpp`

This class also implements `getQueuedDeviceDataBinary()`. It needs the same record format changes. Review its implementation — it may be constructing records differently for BLE-connected devices.

## JS Changes

### 12. RaftDeviceManager.ts — handleClientMsgBinary()

**File**: `raftjs/src/RaftDeviceManager.ts` (line ~244)

Update constants and parsing. No backward compatibility with the old format is needed since nothing has been released.

```typescript
const devbinEnvelopeLen = 3;  // was 2: magic(1) + topic(1) + envelopeSeq(1)
const recordHeaderLen = 8;    // was 7: busInfo(1) + addr(4) + devTypeIdx(2) + deviceSeq(1)
```

After parsing the envelope, read the envelope sequence counter:

```typescript
const envelopeSeq = rxMsg[msgTypeLen + 2];
// Track for gap detection (TODO: compare with previous)
msgPos += devbinEnvelopeLen;  // skip full 3-byte envelope
```

Parse per-device records with the new format:

```typescript
// After extracting busInfo, address, devTypeIdx...
const deviceSeqNum = rxMsg[recordPos];
recordPos += 1;

// Parse length-prefixed samples
const samplesEndPos = msgPos + recordLenLen + recordLen;

while (recordPos < samplesEndPos) {
    const sampleLen = rxMsg[recordPos];
    recordPos += 1;
    if (sampleLen === 0 || recordPos + sampleLen > samplesEndPos) break;

    // Process this single sample through attribute handler
    const newMsgBufIdx = this._attributeHandler.processMsgAttrGroup(
        rxMsg, recordPos, deviceState.deviceTimeline,
        pollRespMetadata, deviceState.deviceAttributes,
        this._maxDatapointsToStore);
    recordPos += sampleLen;
}
```

The existing v1 parsing code (fixed-size iteration by `resp.b`) is removed entirely.

### 13. RaftAttributeHandler.ts — No Changes Required

The `processMsgAttrGroup()` method already:
- Extracts the 2-byte timestamp and advances
- Parses attributes from the buffer at the current position
- Returns the new buffer index after consumed bytes
- Custom decoders derive sample count from the data itself

Each call processes exactly one poll result (one `sampleLen` chunk). The `resp.b` fallback for advancement is still harmless — the caller advances by `sampleLen` regardless.

## Implementation Sequence

1. `PollDataAggregatorIF.h` — add `getWithLengths()` pure virtual
2. `PollDataAggregator.h` — add `_actualLengths`, relax `put()`, implement `getWithLengths()`
3. `DevicePollingMgr.cpp` — remove padding
4. `DeviceStatus.h` — add `publishSeqCounter`, `getPollResponsesWithLengths()`
5. `BusStatusMgr.h/.cpp` — add `getBusElemPollResponsesWithLengths()`, `getAndIncrementDeviceSeqCounter()`
6. `RaftDevice.h/.cpp` — modify `genBinaryDataMsg()` to add `deviceSeqNum` parameter
7. `DeviceIdentMgr.cpp` — rewrite `getQueuedDeviceDataBinary()` for new format
8. `DeviceManager.h/.cpp` — envelope seqNum (3rd byte) + remove `const`
9. `BLEBusDeviceManager.cpp` — update for new record format
10. `RaftDeviceManager.ts` — parse length-prefixed samples and sequence counters
11. Build and test firmware
12. Build and test JS

## Backwards Compatibility

None needed. Nothing has been released with the current format. The version byte stays `0xDB`. Both firmware and JS are updated together as a single change.

## Risks

| Risk | Mitigation |
|------|------------|
| `getWithLengths()` must be thread-safe | Uses existing `_accessMutex` |
| `_actualLengths` uninitialized for legacy data | Default 0 treated as "use `_resultSize`" |
| `sampleLen` of 0 in stream | Parser treats 0 as end-of-samples (no data) — add guard |
| `BLEBusDeviceManager` has separate build path | Must be updated in parallel |
| Envelope seqNum plan overlap | This plan incorporates it — the existing `devbin-sequence-counter-plan.md` becomes a subset of this plan |
| All `genBinaryDataMsg` callers need updating | Search for all call sites including static devices (`getStatusBinary`) |
