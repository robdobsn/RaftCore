# LSM6DS FIFO-full deadlock — why the on-board IMU stops publishing

> Status: **fixed and verified** — 2026-09-11. Record change (option 1) built (`raft build -c -i`,
> local ESP-IDF 6.0.2; Docker was not available) and flashed to Axiom018; re-test below passed. Symptom: the raftjs
> dashboard shows no accelerometer/gyro readings although the LSM6DS is listed online. It had
> not been seen before; it appeared after a day of RSAO test-mode sessions and commissioning
> runs on Axiom018.

## What was observed (without resetting anything)

- The Axiom kept polling the IMU (`devman/listdevs` intermittently listed `1_76a`, which it
  only does while unconsumed poll responses are buffered) and kept publishing records for it.
- A raftjs client subscribed to `devbin` received the IMU's records every poll, but each one
  carried only the four FIFO status bytes and no FIFO words, so the decoder produced zero
  samples. Raw record payload, after the sequence number, sample length and timestamp:
  `00 e0 02 00`.
- Decoding those as LSM6DS3 `FIFO_STATUS1..4`: unread-word count low byte 0x00; `STATUS2` =
  0xE0 = FTH | OVER_RUN | FIFO_FULL with the count's high nibble 0; pattern index 2. That is
  "FIFO completely full, count reads as zero".

## Mechanism

The LSM6DS3's 12-bit `DIFF_FIFO` counter wraps to 0 when the FIFO holds all 4096 words. The
device-type record's poll string sizes the data read from that counter:

```
"c": "0x3a=r4&0x3e=r{$0.w12:mask0FFF*2:max240}"
```

With the counter at 0 the poll reads 0 bytes of FIFO data, nothing is drained, the FIFO stays
full and the counter stays 0 — permanently. `FIFO_CTRL5` = 0x26 (continuous mode, 104 Hz)
does not help: continuous mode overwrites old data but the *count* still reads 0 while full.

Getting there needs the host to stop draining for longer than 4096 words / (104 Hz × 6 words)
≈ 6.6 s. Anything that pauses the bus worker that long does it: an RSAO test-mode session
(`rsao/enter` … `rsao/exit`), and every commissioning / arbitration pass (the `RESET_BOARD`
settle alone is 3 s, a full assign about 8 s). So the deadlock can be triggered by normal
operation whenever an RSAO is plugged in at 0x0F.

## Recovery without a reset

Two write-only commands re-initialise the FIFO (the same bytes the record's `initValues`
write at identification):

```
/api/devman/cmdraw?deviceid=1_76a&hexWr=0a00&numToRd=0     # FIFO_CTRL5 = bypass (resets the FIFO)
/api/devman/cmdraw?deviceid=1_76a&hexWr=0a26&numToRd=0     # FIFO_CTRL5 = continuous, 104 Hz
```

Samples resumed immediately (348 decoded in 9 s; FIFO status then read a normal word count).

## Fix applied

`devtypes/DeviceTypeRecords.json`, `LSM6DS.initValues` now writes `FIFO_CTRL4` = 0x80
(`STOP_ON_FTH`) and a watermark of 1020 words (`FIFO_CTRL1` = 0xFC, `FIFO_CTRL2` = 0x03) before
enabling continuous mode:

```
0x0a00&0x1244&0x1048&0x114c&0x1640&0x0809&0x0980&0x06fc&0x0703&0x0a26
```

The FIFO depth is now limited to 1020 words (170 samples, ~1.6 s at 104 Hz), so `DIFF_FIFO`
reads at most 1020 and never wraps; after a long host pause the poll reads its 240-byte
maximum each time and drains the backlog at ~90 words per 50 ms poll. The previous 48-word
threshold was only used for the FTH flag. Re-test after flashing: run an RSAO test-mode
session for > 10 s (or a commissioning pass) and confirm the IMU keeps publishing afterwards
(`raftjs/tests/e2e/observe-devices.mjs`).

**Re-test result (2026-09-11 12:02):** after boot the IMU init ran with the 10 new init writes and the
IMU streamed; the bus worker was then paused for 15 s through `rsao/enter` … `rsao/exit` (more than
twice the old 6.6 s fill time); afterwards the IMU kept streaming - 963 samples in 9 s (~107 Hz, the
full sensor rate), az ≈ -0.97 g, FIFO status showing a normal word count. Before the fix the same
pause left it at zero samples indefinitely.

## Fix options considered

1. **Never let the counter wrap.** Set `FIFO_CTRL4.STOP_ON_FTH` = 1 with a watermark below
   4096 words (e.g. 1024) in `initValues`; the FIFO depth is then limited to the threshold and
   `DIFF_FIFO` reads the threshold when full, so the poll still reads (up to `max240`) and
   drains it. Simplest and entirely in the device-type record.
2. **Interpret FULL + 0 as "read max"** in the poll-length expression or in RaftI2C's
   `PollReadLenExpr` handling (e.g. a `:fullmask` / conditional), so a full FIFO is drained
   rather than ignored.
3. **Re-run the device init when `FIFO_FULL` is seen** in the poll response — a generic
   "device needs re-init" hook in `DevicePollingMgr`, which would also cover sensors that
   lose configuration.
4. Longer term: keep the bus worker polling non-RSAO devices during arbitration instead of a
   whole-bus pause, or bound the pause below the FIFO fill time.

## Related bench notes

- `devman/cmdraw` *reads* time out for every device on this Axiom (the endpoint waits at most
  20 ms for the worker; with two polled devices the turnaround is longer), so raw reads are
  not a usable diagnostic here; write-only commands (`numToRd=0`) work.
- Opening the Axiom's USB COM port with default DTR/RTS resets it; use
  `raft m -p COMx --agent-stream` to watch its log.
- A small node observer that connects like the dashboard and dumps raw device records lives
  in `raftjs/tests/e2e/observe-devices.mjs`.
