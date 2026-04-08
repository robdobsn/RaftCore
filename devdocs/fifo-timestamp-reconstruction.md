# FIFO Timestamp Reconstruction for Cross-Device Synchronization

## Problem Statement

When logging data from FIFO-based sensors (e.g., LSM6DS3 IMU) at native sampling rates, the timestamps assigned to individual samples do not accurately reflect the true time each sample was captured by the sensor. This matters when correlating data from multiple sensors — for example, two accelerometers measuring different aspects of the same physical system — where sub-millisecond cross-device timing accuracy is desirable.

## Current Timestamp Pipeline

### Firmware Side

1. **Poll timestamp capture** (`DevicePollingMgr.cpp`): A 16-bit timestamp is written into the first 2 bytes of the poll result buffer at the start of each I2C transaction. The value is `(micros() / POLL_RESULT_RESOLUTION_US) & 0xFFFF`. Currently `POLL_RESULT_RESOLUTION_US = 1000` giving millisecond resolution, wrapping every ~65.5 seconds. This resolution is a significant source of error at higher ODRs (see §Timestamp Resolution below).

2. **FIFO decode** (generated in `DeviceTypeRecords_generated.h`): The decode function extracts the poll timestamp, assigns it to the first FIFO sample, then increments by the nominal inter-sample interval (`"us": 9615` for 104 Hz LSM6DS3) for each subsequent sample. Wrap-around is handled via `RaftBusDeviceDecodeState`.

3. **DataLogger** (`DataLogger.cpp`): Reads the decoded `timeMs` from each record struct and converts to session-relative time for CSV/JSONL output.

### raftjs Side

`RaftAttributeHandler.ts` follows the same model: `extractTimestampAndAdvanceIdx()` reads the 2-byte poll timestamp, then `processMsgAttrGroup()` assigns `timestampUs + i * timeIncUs` to the i-th data point. A monotonicity fixup bumps any non-increasing timestamp by 1 µs.

### What Goes Wrong

1. **Poll timestamp ≠ sample time.** The timestamp records when the I2C read started, not when any FIFO sample was acquired. There is variable latency (bus contention, task scheduling) between sensor sampling and the poll.

2. **Nominal interval ≠ actual interval.** The `us` field is the datasheet-nominal period. Real sensor oscillators drift (LSM6DS3: ±1.5% ODR tolerance). Over a 10-minute session at 104 Hz with 1% drift, timestamps accumulate ~6 seconds of error.

3. **Batch-boundary discontinuities.** Each poll resets the base timestamp. If the poll interval varies (48 ms one cycle, 52 ms the next), there are gaps or overlaps between the last synthesized timestamp of batch N and the first of batch N+1. The TS code masks this with a +1 µs monotonicity fixup, which distorts the actual time spacing.

4. **Truncation loss.** The C++ path computes `timeMs = timestampUs / 1000`, losing sub-ms fractional microseconds. At 9615 µs intervals this creates a ~0.6 ms sawtooth pattern.

5. **Timestamp quantization at higher ODRs.** At 1 ms resolution, the 16-bit timestamp has only ~2.4 distinct values per sample interval at 416 Hz, and ~4.8 at 208 Hz. This quantization dominates the regression noise at higher sample rates, severely limiting the accuracy of interval estimation.

## Timestamp Resolution: Change to 0.1 ms

### Rationale

The current 1 ms timestamp resolution is adequate for 104 Hz but becomes a critical bottleneck at higher ODRs. Reducing `POLL_RESULT_RESOLUTION_US` from 1000 to 100 (0.1 ms resolution) provides a 10x improvement in timestamp precision with minimal downside.

### Quantization Impact by ODR

| ODR (Hz) | Sample interval (µs) | Ticks/interval @ 1ms | Quant. error @ 1ms | Ticks/interval @ 0.1ms | Quant. error @ 0.1ms |
|---|---|---|---|---|---|
| 104 | 9615 | ~9.6 | ±500 µs (5.2%) | ~96 | ±50 µs (0.5%) |
| 208 | 4808 | ~4.8 | ±500 µs (10.4%) | ~48 | ±50 µs (1.0%) |
| 416 | 2404 | ~2.4 | ±500 µs (20.8%) | ~24 | ±50 µs (2.1%) |

At 416 Hz with 1 ms resolution, the timestamp quantization noise (±500 µs) is 21% of the sample interval — effectively useless for distinguishing individual sample times. At 0.1 ms, quantization drops to 2.1%, making the EMA regression viable at all supported ODRs.

### Wrap-Around at 6.55 Seconds

With 0.1 ms resolution, the 16-bit counter wraps every 6.5536 seconds instead of 65.5 seconds. This is safe because:

- The slowest configured poll interval is 100 ms (for 12.5/26 Hz ODRs), giving ~65 polls per wrap period.
- Even a hypothetical 1-second poll interval gives ~6 polls per wrap — adequate for wrap detection.
- A device not polled for >6.55 seconds is effectively offline and wrap detection doesn't matter.
- The wrap detection logic (`timestampUs + threshold < lastReportTimestampUs`) works identically; only the threshold constant needs scaling proportionally (e.g., from 100 ms to 10 ms in timestamp units).

### Impact on EMA Regression Accuracy

The total timestamp noise is the RSS of polling jitter and quantization:

- Current: √(1000² + 500²) ≈ 1118 µs
- Proposed: √(1000² + 50²) ≈ 1001 µs

The ~10% reduction in total noise is modest because RTOS scheduling jitter (~1 ms) dominates. However, the more important benefit is that quantization noise is systematic, not random — it creates deterministic aliasing patterns that can bias the EMA when the poll interval is near a multiple of the quantization step. The 10x finer resolution largely eliminates this aliasing risk, producing more stable convergence.

For direct timestamp use (non-FIFO single-sample devices where the regression is not applicable), the improvement is a straightforward 10x gain in precision.

### devbin v2 Compatibility

The devbin v2 wire format (described in `unified-sampling-rate-and-polling-design.md`) is not affected structurally — the timestamp remains 2 bytes in the sample data. Only the interpretation changes: the decoder multiplies by 100 instead of 1000 to recover microseconds. Since devbin v2 is a new format that clients must already update to handle, this is a clean opportunity to change the resolution without backward-compatibility concerns.

The devbin v1 / legacy format continues to use 1 ms resolution. Clients negotiating v1 apply the old constant; clients using v2 apply the new one. The `POLL_RESULT_RESOLUTION_US` constant on the firmware side changes globally, so the ring buffer and decode functions use 0.1 ms natively; only the v1 publish path (if retained) would need to re-scale.

### Changes Required

| Component | Change |
|---|---|
| `DevicePollingInfo.h` | `POLL_RESULT_RESOLUTION_US = 100` |
| `DevicePollingMgr.cpp` | No change (already uses the constant) |
| Generated decode functions | No change (already multiply by `POLL_RESULT_RESOLUTION_US`) |
| Wrap detection (C++/TS) | Scale threshold from 100000 µs to 10000 µs (or derive from `POLL_RESULT_WRAP_VALUE * POLL_RESULT_RESOLUTION_US`) |
| raftjs `RaftAttributeHandler.ts` | `POLL_RESULT_RESOLUTION_US = 100` and adjust wrap threshold |
| DataLogger | No change (consumes decoded `timeMs` from structs) |

## Proposed Solution: Continuous Clock Reconstruction

Combine backward-anchoring (use poll time as the time of the *last* sample) with adaptive interval estimation and continuous clock-origin refinement.

### Conceptual Model

Each sensor produces samples at a constant (but unknown) rate on its own oscillator. The time of the j-th sample globally is:

```
T_sample_j = T0 + j * Δt_actual
```

where `T0` is the ESP32-time of the hypothetical zeroth sample and `Δt_actual` is the true sample interval. At each poll k we observe:

- `T_k` = ESP32 µs time when the FIFO was read
- `N_k` = number of samples in the FIFO
- `J_k = Σ N_i` = cumulative sample count

The constraint is that the most recent FIFO sample was produced just before the read:

```
T_k ≈ T0 + J_k * Δt_actual + ε_k
```

where `ε_k ∈ [0, Δt_actual)` is a random phase offset — how far into the next sample cycle the poll landed.

This is effectively a linear regression over observations `(J_k, T_k)`. The slope gives `Δt_actual`, the intercept gives `T0`.

### Per-Device Calibration State

Extend `RaftBusDeviceDecodeState` with:

```cpp
// Timestamp reconstruction state
uint64_t cumulativeSampleCount = 0;    // total samples across all polls
double   estimatedIntervalUs = 0.0;    // EMA of actual sample interval (µs)
double   estimatedT0Us = 0.0;          // estimated clock origin (ESP32 µs)
uint64_t prevPollTimeUs = 0;           // previous poll timestamp (ESP32 µs)
uint64_t prevCumulCount = 0;           // cumulative count at previous poll
bool     tsCalibrated = false;         // whether the estimate has converged
uint16_t tsCalibrationPolls = 0;       // number of polls used for calibration
```

### Algorithm (Per Poll)

```
On receiving poll k with N_k samples at ESP32 time T_k:

    if not calibrated:
        estimatedIntervalUs = nominalIntervalUs   // from "us" field
        estimatedT0Us = T_k - N_k * nominalIntervalUs
        calibrated = true
        prevPollTimeUs = T_k
        prevCumulCount = 0
        cumulativeSampleCount = N_k
    else:
        // Compute instantaneous interval from this poll gap
        instantIntervalUs = (T_k - prevPollTimeUs) / N_k

        // EMA update with adaptive alpha
        alpha = tsCalibrationPolls < 20 ? 0.3 : 0.05
        estimatedIntervalUs = alpha * instantIntervalUs
                            + (1 - alpha) * estimatedIntervalUs

        cumulativeSampleCount += N_k

        // Recompute T0 from latest observation
        // T_k ≈ T0 + cumulativeSampleCount * Δt
        estimatedT0Us = T_k - cumulativeSampleCount * estimatedIntervalUs

        prevPollTimeUs = T_k
        prevCumulCount = cumulativeSampleCount - N_k

    tsCalibrationPolls++

    // Assign timestamps (backward-anchored, globally monotonic)
    for i = 0 to N_k - 1:   // 0 = oldest sample in batch
        sampleIdx = prevCumulCount + i
        T_sample_i = estimatedT0Us + sampleIdx * estimatedIntervalUs
```

### Properties

- **Globally monotonic** by construction — cumulative count only increases, and `estimatedIntervalUs > 0`.
- **No batch-boundary discontinuities** — timestamps are derived from a single continuous model, not per-batch anchoring.
- **Self-correcting** — the EMA tracks actual sensor clock drift from temperature changes etc.
- **Cross-device synchronizable** — both sensors' timestamps reference the same ESP32 µs clock; the per-device `T0` and `Δt` are independently estimated.

### Expected Accuracy

With ~5 samples/poll, accuracy depends on timestamp resolution:

**At 0.1 ms timestamp resolution** (combined noise σ_ε ≈ 1001 µs):

| Polls (time at 50ms/poll) | Interval accuracy (σ_Δt) | Cross-device sync (σ_sync) |
|---|---|---|
| 20 (~1 s) | ~45 µs | ~315 µs |
| 100 (~5 s) | ~20 µs | ~140 µs |
| 1000 (~50 s) | ~6 µs | ~45 µs |

**At 1 ms timestamp resolution** (combined noise σ_ε ≈ 1118 µs — includes quantization aliasing):

| Polls (time at 50ms/poll) | Interval accuracy (σ_Δt) | Cross-device sync (σ_sync) |
|---|---|---|
| 20 (~1 s) | ~50 µs | ~354 µs |
| 100 (~5 s) | ~22 µs | ~158 µs |
| 1000 (~50 s) | ~7 µs | ~50 µs |

The numerical difference is modest because polling jitter dominates, but the 0.1 ms resolution eliminates systematic quantization aliasing that can stall or bias convergence at specific polling frequencies. After a brief warmup, cross-device timing accuracy is well within one sample period at either resolution.

## Implementation Strategy

### Phase 1: Firmware Decode Path (DataLogger)

This is the simplest path since the DataLogger already has `millis()` available and operates on fully decoded structs.

1. **Extend `RaftBusDeviceDecodeState`** with the calibration fields listed above. This struct is already maintained per-device in `DeviceCallbackCtx::decodeState`.

2. **Modify the generated decode functions** (via the code generator in `scripts/`) to:
   - Accept the poll timestamp as a µs value rather than encoding/decoding it as a 16-bit truncated field. Alternatively, reconstruct full µs from the existing 16-bit value using the wrap-around logic already present.
   - After determining `N` (the FIFO sample count), run the EMA update on `decodeState`.
   - Assign `pOut->timeMs = (estimatedT0Us + sampleIdx * estimatedIntervalUs) / 1000.0` for each record.

3. **DataLogger changes**: Minimal — it already reads `timeMs` from decoded structs. Consider storing full µs precision (e.g., `timeUs` field or a `double timeMs`) to preserve sub-ms accuracy in CSV output.

4. **Fallback**: For non-FIFO sensors (`N=1` every poll), the algorithm degenerates to using the poll timestamp directly. The existing behaviour is preserved — no regression/interpolation is possible with a single sample per poll.

### Phase 2: raftjs / BLE Path

The raftjs path has two differences: the timestamp arrives as an encoded 16-bit value over BLE (not raw `millis()`), and decoding happens in TypeScript.

1. **Extend `DeviceTimeline`** with the same calibration state fields. This object is already maintained per-device.

2. **In `processMsgAttrGroup()`**: After the custom handler (or standard handler) produces `numNewDataPoints` values, apply the EMA algorithm instead of the current `timestampUs + i * timeIncUs` logic.

3. **Timestamp resolution**: With the 0.1 ms resolution change (see §Timestamp Resolution above), the 16-bit timestamp provides adequate precision for the regression at all supported ODRs up to 416 Hz (~24 ticks per sample interval). The raftjs `POLL_RESULT_RESOLUTION_US` constant and wrap detection threshold must be updated to match the firmware. Since this change is tied to the devbin v2 format, the raftjs parser can select the appropriate resolution constant based on the negotiated protocol version.

### Phase 3: Improvements

1. **Use I2C transaction end time instead of start time.** The poll timestamp is currently captured before the I2C read (`DevicePollingMgr.cpp` line 100). The FIFO content reflects what was available when the status register was read (first I2C operation), so the "true" FIFO snapshot time is `T_k + δ_i2c` where `δ_i2c ≈ 0.5-1 ms`. This is a near-constant bias that folds into T0 and doesn't affect cross-device sync (both devices have similar overhead), but correcting for it would improve absolute accuracy. Could record `micros()` at both start and end of the poll transaction and use the midpoint or the time of the status register read.

2. **Handle FIFO mid-read sample arrival.** Between reading the FIFO status register and completing the data read, new samples may arrive. At 104 Hz with ~5 ms I2C read time, there's a ~50% chance of one extra sample. This means `N_k` (from the status register) may undercount by 0-1, adding noise to the interval estimate. The EMA smoothing handles this naturally, but an explicit correction (checking if extra words were read) could help.

3. **Adaptive alpha based on residuals.** Instead of a fixed two-phase alpha (0.3 then 0.05), track the prediction residual `|T_k - (T0 + J_k * Δt)|` and increase alpha when residuals grow (indicating a clock drift rate change, e.g., from temperature shift).

4. **Multi-poll regression.** For higher accuracy, maintain a small sliding window of `(J_k, T_k)` pairs and fit a proper least-squares line. This is more robust to outliers than EMA but requires more state and computation. Likely overkill given the EMA performance.

## Where This Does Not Help

- **Sensors on different ESP32s**: No shared clock reference. Would require NTP/PTP or GPS PPS synchronization.
- **Non-FIFO single-sample sensors**: With `N=1`, there is no inter-sample interval to estimate. The algorithm falls back to using the poll timestamp directly.
- **First few polls**: Before the estimator converges (~1 second), timestamps are based on the nominal interval. Could be acceptable for most use cases; alternatively, retroactively correct early timestamps once the estimate stabilises (feasible if samples are buffered before writing).

## Files Affected

### RaftCore (library level)
- `components/core/DeviceTypes/DevicePollingInfo.h` — `POLL_RESULT_RESOLUTION_US` change from 1000 to 100; reference constants
- `components/core/Bus/RaftBusDevicesIF.h` — `RaftBusDeviceDecodeState` extension with calibration fields
- `scripts/` — code generator for `DeviceTypeRecords_generated.h` decode functions (EMA logic, wrap threshold)

### RaftI2C
- `components/RaftI2C/BusI2C/DevicePollingMgr.cpp` — optionally capture higher-precision or end-of-transaction timestamps (the resolution change itself requires no code change here as it already uses the constant)

### Application level (may fold into Raft)
- `components/DataLogger/DataLogger.cpp` — consume improved timestamps from decode path

### raftjs
- `src/RaftAttributeHandler.ts` — `POLL_RESULT_RESOLUTION_US` change to 100, wrap detection threshold update, EMA regression logic in `processMsgAttrGroup()` and `extractTimestampAndAdvanceIdx()`
- `src/RaftDeviceStates.ts` — `DeviceTimeline` calibration state extension
- `src/RaftDeviceStates.ts` — `DeviceTimeline` calibration state extension
