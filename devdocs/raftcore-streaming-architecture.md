# RaftCore & RaftJS Streaming Architecture

## Overview

This document describes how streaming works in the RaftCore firmware framework and the RaftJS client library, and how it is used in the SandBot project for pattern streaming. It covers the native binary streaming protocols built into Raft, documents the current implementation on both the firmware and WebUI sides, and identifies remaining improvements.

## RaftCore Streaming Protocols

RaftCore provides a complete streaming infrastructure in `components/comms/FileStreamProtocols/`. There are four protocol implementations, each inheriting from `FileStreamBase`:

| Protocol Class | Content Type | Flow Type | Flow Control | Use Case |
|---|---|---|---|---|
| `FileUploadHTTPProtocol` | FILE / FIRMWARE | HTTP_UPLOAD | None (HTTP handles it) | HTTP multipart uploads |
| `FileUploadOKTOProtocol` | FILE / FIRMWARE | RICREST_UPLOAD | OKTO batch acks | Reliable file transfer over BLE/WS |
| `FileDownloadOKTOProtocol` | FILE / FIRMWARE | RICREST_DOWNLOAD | OKTO batch acks | File downloads |
| `StreamDatagramProtocol` | RT_STREAM | any | SOKTO feedback | Real-time audio/data streaming |

### Core Data Structures

**`FileStreamBlock`** — A lightweight, non-owning descriptor for a block of streaming data:
- `pBlock` — pointer to data buffer
- `blockLen` — length of this block
- `filePos` — position within the overall stream
- `fileLen` — total stream length (if known)
- `firstBlock` / `finalBlock` — boundary flags
- `crc16` — optional integrity check
- `filename` — name/identifier for the stream

**`FileStreamBlockOwned`** — Same as above but owns its data via `SpiramAwareUint8Vector`. Used for downloads where data must outlive the original buffer.

### Callback Types

When a SysMod registers a REST API endpoint with a chunk callback, it receives streaming data through:

```cpp
// Write callback — receives each block of incoming stream data
typedef std::function<RaftRetCode(FileStreamBlock& fileBlock)> FileStreamBlockWriteFnType;

// Read callback — provides data for downloads
typedef std::function<RaftRetCode(FileStreamBlockOwned& fileBlock, uint32_t filePos, uint32_t maxLen)> FileStreamBlockReadFnType;

// CRC callback — reports file integrity
typedef std::function<RaftRetCode(uint32_t& fileCRC, uint32_t& fileLen)> FileStreamGetCRCFnType;

// Cancel/End callback — notification of stream termination
typedef std::function<void(bool isNormalEnd)> FileStreamCancelEndFnType;
```

### REST API Endpoint Registration

A REST API endpoint can register a **chunk callback** (`RestAPIFnChunk`) which receives `FileStreamBlock` data:

```cpp
typedef std::function<RaftRetCode(const String &reqStr, FileStreamBlock& fileStreamBlock, 
    const APISourceInfo& sourceInfo)> RestAPIFnChunk;
```

When a `RT_STREAM` session starts, the `FileStreamSession` looks up the REST API endpoint by name (from the `"endpoint"` field in the `ufStart` command) and wires the endpoint's `_callbackChunk` as the data sink. Each incoming binary `FILEBLOCK` frame is then delivered directly to the chunk callback.

## The Two Streaming Protocols in Detail

### 1. OKTO Protocol (FileUploadOKTOProtocol) — Reliable File Transfer

The OKTO protocol provides **guaranteed delivery** with batch acknowledgments. It is designed for transferring files where every byte matters (firmware updates, filesystem files).

**Wire Protocol:**

1. **Start**: Client sends a `COMMAND_FRAME` (elemCode 3):
   ```json
   {"cmdName":"ufStart","reqStr":"fileupload","fileType":"fs",
    "fileName":"pattern.thr","fileLen":12345,
    "batchMsgSize":500,"batchAckSize":10}
   ```
   Firmware responds with negotiated `batchMsgSize`, `batchAckSize`, and `streamID`.

2. **Data**: Client sends `FILEBLOCK` frames (elemCode 4) in binary format:
   ```
   [streamID:1byte] [filePos:3bytes BE] [payload:Nbytes]
   ```
   After each batch of `batchAckSize` blocks, the firmware sends an `"okto"` acknowledgment:
   ```json
   {"okto": 5000}
   ```
   The client waits for this before sending the next batch.

3. **End**: Client sends `COMMAND_FRAME`:
   ```json
   {"cmdName":"ufEnd","reqStr":"fileupload","fileType":"fs",
    "fileName":"pattern.thr","fileLen":12345}
   ```

**Key characteristics:**
- Block size: negotiated per-channel (default ~500 bytes, up to 5000)
- Batch size: default 40 blocks between acks (negotiated)
- Timeout: 5s for first block, 1s for subsequent, up to 5 retries
- Overall timeout: 2 hours (for slow BLE transfers)
- **Guarantees**: No data loss, ordered delivery, position validation

### 2. StreamDatagram Protocol — Real-Time Streaming

The StreamDatagram protocol is designed for **real-time data** where timeliness matters more than completeness. Used in other Raft projects for audio streaming.

**Wire Protocol:**

1. **Start**: Client sends `COMMAND_FRAME`:
   ```json
   {"cmdName":"ufStart","reqStr":"ufStart","fileType":"rtstream",
    "fileName":"stream.thr","endpoint":"streampattern","fileLen":0}
   ```
   The `"endpoint"` field specifies which REST API endpoint receives the data.
   Firmware responds with `streamID` and `maxBlockSize`.

2. **Data**: Client sends `FILEBLOCK` frames (same binary format as OKTO).
   - Firmware accepts any block at or ahead of current stream position
   - Out-of-order/duplicate packets are silently dropped
   - On `BUSY` or `POS_MISMATCH`, firmware sends `"sokto"` feedback:
     ```json
     {"streamID":1,"sokto":4500,"reason":"POS_MISMATCH"}
     ```
   - On final block, firmware sends end-of-stream `sokto`

3. **End**: Client sends `COMMAND_FRAME`:
   ```json
   {"cmdName":"ufEnd","reqStr":"ufEnd","streamID":1}
   ```

**Key characteristics:**
- No batch waiting — continuous send
- "Better never than late" — drops old data rather than stalling
- SOKTO is informational feedback, not a gate for sending
- Suitable for live/continuous data feeds

## RaftJS Client Library

RaftJS (`@robdobsn/raftjs`) provides native support for both protocols:

### RaftFileHandler — OKTO Client

Exposed via `RaftConnector.sendFile()`:

```typescript
async sendFile(
  fileName: string,
  fileContents: Uint8Array,
  progressCallback: ((sent: number, total: number, progress: number) => void) | undefined
): Promise<boolean>
```

- Handles the full ufStart → FILEBLOCK batches → okto wait → ufEnd cycle
- Default block size: 500 bytes, batch ack size: 10
- Configurable via `configureFileHandler(blockSize, batchAckSize)`
- Progress callback fires every ~20 blocks
- Cancel via `fileSendCancel()`

### RaftStreamHandler — RT_STREAM Client

#### `streamData()` — Generic Streaming (recommended)

Exposed via `RaftConnector.streamData()`:

```typescript
// RaftConnector — public API
async streamData(
  streamContents: Uint8Array,
  fileName: string,            // e.g. "pattern.thr" — sent in ufStart
  targetEndpoint: string,      // e.g. "streampattern" — firmware REST endpoint
  progressCallback?: RaftStreamDataProgressCBType,
): Promise<boolean>
```

- Sends `ufStart` with `fileType:"rtstream"` and the caller's endpoint/fileName
- Streams all blocks sequentially with SOKTO backpressure handling (50ms backoff on SOKTO)
- Sends `ufEnd` on completion, returns `true` on success
- No audio-specific rate limiting — sends as fast as the channel allows
- Progress callback reports `(bytesSent, totalBytes, fractionComplete)`

#### `streamAudio()` — Legacy Audio Streaming

Exposed via `RaftConnector.streamAudio()`:

```typescript
streamAudio(streamContents: Uint8Array, clearExisting: boolean, duration: number): void
```

- Hardcoded to `fileName:"audio.mp3"` and `endpoint:"streamaudio"`
- Rate-limited: pauses after every 15 blocks based on audio byte rate
- SOKTO messages are received and logged but (in non-legacy mode) do not block sending
- Emits `CONN_STREAMING_ISSUE` event if SOKTO arrives during active streaming
- Preserved for backward compatibility with existing audio-streaming applications

## Session Management (Firmware Side)

`FileStreamSession` manages active stream/upload sessions in the firmware:

- Maximum **3 simultaneous sessions** 
- Sessions identified by `streamID` (1–255, allocated sequentially)
- Idle timeout: **10 seconds** (reset on each block received)
- Session creation is triggered by `ufStart` COMMAND_FRAME
- Session type is determined by `fileType` field: `"fs"` → FILE, `"fw"` → FIRMWARE, `"rtstream"` → RT_STREAM

For RT_STREAM sessions, `FileStreamSession` routes data via:
```
FILEBLOCK → StreamDatagramProtocol::handleDataFrame() 
  → _fileStreamBlockWrite() 
    → FileStreamSession::writeRealTimeStreamBlock() 
      → REST endpoint's _callbackChunk(reqStr, fileStreamBlock, sourceInfo)
```

## Transport Independence

Both protocols work identically across all RaftJS-supported transports:

| Transport | Channel | Notes |
|---|---|---|
| WebSocket | `ws://host/ws` | Binary frames via RICSerial codec |
| Web Bluetooth (BLE) | GATT characteristics | Smaller MTU, slower throughput |
| Web Serial (USB) | Serial port | HDLC framing over serial |

The OKTO protocol's batch size negotiation automatically adapts to channel capacity (`inboundMsgBlockMax()` per channel).

## Current SandBot Implementation

### What Exists Today

The SandBot project already uses the **native Raft RT_STREAM protocol** for pattern streaming. Both the firmware and WebUI have working implementations.

#### Firmware (`PatternManager.cpp`)

A `streampattern` REST endpoint is registered with chunk and isReady callbacks:

```cpp
endpointManager.addEndpoint("streampattern",
    RestAPIEndpoint::ENDPOINT_CALLBACK,
    RestAPIEndpoint::ENDPOINT_POST,
    std::bind(&PatternManager::apiStreamPattern, ...),
    "Stream pattern data", "application/octet-stream", nullptr,
    RestAPIEndpoint::ENDPOINT_CACHE_NEVER, nullptr,
    nullptr,  // body callback (not used)
    std::bind(&PatternManager::apiStreamPatternChunk, ...),   // chunk callback
    std::bind(&PatternManager::apiStreamPatternIsReady, ...)  // isReady callback
);
```

The chunk callback (`apiStreamPatternChunk`) handles the full lifecycle:
- **First block**: clears state, detects format from filename extension, transitions to `LOADING`
- **Data blocks**: appends raw bytes to `_streamBuffer` via `appendStreamData()`
- **Final block**: sets `_streamFinalBlockReceived`
- **Auto-start**: once `_streamBuffer` reaches `_streamStartMinBytes` (4 KB) or the final block arrives, transitions to `READY` and calls `startPlayback()`
- **Backpressure**: returns `RAFT_BUSY` when buffer usage exceeds 90%; `apiStreamPatternIsReady()` returns `true` when usage is below 80%

Buffer management:
- `_streamBufferMaxBytes` = 64 KB (default)
- `_streamStartMinBytes` = 4 KB (playback starts before stream completes)
- `compactStreamBuffer()` erases consumed bytes when `_streamReadPos` exceeds 4 KB or half the buffer
- Playback loop (`feedPatternData()`) consumes complete lines from the buffer incrementally

#### WebUI (`ConnManager.ts`)

`ConnManager.uploadPattern()` manually implements the RT_STREAM protocol:

1. Sends `ufStart` via `msgHandler.sendRICRESTCmdFrame()` with `fileType:"rtstream"`, `endpoint:"streampattern"`
2. Gets `streamID` and `maxBlockSize` (default 475 bytes) from the response
3. Loops sending binary blocks via `msgHandler.sendStreamBlock(block, pos, streamID)`
4. SOKTO handling: reads `_soktoReceived` / `_soktoPos` from `RaftStreamHandler` via an unsafe type cast, rewinds `pos` on backpressure
5. Sends `ufEnd` on completion

Mock mode (`MockFirmware.ts`) uses a separate text-based path with 1024-byte string chunks.

### Current Limitations

1. **Manual protocol reimplementation**: `ConnManager.uploadPattern()` duplicates the RT_STREAM ufStart → blocks → ufEnd logic that already exists in `RaftStreamHandler.streamData()`. This is fragile and bypasses the handler's built-in SOKTO handling.
2. **Unsafe type cast for SOKTO**: The WebUI accesses private fields (`_soktoReceived`, `_soktoPos`) via `as unknown as { ... }` — this will break if the stream handler's internals change.
3. **No cancellation support**: The `streamData()` API and `uploadPattern()` both lack the ability to cancel a stream in progress.
4. **`playbackSpeed` parameter unused**: `uploadPattern()` accepts `playbackSpeed` but never sends it to the firmware in the `ufStart` command. The firmware would need a custom field or a separate command to set playback speed.

## Recommended Next Steps

### 1. Refactor WebUI to Use `RaftConnector.streamData()`

The `ConnManager.uploadPattern()` method should be simplified to call the raftjs `streamData()` API instead of reimplementing the RT_STREAM protocol:

```typescript
public async uploadPattern(
  file: File,
  playbackSpeed: number = 20,
  onProgress?: (percent: number) => void,
): Promise<void> {
  const content = await file.arrayBuffer();
  const fileContents = new Uint8Array(content);
  const fileName = file.name;

  // Mock mode handling (unchanged) ...

  // Real device: use the raftjs streamData() API
  const ok = await this._connector.streamData(
    fileContents,
    fileName,
    'streampattern',
    (sent, total, progress) => onProgress?.(progress * 100),
  );
  if (!ok) throw new Error(`Stream failed for ${fileName}`);
}
```

**Benefits:**
- Eliminates ~40 lines of manual protocol code
- Removes the unsafe `as unknown as { ... }` type cast to access private SOKTO fields
- SOKTO backpressure is handled cleanly inside `RaftStreamHandler`
- Future improvements to the stream handler (retries, cancellation) apply automatically

### 2. Pass Playback Speed to Firmware

The `playbackSpeed` parameter needs to be communicated to the firmware. Options:
- **Option A**: Send as a custom field in the `ufStart` command JSON (requires firmware-side parsing in `apiStreamPatternChunk` when handling the first block)
- **Option B**: Send a separate REST command before the stream: `pattern?cmd=setSpeed&speed=20`
- **Option C**: Encode as metadata in the filename: `pattern_s20.thr`

Option B is simplest since the `pattern` REST endpoint command handler already exists.

### 3. Add Stream Cancellation

Add `streamCancel()` support to the `streamData()` flow:
- In `RaftStreamHandler`: add a `_streamCancelled` flag checked in the send loop, and send `ufCancel` (commented-out code already exists in the handler)
- In `RaftConnector`: expose `streamDataCancel()` that sets the flag
- In the WebUI: wire to a "Stop" button

### 4. Consider OKTO Protocol for Reliable Transfer

The current RT_STREAM protocol is best-effort — dropped blocks cause gaps. For patterns where data integrity is critical (complex artwork), consider alternatively using `RaftConnector.sendFile()` (OKTO protocol) to upload the pattern file to the device filesystem, then issuing a `pattern?cmd=load&name=...` command to play it. This is already supported by the firmware's `apiPattern()` handler with the `load` command.

This two-mode approach gives users a choice:
- **Stream mode** (RT_STREAM via `streamData`): immediate playback, unlimited length, but best-effort delivery
- **Upload-then-play** (OKTO via `sendFile`): guaranteed delivery, but requires full upload before playback and is limited by device flash/filesystem size

## Data Flow Diagram

### Current Implementation (Raft RT_STREAM Protocol)

```
WebUI (ConnManager.uploadPattern)       Firmware (PatternManager)
  │                                       │
  ├─ ufStart {fileType:"rtstream",       ►│ ProtocolExchange creates FileStreamSession
  │   endpoint:"streampattern",           │  with StreamDatagramProtocol
  │   fileName:"p.thr", fileLen} ────────►│  Looks up "streampattern" endpoint chunk CB
  │◄──── {streamID, maxBlockSize} ───────┤
  │                                       │
  ├─ FILEBLOCK [binary data] ────────────►│ StreamDatagram.handleDataFrame()
  │                                       │   → _fileStreamBlockWrite()
  │                                       │     → writeRealTimeStreamBlock()
  │                                       │       → apiStreamPatternChunk()
  │                                       │         → appendStreamData() to _streamBuffer
  │                                       │
  │                                       │ (auto-start at 4KB or final block)
  │                                       │   → startPlayback()
  │                                       │     → PLAYING state, feedPatternData() loop
  │                                       │
  │  (if firmware returns BUSY)           │
  │◄──── {"sokto": pos} ─────────────────┤ Client backs off 10-50ms
  │                                       │
  ├─ FILEBLOCK [binary data] ────────────►│ Data feeds playback incrementally
  │         ... continuous ...            │  compactStreamBuffer() reclaims consumed bytes
  │                                       │
  ├─ ufEnd {streamID} ──────────────────►│ Stream complete
  │                                       │
```

### Proposed Simplification (WebUI uses `RaftConnector.streamData()`)

```
WebUI                   RaftJS (streamData)             Firmware
  │                        │                              │
  ├─ streamData(           │                              │
  │    contents,           │                              │
  │    "p.thr",            │                              │
  │    "streampattern",    │                              │
  │    progressCB)         │                              │
  │  ─────────────────────►├─ ufStart ───────────────────►│
  │                        │◄─── {streamID} ─────────────┤
  │                        │                              │
  │                        ├─ FILEBLOCK [binary] ────────►│
  │◄─ progressCB(sent,    │                              │
  │    total, fraction)    │  (SOKTO backpressure         │
  │                        │   handled internally)        │
  │                        │         ...                  │
  │                        ├─ ufEnd ─────────────────────►│
  │◄─ Promise<true> ──────┤                              │
  │                        │                              │
```

## Implementation Status

| Component | Status | Notes |
|---|---|---|
| Firmware `streampattern` endpoint | **Done** | Chunk callback, isReady, auto-start at 4KB, buffer compaction |
| Firmware incremental consumption | **Done** | `feedPatternData()` consumes lines; `compactStreamBuffer()` reclaims memory |
| RaftJS `streamData()` API | **Done** | Generic RT_STREAM method in `RaftStreamHandler`, exposed via `RaftConnector` |
| WebUI `uploadPattern()` | **Working but suboptimal** | Manually reimplements RT_STREAM; should use `RaftConnector.streamData()` |
| Stream cancellation | **Not started** | No cancel support in `streamData()` or WebUI |
| Playback speed parameter | **Not started** | `uploadPattern()` accepts it but never sends to firmware |

## Summary

| Aspect | Implementation |
|---|---|
| Protocol | Native Raft RT_STREAM (binary FILEBLOCK frames) |
| Encoding | Raw binary |
| Max block size | ~475 bytes (negotiated via `maxBlockSize` in ufStart response) |
| Flow control | SOKTO feedback + `isReady` callback (80/90% thresholds) |
| Delivery guarantee | Best-effort (sequential send, drops out-of-order) |
| Buffering model | Stream-and-play: auto-starts at 4KB, incremental consumption |
| Max pattern size | Unlimited (buffer compaction reclaims consumed bytes) |
| JS API | `RaftConnector.streamData()` (available); WebUI currently uses manual protocol |
| Firmware endpoint | `streampattern` — fully implemented with chunk + isReady callbacks |
| Transport support | All (WebSocket, BLE, Serial — native binary framing) |
