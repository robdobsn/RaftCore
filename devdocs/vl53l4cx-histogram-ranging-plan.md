# VL53L4CX: histogram ranging and telling it from the VL53L4CD

Date: 2026-10-26.

> **Status: parked (2026-10-26).** Proper VL53L4CX support (its histogram ranging, up to about 6 m) needs processing
> on the host that a device type record can't do (§3), and a way to tell the sensor from a VL53L4CD, which reads the
> same identification registers (§2). It is **not on any schedule**: it is listed under "Parked" in RaftCore's
> `TODO.md`, and decisions L1-L3 (§5) are open. Meanwhile a VL53L4CX is identified as a VL53L4CD and ranges with that
> record (§4).

**Companion documents:** `firmware-loaded-i2c-devices-plan.md` (the VL53L5CX, parked the same way: also a sensor ST
drives with a large host driver); `computed-init-values.md`; `device-type-record-review.md`.

## 1. The two sensors

| | VL53L4CD | VL53L4CX |
|-|-|-|
| Range | up to about 1.3 m | up to about 6 m, several targets |
| How it ranges | on the chip: the sensor reports a distance | histograms: the sensor reports photon-count bins, and ST's driver works out the distances on the host |
| ST's driver | VL53L4CD Ultra Lite Driver: a default configuration, a few registers, a result block | VL53L4CX driver (STSW-IMG029, as stm32duino/VL53L4CX): about 1 MB of source - presets, calibration, NVM, histogram algorithms (`hist_algos_gen3/gen4`, crosstalk, sigma, dmax) |
| Licence | BSD-3-Clause (ST) | BSD-3-Clause (ST) |

## 2. Identification

Both read IDENTIFICATION__MODEL_ID (0x010F) = 0xEB and MODULE_TYPE (0x0110) = 0xAA: ST's VL53L4CD driver expects
0xEBAA, and ST's VL53L4CX driver identifies its sensor by the same two values (`IsL4()`). So a record's detection
can't tell them apart, and the VL53L4CD record (identPriority 1, at 0x29) claims a VL53L4CX.

The VL53L4CX on Axiom018 (2026-10-26) read 0x010F-0x0112 = EB AA 10 FF (REVISION_ID 0x10). Whether a VL53L4CD reads a
different revision, or differs in an NVM field, isn't known: there was no VL53L4CD to compare.

## 3. Why a record can't do the VL53L4CX's own ranging

The VL53L4CX's distances come from ST's histogram algorithms running on the host, on the bins the sensor reports
(ST's driver has only histogram presets: short, medium and long). A record's poll can read the bins, but the
processing - ambient estimation, pulse fitting, crosstalk correction, sigma and dmax estimates, several targets - is
far beyond the decode language, and too much code to keep as decode code.

Ways it could be done (none chosen):
- **A SysMod with ST's driver**, as the Axiom does for the VL53L5CX (`VL53LxCX`). Device-specific code outside the
  record model, but it works with ST's code as it is.
- **A compiled processing hook for a record:** a record names a processing function compiled into the firmware (ST's
  histogram code, adapted), which its decode calls with the bins. Generic in RaftCore/RaftI2C, but the processing
  code is still the VL53L4CX's. It also needs the VL53L4CX's init, which is much longer than the VL53L4CD's and reads
  its NVM and calibration (computed init values, `computed-init-values.md`, can carry some of it).

## 4. What happens now

A VL53L4CX is identified as a VL53L4CD and runs the VL53L4CD record: ST's VL53L4CD configuration and on-chip
ranging, 50 ms timing budget. On Axiom018 (2026-10-26) the VL53L4CX did this: init as the VL53L4CD's (the range
timeouts calculated from its oscillator, 0x02F2/0x03A1); with a target at about 15 cm, valid ranges of 134-137 mm
(sigma 1.5 mm, signal about 10 Mcps); with nothing within about 1.3 m it reported status 5 (phase) at about 1.5 m. So it works as a short-range sensor, not to its 6 m. The record's
notes say so.

## 5. Decisions (open)

- **L1. Identification:** compare a VL53L4CD's REVISION_ID and NVM fields with the VL53L4CX's (§2), and use a
  difference in detection if there is one; otherwise the project chooses (a VL53L4CX record that starts disabled,
  enabled in `DeviceTypeSources.json` with the VL53L4CD record disabled).
- **L2. Ranging:** a SysMod with ST's driver, or a compiled processing hook (§3).
- **L3. Scope:** whether the VL53L3CX (also histogram ranging, ST's VL53LX driver family) is done the same way.

## 6. Before resuming

- Re-read ST's current VL53L4CX driver (stm32duino/VL53L4CX) and check its licence.
- If the VL53L5CX work (`firmware-loaded-i2c-devices-plan.md`) has gone ahead, reuse what it added: long init
  sequences, asset data, reads over 240 bytes.
- A VL53L4CD for the identification comparison (L1).
