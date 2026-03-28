# Devbin/Devjson Sequence Counter Implementation Plan

## Background

As part of the BLE data drop investigation (see `ble-data-drop-investigation.md`), we need a way to detect whether published device data messages are being lost. Adding a single-byte sequence counter to the devbin and devjson envelopes will allow the raftjs client to detect gaps immediately, without needing to analyze the payload contents.

The devbin version does not need to be incremented since nothing has been released since the last format change.

## Design

### Sequence Counter Semantics

- **Size**: 1 byte (uint8), wraps from 255 → 0
- **Scope**: Per topic — each topicIndex maintains its own independent counter
- **Increment**: Once per publish call to `getDevicesDataBinary()` / `getDevicesDataJSON()`
- **Initial value**: 0
- **Wrap**: Silent, 255 → 0. The receiver detects gaps by checking `(prev + 1) & 0xFF === current`

### Devbin Format Change

Current envelope (2 bytes):
```
Byte 0: magic+version (0xDB)
Byte 1: topicIndex
```

New envelope (3 bytes):
```
Byte 0: magic+version (0xDB) — unchanged, no version bump
Byte 1: topicIndex           — unchanged
Byte 2: sequenceCounter      — NEW (uint8, wraps at 255)
```

All per-device records shift by 1 byte. The `devbinEnvelopeLen` constant changes from 2 to 3.

### Devjson Format Change

Current:
```json
{"_t":2,"_v":1,"0":{...}}
```

New:
```json
{"_t":2,"_v":1,"_s":42,"0":{...}}
```

The `_s` field contains the sequence counter (0–255). Existing receivers that don't know about `_s` will simply ignore it.

## Firmware Changes (RaftCore)

### 1. `DeviceManager.h` — Add sequence counter state

Add a member to track per-topic sequence counters:

```cpp
// In private members
std::unordered_map<uint16_t, uint8_t> _publishSeqCounters;
```

Alternatively, since topic indices are small integers allocated sequentially from 0, a simple fixed array is sufficient:

```cpp
static const uint32_t MAX_TOPIC_SEQ_COUNTERS = 16;
uint8_t _publishSeqCounters[MAX_TOPIC_SEQ_COUNTERS] = {0};
```

Both `getDevicesDataBinary` and `getDevicesDataJSON` must use the same counter for a given topic so that both formats can be validated independently.

However, devbin and devjson are registered as separate topics (separate `registerDataSource` calls), so they will naturally have separate topicIndex values and separate counters. This is correct — each published stream gets its own sequence.

### 2. `DeviceManager.cpp` — `getDevicesDataBinary()`

**File**: `components/core/DeviceManager/DeviceManager.cpp`

Current code (lines ~465–467):
```cpp
// Envelope header: magic+version byte (0xDB = devbin v1) followed by topic index
binaryData.push_back(0xDB);
binaryData.push_back(topicIndex <= 0xFE ? (uint8_t)topicIndex : 0xFF);
```

New code:
```cpp
// Envelope header: magic+version byte (0xDB = devbin v1), topic index, sequence counter
binaryData.push_back(0xDB);
binaryData.push_back(topicIndex <= 0xFE ? (uint8_t)topicIndex : 0xFF);
uint8_t seqNum = (topicIndex < MAX_TOPIC_SEQ_COUNTERS) ? _publishSeqCounters[topicIndex]++ : 0;
binaryData.push_back(seqNum);
```

Note: `getDevicesDataBinary` is currently `const`. Adding `_publishSeqCounters[topicIndex]++` means the method can no longer be `const`. The signature needs to change to non-const:

```cpp
// DeviceManager.h
std::vector<uint8_t> getDevicesDataBinary(uint16_t topicIndex);  // was const

// DeviceManager.cpp
std::vector<uint8_t> DeviceManager::getDevicesDataBinary(uint16_t topicIndex)
```

### 3. `DeviceManager.cpp` — `getDevicesDataJSON()`

**File**: `components/core/DeviceManager/DeviceManager.cpp`

Current code (lines ~394–397):
```cpp
jsonStr += "{\"_t\":";
jsonStr += String(topicIndex);
jsonStr += ",\"_v\":1";
```

New code:
```cpp
jsonStr += "{\"_t\":";
jsonStr += String(topicIndex);
jsonStr += ",\"_v\":1";
uint8_t seqNum = (topicIndex < MAX_TOPIC_SEQ_COUNTERS) ? _publishSeqCounters[topicIndex]++ : 0;
jsonStr += ",\"_s\":";
jsonStr += String(seqNum);
```

Same const removal applies as for the binary variant.

## raftjs Changes

### 4. `src/RaftDeviceManager.ts` — Parse sequence counter from devbin

**File**: `src/RaftDeviceManager.ts`

Update the envelope parsing constants:
```typescript
// Change:
const devbinEnvelopeLen = 2;
// To:
const devbinEnvelopeLen = 3;
```

After extracting `topicIndex`, extract the sequence counter:
```typescript
const seqCounter = rxMsg[msgTypeLen + 2];
```

Add gap detection (log warning if sequence is not contiguous):
```typescript
// Track last seen sequence per topic
if (this._lastDevbinSeqByTopic === undefined) {
    this._lastDevbinSeqByTopic = new Map<number, number>();
}
const lastSeq = this._lastDevbinSeqByTopic.get(topicIndex);
if (lastSeq !== undefined) {
    const expectedSeq = (lastSeq + 1) & 0xFF;
    if (seqCounter !== expectedSeq) {
        const gap = (seqCounter - lastSeq + 256) & 0xFF;
        RaftLog.warn(`devbin seq gap: topic=${topicIndex} expected=${expectedSeq} got=${seqCounter} (${gap - 1} messages lost)`);
    }
}
this._lastDevbinSeqByTopic.set(topicIndex, seqCounter);
```

Add the tracking map as a class member:
```typescript
private _lastDevbinSeqByTopic: Map<number, number> = new Map();
```

### 5. `src/RaftPublish.ts` — Update frame inspection

**File**: `src/RaftPublish.ts`

If `inspectPublishFrame` returns envelope metadata, add the sequence counter to the returned `RaftPublishFrameMeta`:
```typescript
interface RaftPublishFrameMeta {
    // ... existing fields ...
    seqCounter?: number;
}
```

### 6. `src/RaftDeviceManager.ts` — Parse sequence counter from devjson

When processing JSON publish messages, extract `_s` if present:
```typescript
const seqCounter = jsonMsg._s;
```

Apply the same gap detection logic as for devbin, using a separate tracking map or a shared one keyed by `(format, topicIndex)`.

## File Summary

| File | Repo | Change |
|------|------|--------|
| `components/core/DeviceManager/DeviceManager.h` | RaftCore | Add `_publishSeqCounters` array, remove `const` from two methods |
| `components/core/DeviceManager/DeviceManager.cpp` | RaftCore | Add seq byte to devbin envelope, add `_s` to devjson, increment counters |
| `src/RaftDeviceManager.ts` | raftjs | Parse seq from devbin (update envelope len), add gap detection logging |
| `src/RaftPublish.ts` | raftjs | Add `seqCounter` to `RaftPublishFrameMeta` |

## Backward Compatibility

- **devbin**: Existing raftjs code that expects a 2-byte envelope will misparse the first byte of the first device record as part of the envelope. Since we are changing both sides simultaneously and nothing has been released, this is acceptable. The version byte remains `0xDB` per the requirement.
- **devjson**: Fully backward compatible — `_s` is an additional JSON field that old receivers will ignore.

## Testing

1. Build firmware, connect over WiFi and BLE.
2. Verify raftjs logs show sequential counters with no gaps over WiFi.
3. Verify raftjs logs show gap warnings over BLE, correlating with the sawtooth discontinuities from the emulator.
4. Use the sawtooth validation script to cross-check: every gap in the sawtooth should correspond to a logged sequence gap, and vice versa.
