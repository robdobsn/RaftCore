# WebSocket Back-Pressure and File Download Reliability

## Problem

Large file downloads (>~160KB) over WebSocket from ESP32-S3 fail with `code 1006` (abnormal close). The firmware sends file blocks via the OKTO protocol, but the TCP socket becomes congested, leading to EAGAIN on `send()`, buffer overflow, and connection death.

## Root Cause

The outbound data path for WebSocket file downloads is:

| Stage | Buffer | Allocator | Notes |
|---|---|---|---|
| 1. `FileStreamBlockOwned::block` | ~5KB read from LittleFS | PSRAM | OK |
| 2. `CommsChannelMsg::_cmdVector` | ~5KB RICREST frame | PSRAM | OK |
| 3. `CommsChannel::_outboundQueue` | N × 5KB queued messages | PSRAM | OK |
| 4. `ProtocolRICSerial` HDLC encode | ~10KB intermediate | PSRAM | OK |
| 5. `RaftWebSocketLink::sendMsg` frame | ~5KB WebSocket frame | PSRAM | OK |
| 6. POSIX `send()` | TCP socket buffer | **Internal RAM (DMA)** | Bottleneck |
| 7. `_socketTxQueuedBuffer` (EAGAIN overflow) | Unsent bytes | PSRAM | OK |

Stages 1-5 and 7 all use `SpiramAwareAllocator` — no internal RAM pressure from user-land buffers.

The bottleneck is stage 6: the lwIP TCP stack requires DMA-capable internal RAM for packet buffers. When multiple 5KB WebSocket frames are in flight, the TCP send buffer fills, `send()` returns EAGAIN, and unsent data overflows to `_socketTxQueuedBuffer`.

The critical configuration issue: `_maxSendBufferBytes` (configured via `sendMax` in SysTypes.json WebServer config) defaulted to **5000 bytes** — smaller than a single WebSocket file block frame (~5011 bytes). Any EAGAIN instantly caused `WEB_CONN_SEND_FAIL` and killed the connection.

## Mitigations Applied

### 1. Increased `sendMax` to 20000

In `SysTypes.json`, the WebServer's `sendMax` was increased from 5000 to 20000. This allows `_socketTxQueuedBuffer` to absorb up to ~4 queued frames on EAGAIN without failing.

### 2. Reduced `batchAckSize` to 4 (JS side)

In `RaftFileHandler.ts`, `batchAckSizeRequested` was reduced from 10 to 4. This limits in-flight data to 20KB (4 × 5KB blocks) instead of 50KB, reducing TCP buffer pressure.

### 3. Heap-based throttle in `FileDownloadOKTOProtocol`

Before each `sendBlock()` call in `transferService()`, the firmware checks `heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)`. If below `HEAP_LOW_WATER_MARK_BYTES` (35000), the send is skipped and retried on the next loop iteration. This provides a safety net for extreme memory pressure.

### 4. `MAX_WS_SEND_RETRY_MS` kept at 0

Setting this to a non-zero value (e.g. 100ms) causes the main loop to block on EAGAIN, which stalls device polling, publishing, and other SysMods. This is unacceptable for normal operation — even small publish messages (200 bytes) can trigger the blocking retry during transient TCP congestion.

## Non-Blocking Retry Options (Future)

### Option A: Rely on `_socketTxQueuedBuffer` drain (current approach)

With `MAX_WS_SEND_RETRY_MS = 0` and `sendMax: 20000`, EAGAIN causes immediate queueing to `_socketTxQueuedBuffer`. On the next connection service cycle, `handleTxQueuedData()` attempts to drain the queue via `send()`. This is inherently non-blocking.

**Status:** Currently implemented. Sufficient for `batchAckSize: 4` with file downloads.

### Option B: Congestion back-pressure via `canAccept()` (recommended future improvement)

Add a congestion signal that propagates from the TCP socket up through the WebSocket, CommsChannel, and protocol layers:

1. **`RaftWebConnection`**: Track `_socketTxQueuedBuffer.size()`. Set a `_congested` flag when it exceeds 75% of `_maxSendBufferBytes` (15KB). Clear when it drops below 25% (5KB). Hysteresis prevents oscillation.

2. **`RaftWebSocketLink`**: Expose `isCongested()` that checks the underlying connection's congestion state.

3. **`RaftWebHandlerWS`**: Expose congestion state to the CommsChannel.

4. **`CommsChannelManager::loop()`**: After `outboundQueuePeek()` and before `addTxMsgToProtocolCodec()`, check channel congestion. If congested, skip this iteration — the message stays in `_outboundQueue` (PSRAM-backed) and is retried on the next loop (~1.5ms later).

```
CommsChannelManager::loop()
    ├── peek message from _outboundQueue (PSRAM)
    ├── channel->isCongested()? ── YES → leave in queue, try next loop
    │                               │
    │                              NO
    ├── get message
    ├── addTxMsgToProtocolCodec() → HDLC encode → sendMsg()
    │                                               │
    │                                          send() EAGAIN?
    │                                           │         │
    │                                          YES       NO
    │                                           │
    │                                    queue in _socketTxQueuedBuffer
    │                                    set congested flag
    │
    └── next loop iteration: handleTxQueuedData() drains queue
                             clear congested flag when drained
```

**Benefits:**
- True flow control — upstream queues (PSRAM-backed) absorb the backlog
- No data loss, no blocking
- File download protocol naturally slows down because blocks queue up
- Works for all message types, not just file downloads

**Effort:** ~20-30 lines across 4 files (`RaftWebConnection`, `RaftWebSocketLink`, `RaftWebHandlerWS`, `CommsChannelManager`).

### Option C: Socket writability pre-check

Before calling `send()` in `rawSendOnConn()`, use `select(fd, NULL, &writefds, NULL, &zero_timeout)` or `poll()` to check if the socket is writable. If not, skip `send()` and queue directly to `_socketTxQueuedBuffer`.

**Benefits:** Avoids EAGAIN entirely — no wasted syscalls.
**Drawback:** Adds a syscall per send. Doesn't propagate back-pressure upstream.

## Key Configuration Parameters

| Parameter | Location | Default | Current | Notes |
|---|---|---|---|---|
| `sendMax` | SysTypes.json WebServer config | 5000 | 20000 | `_maxSendBufferBytes` for EAGAIN overflow queue |
| `batchAckSize` | `RaftFileHandler.ts` | 10 | 4 | In-flight file blocks before ack required |
| `blockSize` | `RaftFileHandler.ts` | 5000 | 5000 | File block size in bytes |
| `MAX_WS_SEND_RETRY_MS` | `RaftWebSocketLink.h` | 0 | 0 | Blocking retry on EAGAIN (keep at 0) |
| `HEAP_LOW_WATER_MARK_BYTES` | `FileDownloadOKTOProtocol.h` | N/A | 35000 | Heap threshold for send throttle |
| `MIN_TIME_BETWEEN_BLOCKS_MS` | `FileDownloadOKTOProtocol.h` | 100 | 100 | Minimum interval between block sends |

## Observed Performance

- File download throughput with `batchAckSize: 4`: ~20KB/s (~100ms per block, 4 blocks per batch)
- Heap minimum during transfer: ~29KB internal (from ~55KB baseline)
- LittleFS write latency: avg 14ms, max 33ms per 4KB flush
- Transfer failures at `batchAckSize: 10` with `sendMax: 5000`: consistent at ~160KB transferred
- Transfer success at `batchAckSize: 4` with `sendMax: 20000`: reliable for 1.6MB files
