# Init values computed from values read from the device: proposal

Date: 2026-10-18.

**Status:** proposed, not started.

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
