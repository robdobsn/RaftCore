# Init values computed from values read from the device: proposal

Date: 2026-10-18.

**Status:** implemented 2026-10-18 (section 6); guards added 2026-10-19 (section 7). The design below is as proposed, except the type-file rule (section 6).

**Raised by:** the BME680 record (device type record review, Stage 12).

## 1. The problem

A record's `initValues` is a fixed string: writes, reads and pauses. Some devices need a value written at init that is **calculated from values read from that device**, usually its factory calibration. A fixed string can't do that, so the record has to use a typical value.

| Device | Value | Calculated from | With a fixed value |
|-|-|-|-|
| BME680 | `res_heat_0` (0x5A), the gas heater set-point | par_g1-g3, res_heat_range (0x02), res_heat_val (0x00), the target temperature, the ambient temperature | The record uses 0x6E, one unit's value for 320 °C. On other units the hot plate runs some tens of degrees off, so gas resistance is a relative trend, not comparable between units |
| BME688 | heater profile (up to 10 set-points) | as BME680 | as BME680 |
| VL53L0X | reference SPAD map (0xB0-0xB5) | reference SPAD count and type from NVM | The record omits the SPAD setup (review §3.3), losing accuracy and range |

**Why do it:**
- It turns these records from approximate into correct: per-unit heater temperature, and the ST time-of-flight sensors' recommended init.
- It keeps the data-driven model. The alternative, a C++ driver per device, would bring back device-specific code in the generic path, which Stage 1 removed.
- It is needed for any future record with a calibration-dependent setting.

## 2. Proposed design: init code

Add an optional **init code** block, written in the same decode language as `resp.c`, that runs once during init. It reads bytes that earlier init steps read, and produces named values. Later init steps write those values through placeholders.

```json
"initValues": "0xe0b6=p10&0x7201&0x7500&0x6466&0x89=r25&0xe1=r16&0x00=r5&0x5a{rh0}&0x7110",
"initCode": {
    "n": "bme680_heater",
    "c": "int gh1=buf[37];if(gh1>127){gh1=gh1-256;} ... float v5=...; init.rh0=3.4*(v5*...-25.0);"
}
```

**Semantics:**
1. Init steps run in order, as now. The bytes read by `=rN` steps (here `0x89=r25`, `0xe1=r16`, `0x00=r5`) are appended to an **init buffer**.
2. Before the first step that contains a placeholder `{name}`, the init code runs with `buf` set to the init buffer. It assigns `init.<name>` values; each is written as one byte (bytes for 16-bit values to be decided).
3. The placeholder step is written with the value substituted. Here `0x5a{rh0}` writes register 0x5A.
4. If the init code fails, for example because a read step failed, init fails as it does today. That is a bus error, retried by the identification scheduler.

**Ambient temperature (BME680):** the heater value varies little with ambient: 0x6D at 15 °C and 0x6E at 25-35 °C for one unit. So the init code can assume 25 °C. If more is needed, an init step can take a temperature measurement first, and the init code can include the temperature compensation.

## 3. Where it goes

| Part | Change |
|-|-|
| **Record format** (wiki *Device Type Record Format*) | `initCode` (`n`, `c`). `{name}` placeholders allowed in init write data |
| **Grammar** (`DeviceTypeGrammar` C++ and Python, shared vectors) | Parse `{name}` in an init step's write data as a value slot: a known length and no hex check. Report a placeholder used before any init code, or an init code that never assigns one. Detection and poll strings don't take placeholders |
| **Generator** (`ProcessDevTypeJsonToC.py`, `DecodeGenerator.py`, `PseudocodeHandler.py`) | Generate an init function per record from `initCode`, as `resp.c` becomes a decode function. `init.<name>` maps to an output array. Add a pointer in the record table (null for most records) |
| **Init execution** (RaftI2C `DeviceIdentMgr::runInitSteps`, RaftCore `DeviceTypeRecords::getInitBusRequests`) | Collect read data into the init buffer, call the init function before the first placeholder step, and substitute. Resumable pauses (Stage 7) keep the buffer in the init job |
| **Runtime type files** (`DeviceTypeFileLoader`) | An `initCode` record from a type file has no compiled function. Reject it with a clear error until there is an on-device interpreter; that is the same requirement as D11, the decoder for type-file records, so do both together |
| **Decode-code check** (`PseudocodeHandler.check_code`) | Allow `init.<name>` in init code, and keep the same restrictions on names and calls, since the code is compiled into the firmware |
| **raftjs** | None. Init runs on the device |

## 4. Alternatives considered

- **Expressions in the init string,** like poll strings' `r{...}` read-length expressions. These are fine for integer arithmetic on one or two read values, but the BME680 heater formula needs floating point and several coefficients. Init code reuses a language, check and generator that already exist.
- **A C++ hook per device type.** It works, but it puts device-specific code back in the generic path, and records from other sources (git sources, type files) couldn't use it.
- **Compute on the host** (raft CLI or app) and write the value with `cmdraw`. It isn't automatic, and it's lost at every reset.

## 5. Testing

- **Grammar vectors:** placeholders accepted in init only; errors for unknown or unassigned names.
- **Generator:** the init function is generated and compiled for a record with `initCode`; records without it are unchanged.
- **Host test:** run init against a mock bus that returns a known calibration, and check the substituted write. For the BME680, the value from one unit's calibration is 0x6E at 320 °C and 25 °C.
- **Hardware:** BME680 on Axiom018. Read 0x5A after init and compare it with the Python reference (`res_heat` for that unit). Then VL53L0X SPAD setup when a VL53L0X is available.

## 6. Implementation (2026-10-18)

**Implemented as designed in section 2**, with these details:
- **Grammar** (C++ and Python, shared vectors): `{name}` in init write data is a one-byte slot, kept in `Step.slots` as an offset and a name. `BadPlaceholder` is an error for a slot outside init, unclosed, unnamed, or not on a byte boundary.
- **Record:** `DeviceTypeRecord.initValuesFn` (`bool fn(const uint8_t* buf, uint32_t len, int32_t* values, uint32_t numValues)`) and `initValueNames` (comma separated). Both are appended to the generated table entry only for records with `initCode`.
- **Generator** (`ProcessDevTypeJsonToC.py`, `init_values()`):
  - compiles `initCode` with `PseudocodeHandler`, mapping `init.<name>` to `pValues[i]`, numbered in order of first use;
  - the function first checks that the bytes read cover the reads before the first slot;
  - errors for a slot with no `initCode`, or a slot `initCode` doesn't assign;
  - warnings for `initCode` without slots, and for code outside the language (`check_code(..., code_kind="init")`).
- **Runtime** (RaftCore, host-tested):
  - `DeviceTypeRecords::InitValueState`, `noteInitStepRead()`, `initStepHasValueSlots()` and `getInitStepWrite()`. The latter computes the values once, then fills each slot with the value clamped to 0-255.
  - RaftI2C `DeviceIdentMgr::runInitSteps` collects the bytes read and builds a slot step's request from the filled-in write data. The identification job keeps the state across resumable pauses; reinit and re-identification use a local one. If the values can't be computed, init fails (`BusError`) and is retried later.
- **Type files:** a record may use slots only when it overrides a compiled record of the same key whose init code computes them, and the override then **inherits the compiled function**, as it inherits the decoder. `initCode` in a type file is ignored, with a warning, in that case and rejected otherwise. `build_type_file` applies the same rule before upload.
- **Type files with the code interpreter (2026-10-19,** `RAFT_RUNTIME_DECODE_CODE`, `runtime-decoding.md` §5**):** a type file record's own `initCode` is run by the interpreter, for a new key or an override. It must compute every slot and guard its init string uses. Without the interpreter the rule above stays.
- **Records:** BME680 and BME690 compute `res_heat_0` (`{heat}`) from par_g1-g3 (0xEB-0xEE) and res_heat_val/res_heat_range (0x00-0x02), at 320 °C and an assumed 25 °C ambient.
- **VL53L0X SPAD setup:** not done at first, as it needs values computed at more than one point. Done 2026-10-26 (section 8).

**Tests:**
- Grammar vectors (8 new), and fuzzing against the legacy parser, with slots treated as intended differences.
- `test_device_type_records` `testInitValues`: the generated BME680/BME690 functions on two real units' calibration bytes give 0x6E and 0x69; too few bytes fails; reads after computing aren't kept.
- `test_device_type_file` `testInitValuesInTypeFile`: an override with a slot is accepted and uses the compiled function; a new key with a slot, a slot on a record without init code, and `initCode` on a new key are rejected.
- Python: generator output and errors, type-file build rule, init code check.

**Axiom018, with a BME690 on slot 1 and a BME680 on slot 2:**
- Both identified. 0x5A reads each unit's own value, 0x69 and 0x6E; the init strings carry only the `{heat}` slot.
- After `reinit`, whose soft reset clears 0x5A, the values were recomputed and rewritten.
- 20 re-identifications kept the types.
- Gas measurements are valid and heater-stable.


## 7. Guards: conditional init steps (2026-10-19)

**Why:** the SCD30 stores its measurement interval and self-calibration (ASC) setting in non-volatile memory. Its record wrote both at every init, including each re-identification and hot-plug. With a guard it reads the interval and writes it only if it differs. (At first ASC was guarded the same way; since the record gained an `autoCal` action, init leaves ASC as stored.)

**Syntax:** `{?name}` at the start of an init step. The step is done only if `init.name` isn't 0, and otherwise skipped entirely: nothing written or read, and no pause. A guard can be combined with value slots (`{?g}0x5a{v}`) and can guard a read or a bare pause.

**Where it goes** (as value slots):
- **Grammar** (C++ and Python, shared vectors): `Step.guard`. `BadPlaceholder` for a guard outside init, unclosed, unnamed, or not at the start of a step. 9 new vectors; the fuzzer includes guard tokens.
- **Generator:** a guard counts as a use of init values: the values are computed from the bytes read before the first step with a slot or guard, and a guard that `initCode` doesn't assign is an error.
- **Runtime:** `initStepUsesInitValues()` (was `initStepHasValueSlots()`); `getInitStepWrite(..., doStep)` reports whether a guarded step runs. RaftI2C `runInitSteps` skips the step when it doesn't.
- **Type files:** a guard follows the slot rule: only in an override of a compiled record whose init code computes it.

**Tests:**
- `test_device_type_records` `testInitGuards`: the SCD30 record with the interval already 2 s (skipped), 5 s and garbage reads (written); too few bytes fails.
- `test_device_type_file`: a guard override of SCD30 accepted with the compiled code; a guard on a new key rejected.
- Python: generator output and errors for guards.

## 8. Values computed at more than one point (2026-10-26)

**Why:** the VL53L0X's recommended init (ST's API, as Pololu's vl53l0x-arduino does it) reads, changes and writes back
registers all through the sequence: register 0x83 twice around the SPAD information read, then the reference SPAD map
(from the SPAD count and type), the GPIO polarity, the final range timeout (from the timing budget, read from the
sequence step timeouts after the tuning settings), and the stop variable when ranging starts. With the values computed
once, before the first step with a slot or guard, the later reads couldn't be used.

**Change:**
- **Runtime** (`DeviceTypeRecords`): the bytes every init step reads are kept (not only those before the first slot), and
  the values are computed again before a step with a slot or guard when more bytes have been read since they were last
  computed. Records whose reads all come before their first slot (BME680, BME690, SCD30) compute once, as before.
- **Reads checked at every slot:** before a step that uses values, every byte the earlier steps read must be there (the
  bytes of steps skipped by their guard are allowed for), so a read NACKed after the first slot fails init, as one
  before it always did, rather than compute values from the wrong bytes.
- **Generated function bounds-checked:** code for a value that a later read gives runs, at the earlier slots, before
  that read is done. The generator makes each `buf[i]` read `bufAt(i)`, which gives 0 past the bytes read so far, as
  the code interpreter (type files, `RAFT_RUNTIME_DECODE_CODE`) already did, so the two agree and the compiled code
  can't read past the end of the buffer. The function's own check (the bytes read before the first slot) is unchanged.
- **Unchanged:** the grammar, type files (an override still inherits the compiled function), raftjs and RaftI2C
  (`runInitSteps` already noted every read and asked for each slot step's write).

**VL53L0X record:** Pololu's full init with 15 init values: 2V8 mode (0x89), the 0x83 strobe bits, the SPAD map
(0xB0-0xB5), GPIO active low (0x84), the final range timeout (0x71-0x72, behind a guard that skips it if the budget
can't be kept, as `setMeasurementTimingBudget` refuses) and the stop variable; ST's tuning settings; VHV and phase
calibration; continuous back-to-back ranging. Waits for the device (the NVM strobe, the calibrations) are pauses.

**Tests:**
- `test_device_type_records` `testInitValuesMultiPoint`: the VL53L0X init run step by step with known reads gives
  Pololu's writes (SPAD map for 5 aperture SPADs, final range timeout 0x0290 for the 33849 us budget); a NACKed SPAD map
  read fails init at the next slot. `testInitValues`: later reads are now kept.
- `test_runtime_decoder`: the compiled init functions and the interpreter agree on random buffers of every length,
  including ones shorter than the VL53L0X code reads.
- Python generator test: `bufAt` in the generated function.

**Axiom018 (2026-10-26), a VL53L0X on slot 3:** identified; after init 0x89 = 0x01, 0x44 = 0x0020, 0x01 = 0xE8,
0x0A = 0x04, 0x84 = 0x01, 0x71 = 0x0290 (Pololu's value), 0xB6 = 0xB4, ranging back to back. The SPAD information read
directly afterwards was 0x8F (aperture, 15 SPADs) and the map was 00 F0 FF 07 00 00, 15 SPADs from SPAD 12. Ranges
valid (status 11) and within about 3 mm (38-44 mm at about 4 cm), where the earlier record's single-shot ranges spread
over 30 mm or more; the same after a restart without a power cycle.
