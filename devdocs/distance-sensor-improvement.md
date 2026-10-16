# Distance Sensor Improvement — VL6180

Status: implemented on branch `fix/distance-sensor-validity` in RaftCore and RoboticalAxiom1; not yet tested on hardware
Date: 2026-10-16
Scope: the `VL6180` device type record in `RaftCore/devtypes/DeviceTypeRecords.json` and
`RoboticalAxiom1/AxiomDevTypes/DeviceTypeRecords.json` (the two copies are kept identical apart from JSON formatting)

## 1. Symptom

On an Axiom1 with a VL6180 attached, when nothing is within range the reported output switches between two states:

- `valid: no` (no distance shown)
- `valid: yes, dist: 255`

The sensor plainly knows there is no target, but it reports this in two different ways. The question was also raised
whether 255 mm is the maximum range, or whether values were being truncated.

## 2. Findings

### 2.1 How the old record worked

| Part | Old value | Meaning |
|---|---|---|
| Poll | `0x004f=r1&0x0062=r1&0x001507&0x001801` every 200 ms | Read `RESULT__INTERRUPT_STATUS_GPIO`, read `RESULT__RANGE_VAL`, clear interrupts, start a single-shot range |
| `valid` | `m: 0x04, s: 2` on the 0x004F byte | The "new range sample ready" interrupt bit |
| `dist` | raw 8-bit 0x0062, `vft: valid` | Hidden when `valid` is 0 |

Each poll reads the result of the measurement started by the previous poll.

### 2.2 Why there were two kinds of "nothing in range"

- **`valid: yes, dist: 255`**: the "new sample ready" bit does not mean the measurement succeeded. When there is no
  target, the measurement ends with an error. The error can come from the early convergence estimate, the
  maximum convergence time or the signal-to-noise check. The sensor still raises "new sample ready", puts the
  error code in `RESULT__RANGE_STATUS` (0x004D, bits 7:4) and sets the range value to 255. The old poll never
  read 0x004D, so these errors looked like valid readings of 255 mm.
- **`valid: no`**: no "new sample ready" was seen for that cycle. A no-target measurement should finish in about
  60 ms, well inside the 200 ms poll, so the most likely cause is that the single-shot start was ignored. A
  start is ignored when the sensor is not yet back in its ready state after a failed measurement. This was not
  confirmed on hardware; reading bit 0 of 0x004D ("device ready") would show it.

The underlying fault is that validity was taken from the interrupt status rather than the range error code.

### 2.3 Is 255 mm the maximum range?

- **No truncation:** 0x0062 is an 8-bit register and the poll reads all of it. With 1x range scaling
  (`RANGE_SCALER` 0x0096/0x0097 = 0x00FD) one count is 1 mm, so 255 is the top of the register. The sensor
  uses 255 as its "no target / error" value.
- **Real range:** ST specifies the VL6180X for 0–100 mm. In good conditions (a reflective target and low
  ambient light) it reaches roughly 150–200 mm.
- **Range scaling:** 2x or 3x scaling would let the register represent larger distances, at 2 or 3 mm per
  count. It also needs other registers rescaled and a multiplier in the decoder, and it only helps if the
  target returns enough signal. It was decided to keep 1x scaling.

### 2.4 Other faults in the old record

- **Interleaved mode:** the notes said interleaved ALS and range mode was enabled (`0x2a3=>0x01`), but
  `initValues` never wrote 0x02A3.
- **ALS gain:** the notes said ALS gain 20 (`0x003f=>0x40`), but the record wrote 0x42 (gain ≈5).
- **Unused ALS measurement:** init ended with `0x0038=0x01`, a single ambient light (ALS) measurement. Its
  result was never read, and it could delay the first range measurement.
- **Fresh-out-of-reset flag:** ST's private settings were loaded without clearing `SYSTEM__FRESH_OUT_OF_RESET`
  (0x0016), as ST application note AN4545 recommends.

## 3. Changes

Branch `fix/distance-sensor-validity` (from `main`) in both RaftCore and RoboticalAxiom1.

### 3.1 Decisions

| Choice | Decision |
|---|---|
| Range scaling | Keep 1x |
| Ranging mode | Continuous, paced by the sensor at 100 ms, so the poll never issues a start that can be ignored |
| ALS | Removed (it was never reported) |
| Error code | Reported as its own attribute `err`, hidden from charts by default |

### 3.2 Init sequence

| Write | Purpose |
|---|---|
| `0x0018=0x01`, pause 100 ms | Put the sensor in a known idle state (see 3.4) |
| AN4545 private settings (unchanged) | ST's required tuning, including 1x range scaling |
| `0x0016=0x00` | Clear `SYSTEM__FRESH_OUT_OF_RESET` |
| `0x0011=0x10` | GPIO1 interrupt output, active low |
| `0x010A=0x30` | Readout averaging period 4.3 ms |
| `0x0031=0xFF` | Temperature (VHV) recalibration every 255 ranges |
| `0x002E=0x01`, pause 10 ms | One VHV recalibration now |
| `0x001B=0x09` | Range inter-measurement period 100 ms |
| `0x0014=0x04` | Interrupt on new range sample only (ALS bits removed) |
| `0x0015=0x07` | Clear all interrupts |
| `0x0018=0x03` | Start continuous ranging |

Removed: `0x003F` (ALS gain), `0x0040/0x0041` (ALS integration period), `0x003E` (ALS inter-measurement
period), `0x0038` (ALS start).

### 3.3 Poll and decoding

Poll: `0x004d=r1&0x0062=r1&0x001507` every 200 ms. It reads `RESULT__RANGE_STATUS`, then `RESULT__RANGE_VAL`,
then clears interrupts.

| Attribute | Source | Decoding |
|---|---|---|
| `valid` | byte 0 (0x004D) | `m: 0xF0, s: 4`, then `lut` maps error code 0 to 1 and anything else to 0 |
| `err` | byte 0 (0x004D) | `m: 0xF0, s: 4`; range error code 0–15, hidden series |
| `dist` | byte 1 (0x0062) | Raw mm, `vft: valid`, so it is hidden whenever `err` is not 0 |

All three attributes use `at` so their byte positions are explicit. This follows the pattern already used by the
VL53L0X record (`valid` derived from a status field with `lut`).

Common error codes: 0 = no error, 6 = early convergence estimate, 7 = max convergence time, 11 = signal to noise.
6, 7 and 11 are the usual results when there is no target in range.

### 3.4 Why init begins with `0x0018=0x01`

In continuous mode, writing the start/stop bit of `SYSRANGE__START` toggles ranging. RaftCore re-runs init when
a device reappears on the bus, and on Axiom the sensor is not necessarily power-cycled when the ESP32 resets. A
plain `0x0018=0x03` on a sensor that is still ranging would therefore *stop* it.

According to ST's driver, writing `0x01` (start/stop with single-shot mode selected) stops continuous ranging if
it is running. Otherwise it triggers one single-shot measurement, which is harmless. The 100 ms pause lets
either case finish, so the rest of init always starts from an idle sensor.

### 3.5 Notes

`_notes` in the record now describes each register write, the poll, the meaning of the error codes and that ALS
is not used.

## 4. Verification so far

- Both JSON files parse, and the RaftCore decode tests pass (`scripts/tests`, 12/12).
- `ProcessDevTypeJsonToC.py` accepts both records.
- A temporary raftjs jest test (not committed) decoded three simulated responses with the new record:

  | 0x004D | 0x0062 | `valid` | `err` | `dist` |
  |---|---|---|---|---|
  | 0x01 | 42 | 1 | 0 | 42 |
  | 0x61 | 255 | 0 | 6 | hidden |
  | 0xB1 | 255 | 0 | 11 | hidden |

## 5. Open items

- **Hardware test.** Testing will be done on the branch this work is merged into. Points to check:
  - With nothing in range, `valid` should stay at 0 with no flipping, and `err` should show the error code.
  - With a target, distance readings should be stable and track the target.
  - Re-init without a power cycle (for example, unplug and replug the sensor while the board stays powered)
    should leave ranging running. This depends on the stop/start behaviour in 3.4, which comes from ST's driver
    rather than from a test.
- **C++ decoder.** `scripts/DecodeGenerator.py` does not support `lut`, so C++-side decoding gives `valid` as the
  raw error code (0 means OK), the inverse of the JS meaning. The VL53L0X record has the same limitation.
  Adding `lut` support to the generator would fix both.
- **raftjs test data.** `raftjs/testdata/TestDeviceTypeRecs.json` still holds the old VL6180 record. It was not
  changed in this work.

## 6. Future work: the VL53 family

A similar, thorough review of the other ST time-of-flight records is planned but has not been done yet. It
should cover:

| Record | Where | Current poll | Current attributes |
|---|---|---|---|
| VL53L0X | RaftCore, Axiom | `0x14=r12&0x0b01&0x0001` | `valid`, `status`, `dist` |
| VL53L1X | RaftCore, Axiom | `0x0089=r1&0x0096=r2` | `valid`, `dist` |
| VL53L4CD | RaftCore, Axiom | `0x008601=&0x0089=r1&0x0096=r2` | `valid`, `dist` |
| VL53L5CX (VL53LxCX multizone) | Axiom | custom device (no poll string) | `grid`, `seq`, `tempC`, `nValid`, `dist`, `status` |

Questions to answer for each:

- Is `valid` derived from the sensor's range status or error code, or only from a "data ready" flag (the
  VL6180 fault)?
- How is "no target" reported, and do the out-of-range values reach the user as distances?
- Does the record use single-shot or continuous ranging? Is the poll interval consistent with the
  measurement timing? Is a re-init safe on a sensor that was not power-cycled?
- Do the init settings and `_notes` match the vendor application notes, and do the notes describe what the
  init actually writes?
- What range is reported, what is the real usable range, and is there any scaling or truncation?

For the VL53L5CX/VL53L8CX multizone devices, see also `RoboticalAxiom1/devdocs/vl53l5cx-vl53l8cx-multizone-tof-plan.md`.
