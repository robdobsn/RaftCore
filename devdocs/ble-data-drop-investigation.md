# BLE Data Drop Investigation

## Date: 2026-03-27 (updated 2026-03-28)

## Background

An RP2350-based LSM6DS3 I2C emulator generates a deterministic sawtooth waveform on Accel_X (-0.5g to +0.5g in steps of 0.03125g, 33-sample cycle at 104Hz). This provides a way to verify end-to-end data integrity from sensor through firmware through raftjs to the example/dashboard web app.

- **WiFi**: 10,000 samples, zero errors. Perfect sawtooth.
- **BLE**: Intermittent drops. Non-deterministic — some runs show 30+ errors, others show 0.

The emulator and validation tooling live in `RP2350LSM6DS3I2CEmulator/`. Validation is done with `scripts/validatesawtooth.py`.

## Evidence

### Drop Pattern (When Drops Occur)

When drops do occur, they show a pattern — approximately every **109 samples** (1.05 seconds at 104Hz):

```
Idx    Gap   Dropped  TimeDelta(s)
108    108      17       0.164
217    109      28       0.273
326    109      16       0.167
705    379      28       0.275
813    108      22       0.210
1032   219      21       0.208
1249   217      16       0.480
1358   109       6       0.065
1578   220       5       0.064
...
```

- Gaps between errors are almost always ~109 or multiples thereof (~219, ~329)
- Each drop loses **5–32 sawtooth steps** (variable)
- The pattern is periodic when present, but drops are **non-deterministic** across runs

### Data Files

| File | Transport | scanBTHome | Samples | Errors |
|------|-----------|------------|---------|--------|
| `dump-from-example-dash-WiFi-260327-182700.csv` | WiFi | 1 | 10,000 | 0 |
| `dump-from-example-dash-BLE-260327-183300.csv` | BLE | 1 | 10,000 | 30+ |
| `dump-dash-260327-191100-noBLEHOME.csv` | BLE | 0 | 10,000 | 0 |
| `dump-dash-260328-BLEHOME.csv` | BLE | 1 | 10,000 | 8 |
| `dump-dash-260328-noBLEHOME.csv` | BLE | 0 | 10,000 | 0 |
| `dump-dash-260328-093000-BLEHOME.csv` | BLE | 1 | 8,763 | 0 |
| `dump-dash-260328-105500-OBQUEUE.csv` | BLE | 1 | 65,433 | 0 | Queue fix applied (`outQResvNonPub: 10`) |

## Data Path

```
LSM6DS3 FIFO → I2C → Firmware DeviceManager → StatePublisher
  → [isReadyToSend gate] → BLE outbound queue → BLE stack → raftjs HDLC decode
  → RaftDeviceManager → RaftAttributeHandler → Dashboard
```

## Firmware BLE Configuration

Defaults from `BLEConfig.h`:

| Parameter | SysTypes Key | Default | Effect |
|-----------|-------------|---------|--------|
| `outQSize` | `outQSize` | 30 | Outbound queue holds 30 messages max |
| `minMsBetweenSends` | `minMsBetweenSends` | 50ms | Throttle between sends (notify mode only) |
| `outMsgsInFlightMax` | `outMsgsInFlightMax` | 10 | Max concurrent in-flight indications (NOT USED — hardcoded to 1) |
| `outMsgsInFlightTimeoutMs` | `outMsgsInFlightMs` | 500ms | Timeout waiting for indication ACK |
| `sendUsingIndication` | `sendUseInd` | true | Use indications (ACK-required) vs notifications (fire-and-forget) |

### SysTypes.json BLEMan Section (Axiom009)

```json
"BLEMan": {
    "enable": 1,
    "peripheral": true,
    "advIntervalMs": 100,
    "connIntvPrefMs": 15,
    "scanBTHome": 0,
    "nimLogLev": "E"
}
```

Note: `central: false` (default), so `scanBTHome` has no runtime effect — see "scanBTHome Hypothesis" below.

### Publish Rate

The dashboard subscribes at **10Hz** (`subscribeRateHz = 10` in `SystemTypeMarty.ts`). At 104Hz sensor rate, each publish message batches ~5 IMU samples. The 50ms send throttle allows 20 msgs/sec, which should accommodate 10Hz.

## WiFi vs BLE: Why WiFi Works

| Aspect | BLE | WiFi (WebSocket/TCP) |
|--------|-----|----------------------|
| Queue | 30 messages, manual | OS TCP window (65KB+) |
| Throttle | Indication ACK gating | No artificial limit |
| In-flight limit | 1 message (hardcoded) | TCP window manages this |
| Timeout | 500ms per message | TCP retransmit (30s+) |
| Retry | _isPending retry (next loop) | TCP auto-retransmit |
| Radio contention | Shared 2.4GHz | N/A |

## Investigation Findings

### scanBTHome Hypothesis — RULED OUT

Initial testing suggested `scanBTHome: 1` caused the drops. However, thorough code analysis proved this is **not the cause**:

- `scanBTHome` only affects `gapEventDiscovery()` — the handler for scan results
- Scanning is started by `startScanning()`, called from `onSync()`
- `startScanning()` is **gated by `enCentral`**, which defaults to `false` and is not overridden in SysTypes.json
- With `central: false`, scanning never starts regardless of `scanBTHome`
- Confirmed by subsequent testing: `scanBTHome: 1` produced 0 errors on a later run

**Conclusion**: Drops are **non-deterministic/environmental** (RF interference, BLE scheduling jitter, etc.). The initial correlation was coincidental.

### Indication vs Notification Send Mechanism

The firmware defaults to **indications** (ACK-required BLE sends). This is the primary mechanism under investigation.

**Chain**: `BLEConfig.sendUsingIndication` (default `true`, key `sendUseInd`) → `BLEGattServer._sendUsingIndication` → `sendToCentral()` calls `ble_gatts_indicate_custom()`.

The response characteristic advertises both `BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_INDICATE` flags. The raftjs Web Bluetooth client calls `startNotifications()`, which subscribes for both notification and indication values — the browser transparently ACKs indications at the ATT layer.

Note: `handleSubscription()` in `BLEGattServer.cpp` only checks `cur_notify` to set `_responseNotifyState`. This works with Web Bluetooth (which sets the notify CCCD bit), but a native client subscribing for indications only would fail to send.

### Root Cause: Indication Flow Control Discards PUBLISH Data

The actual mechanism causing drops has been traced through the full publish path:

1. **StatePublisher::loop()** calls `attemptPublish()` at the configured rate (10Hz)
2. **attemptPublish()** calls `outboundCanAccept()` → `isReadyToSend()`
3. **BLEGattOutbound::isReadyToSend()** for PUBLISH messages requires:
   - No indication in flight (`_outboundMsgsInFlight == 0`)
   - Outbound queue empty (`_outboundQueue.count() == 0`)
4. If either condition fails, **the publish is rejected at the gate** — data never enters the queue
5. `_isPending` is set to `true`, causing retry on the next loop iteration
6. On retry, data is **regenerated** (not buffered), so the current reading is sent — but readings from the blocked period are lost

**Key insight**: PUBLISH messages are treated more strictly than all other message types. Other messages just need the queue to not be full (queue < 30). PUBLISH requires **completely empty queue AND zero in-flight**. This means a single delayed indication ACK blocks all publishes, even though the 30-message queue has capacity.

The 500ms indication timeout makes this worse: if an ACK is genuinely lost, the system stalls for 500ms before resetting — during which ~5 publishes at 10Hz are discarded.

## Improvement Proposals (Keeping Indications)

### 1. Allow PUBLISH to use the existing queue — IMPLEMENTED & VERIFIED

Previously PUBLISH bypassed the queue entirely via a strict `isReadyToSend()` gate that required zero in-flight indications AND an empty queue. This has been changed to allow PUBLISH into the queue with a configurable reserve for non-publish messages:

```cpp
// Old (overly strict for PUBLISH):
if (msgType == MSG_TYPE_PUBLISH)
    return (!_sendUsingIndication || (_outboundMsgsInFlight == 0)) && (_outboundQueue.count() == 0);

// New (use queue with reserved slots for non-publish):
if (msgType == MSG_TYPE_PUBLISH)
    return _outboundQueue.count() + _outQReserveForNonPublish < _outboundQueue.maxLen();
```

New SysTypes.json parameter `outQResvNonPub` (default 10) controls how many queue slots are reserved for non-publish messages (command responses, etc). With default `outQSize: 30`, PUBLISH can use up to 20 slots.

**Files changed**: `BLEConfig.h`, `BLEGattOutbound.h`, `BLEGattOutbound.cpp`
**Wiki updated**: `RaftBLEManagerSettings.md`

**Test result**: 65,433 samples over BLE with zero errors (`dump-dash-260328-105500-OBQUEUE.csv`).

### 2. Use `outMsgsInFlightMax` properly

`_outMsgsInFlightMax` is configurable (default 10) but **never actually used** in the send logic. The code checks `_outboundMsgsInFlight > 0` (hardcoded to allow only 1 in flight). NimBLE can handle a small pipeline of indications. Using the configurable max (even 2–3) would allow overlapping sends:

```cpp
// Current (hardcoded to 1):
if (_outboundMsgsInFlight > 0)

// Proposed (use the configurable max):
if (_outboundMsgsInFlight >= _outMsgsInFlightMax)
```

### 3. Reduce indication timeout

500ms is very conservative. With a 15ms connection interval, an ACK should arrive within 50–100ms even with a missed connection event. Reducing `outMsgsInFlightMs` to ~100ms via SysTypes.json would unblock sending sooner after a genuine timeout.

### 4. Consider a "latest value" single-slot buffer for PUBLISH

Rather than queuing every publish (which could send stale data in a burst), keep a single-slot buffer per message type. When the pipe is blocked, overwrite this buffer with the newest data. When the pipe opens, send the most recent value. This ensures no stale data is sent AND the latest reading is never lost.

### Alternative: Switch to notifications

Setting `sendUseInd: 0` in SysTypes.json switches to notifications (fire-and-forget). This eliminates the ACK-gated flow control entirely. Link-layer retransmits still provide radio-level reliability. For streaming sensor data at 10Hz where the next sample arrives 100ms later, the BLE-level delivery guarantee of indications is unnecessary overhead.

## Debugging Strategy

### Level 1 — Confirm Where Drops Happen

1. **Add sequence counter to devbin** — See `devbin-sequence-counter-plan.md`. Monotonically increasing counter in firmware publish, checked in raftjs.

2. **Enable `WARN_ON_OUTBOUND_MSG_TIMEOUT`** — Uncomment in `BLEGattOutbound.cpp` to log when indication ACK timeouts occur.

3. **Enable `DEBUG_SEND_FROM_OUTBOUND_QUEUE`** — Logs in-flight count, queue depth, and send results per message.

4. **Log BLE notifications in raftjs** — In `_onMsgRx` of `RaftChannelBLE.web.ts`, log timestamp and byte count. Check for gaps correlating with drops.

### Level 2 — Test Configuration Changes

5. **Reduce indication timeout** — Set `"outMsgsInFlightMs": 100` in SysTypes.json.

6. **Switch to notifications** — Set `"sendUseInd": 0` in SysTypes.json.

7. **Increase queue size** — Set `"outQSize": 60` (only useful if proposal #1 is implemented).

### Level 3 — Code Changes

8. **Fix PUBLISH gating** — Allow PUBLISH to use the outbound queue (proposal #1 above).

9. **Use configurable in-flight max** — Use `_outMsgsInFlightMax` instead of hardcoded 1 (proposal #2 above).

10. **Add devbin sequence counter** — Per `devbin-sequence-counter-plan.md`.

## Status

- [x] Initial analysis of drop pattern (~109 samples, ~1.05s periodicity)
- [x] Proved WiFi has zero drops
- [x] Investigated scanBTHome — **ruled out** (no runtime effect with `central: false`)
- [x] Proved drops are non-deterministic across runs
- [x] Traced full publish path: StatePublisher → isReadyToSend → BLEGattOutbound
- [x] Identified root cause: strict PUBLISH gating in `isReadyToSend()` discards data when indication in flight
- [x] Identified unused `outMsgsInFlightMax` parameter
- [x] Analysed indication vs notification mechanism
- [x] Implement proposal #1 (allow PUBLISH to use queue with reserved slots) — **65,433 samples, 0 errors**
- [ ] Implement proposal #2 (use configurable in-flight max)
- [ ] Test with reduced indication timeout
- [ ] Test with notifications (`sendUseInd: 0`)
- [ ] Add sequence counter for ongoing monitoring
