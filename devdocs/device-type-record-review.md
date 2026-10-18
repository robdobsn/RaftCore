# Device type record review (Stage 12)

Date: 2026-10-18.

**Status:** desk review of all 49 RaftCore records done.

**Progress (2026-10-18):** most of §3.1 and §3.2 is applied on `feature/device-type-records`, one record per commit. Done:
- **Scaling (§3.1):** MCP9808, AMG8833, AdafruitSoilSensor, MXC400xXC, MPRLS (hPa), SGP30, VEML7700, LTR-329, VCNL4040, M5Encoder, MAX30101, GravityO2.
- **Init, address and action fixes (§3.2):** ADXL313, TMP117, RoboticalWaterPump, QwiicLEDStick (brightness only), LPS25, STCC4, MMC5603, MAX17048, VL53L0X (identPriority), SGP40 (actions removed).

**Held or moved:**
- **LSM6DS:** 0x6C dropped, rate settings keep the FIFO depth, 0x6B added (2026-10-18). 0x69 parts are still matched; their FIFO-full protection needs a record of its own. The gyro scale change is still held by decision.
- **VL6180:** init restores 0x001C (2026-10-18).

**Axiom018 (2026-10-18), firmware built from these records, app-only flash:**
- **Identification unchanged:** the same 5 devices with the same type indexes as before the flash. At 0x29 the candidate order is now VL53L0X, VL6180, VL53L1X, VL53L4CD, VL53L5CX, LTR-329, and the VL6180 is identified.
- **VL6180:** 0x001C reads 0x31 after a firmware restart without a power cycle. On the old firmware, after a 50 Hz setting, it stayed at 0x03.
- **LSM6DS:** after `devconfig sampleRateHz=104`, FIFO_CTRL1/2 read 0x3FC (1020 words); on the old firmware they read 0x030 (48 words).
- **MAX17048 (0x736):** didn't answer on this unit (no battery; also absent before the flash), so the wider detection is still unverified.
- No warnings or errors in the boot log.
- **Which LSM6DS the Axiom has:** WHO_AM_I reads **0x69**, an original LSM6DS3, not a 0x6A-family part as the desk review assumed.
- **FIFO after 25 s with polling stopped** (`devenable poll=0`):

  | FIFO setting | FIFO_STATUS1/2 | Meaning |
  |-|-|-|
  | FIFO_CTRL4 = 0x00 (before the 2026-09-11 fix) | `00 e0` | full, count 0: the deadlock |
  | The record's FIFO_CTRL4 = 0x80 | `00 e8` | full and overrun, count 2048 (undocumented on 0x69; it still drains) |
  | CTRL4_C = 0x01 (ST's STOP_ON_FTH on 0x69) | `fc e3` | stopped at the 1020-word threshold, as designed |

  So the current record avoids the deadlock on this part only through undocumented behaviour. The documented fix is CTRL4_C bit 0, which must stay 0 on 0x6A parts, so the two chips need separate records.
- **Gyro scale, by rotation:**
  - The unit was tipped onto its side: 91.4° by the accelerometer.
  - FIFO at 12.5 Hz nominal; the measured rate was 13.43 samples/s.
  - Integrating the full 3-D rotation and comparing the predicted gravity direction with the measured one:
    - the record's d = 16.384 predicts 81.3° (error 10.4°);
    - d = 14.2857 (70 mdps/LSB) predicts 93.0° (error 5.6°);
    - the best fit is 67.8 mdps/LSB.
  - The record's gyro therefore reads **low**: true rate ≈ 1.15 × reported. The table in §2 said high; that was wrong.
- **QwiicLEDStick 0x22:** moved to §3.3; adding the address needs detection first.
- **SGP30 and SCD40 actions:** done (decision 5). raftjs actions take `"crc": "crc8w"`, and both records' actions use it. SCD40's four idle-only settings still need measurement stopped, which the record doesn't do; their descriptions say so.

**Checks:**
- `DeviceTypesTool check` over all records gives 0 errors and 0 warnings; the three init grammar warnings are gone.
- Host unit tests and script tests pass.
- Sample frames decoded through raftjs give the expected values for every scaling change. The VL53L0X priority change needs an identification check on the Axiom.

**Decisions taken:**
- The scaling fixes are applied, except the LSM6DS gyro.
- MPRLS reports hPa.
- GravityO2's layout change is accepted.
- Sensirion actions get CRC support.

**Method:** each record was checked against its datasheet and reference drivers (vendor, Adafruit, SparkFun, Linux IIO, Pololu, Bela, M5Stack). The check covered:
- addresses;
- detection: strength, safety in every device state, and variants;
- init;
- poll commands and timing;
- data layout and scaling.

Every current and proposed string was compiled with `DeviceTypeGrammar`; all proposed strings compile. The decoder findings for MCP9808 and AMG8833 were confirmed in raftjs `RaftAttributeHandler` and `DecodeGenerator.py`. The RoboticalWaterPump type ID was confirmed against Axiom's `RSAOTypeRegistry.cpp`.

This follows Stage 12 of `device-identification-improvement-plan.md` and extends `device-detection-review.md`.

**Terms used below:**
- **Layout change:** `resp.b` or attribute offsets change, so the devbin stream for that type changes.
- **Scaling change:** decoded values or units change for clients; the layout is unchanged.
- **HW:** needs a device to verify.

## 1. Cross-cutting findings

1. **Sensirion actions can't work.** raftjs `sendAction` writes `w` + value + `wz` with no per-word CRC, so the sensor ignores the command.
   - This affects SGP30 (`setBaseline`, `setHumidity`) and SCD40 (all 5 actions; 4 of them are idle-only too).
   - SGP40's two actions aren't SGP40 commands at all.
   - Either add a CRC option to actions (raftjs and firmware), or remove these actions.
2. **Invasive detections run first at shared addresses.** Candidates are ordered by `identPriority`, then detection strength. A long detection that writes before it checks is therefore tried *first*, on whatever device is there:
   - **Robotical servo, water pump and light sensor:** at 0x10-0x18 and 0x28, they write a framed START_APP and ROBOTICAL_ID with 500 ms of pauses before checking anything. This hits VEML7700, MXC400xXC, MCP9808, MPRLS, CAP1188/1203 and TrillSquare.
   - **VL6180 at 0x29:** its 16-bit index `00 00` is a register write on a VL53L0X (SYSRANGE_START = 0). VL53L1X/L4CD's `01 0F` would write SYSTEM_SEQUENCE_CONFIG on a VL53L0X if VL53L0X's own detection failed.
   - **Trill identify `00 FF`:**
     - at 0x40-0x4F, it would set MODE1 = 0xFF on a PCA9685 PWM driver. EXTCLK is sticky until a power cycle, so PWM would stop (desk analysis, HW);
     - at 0x20-0x27 and 0x38-0x3F, it would glitch PCF8574 expanders.
   - **AdafruitSoilSensor `00 01`:** a malformed Trill "set mode" at shared addresses 0x36-0x39.

   **Rule for records:** read an ID first, and write only after something has matched. Where a write is unavoidable, the record should not be the strongest at that address.
3. **The Stage 12 text on pauses is out of date.** Since Stage 7, scheduled identification returns `Paused` for detection pauses of 10 ms or more, so they no longer block the bus. They still block in `reIdentifyDevice()` and `identifyDevice()`; re-identifying at 0x10-0x18 costs about 1 s today.
4. **"lux" on raw counts.** VEML7700 `als`, LTR-329 `ir`/`visible`, and the VCNL4040 and VEML7700 `white` channels are labelled lux but are counts. Only VEML7700 `als` has a calibrated lux factor.
5. **Bugs in the producer, not the record.** BLEBTHome's record is right, but RaftSysMods `BLEAdvertDecoder`:
   - takes illuminance one byte early (it includes the object ID);
   - gives the button object 0x3A length 3 instead of 1;
   - ignores the encryption flag.
6. **Missing alternative addresses:**
   - safe to add: STCC4 0x65, LPS25 0x5C, LSM6DS 0x6B;
   - only together with detection, or with care: QwiicLEDStick 0x22, QwiicButton 0x60-0x6F, AdafruitGamepad 0x50-0x52 (not 0x53), AdafruitSoilSensor 0x37-0x39, AMG8833 0x68, MT6701 0x46.

## 2. Summary by record

**Verdicts:** S = significant, m = minor, OK.

| Record | Verdict | Main finding | Layout / scaling | HW |
|-|-|-|-|-|
| ADXL313 | S | Init sets the Sleep bit (`0x2d0c`), giving 8 Hz updates; range is ±2 g, not the stated ±4 g | rate changes | light |
| LSM6DS | S | Gyro `d` 16.384 should be 14.2857 (70 mdps/LSB): reads about 13% low (confirmed by rotation on the Axiom). Detection accepts LSM6DSO (0x6C), which has a different FIFO. FIFO-full fix ineffective on 0x69 parts. `_conf.rate` entries cut FIFO depth to about 77 ms | scaling | Axiom (0x6A) |
| MXC400xXC | S | Temperature `d` 0.586 should be 1.7065. Init reads instead of writing the reset. ID 0x03 not accepted | scaling | yes |
| MMC5603 | m | ODR 100 Hz exceeds the 75 Hz limit at BW=00 | - | light |
| AS5600 | m | Detection is 6 bits; 20 possible from config registers | - | yes |
| MT6701 | OK | Optional address 0x46 | - | - |
| M5Encoder | m | Counter is signed int16, decoded as uint16 | scaling | detection |
| HX711 | m | Not I2C, no addresses, no producer found: document or remove | - | - |
| M5-Weight | m | Cosmetic output types | - | - |
| AHT20 | m | No detection (`=0bX00XXXXX` possible). First sample stale: init doesn't trigger a measurement | - | detection |
| SHT40 | m | No detection: serial number with `:crc8w` (strength 16) | - | yes |
| SGP30 | S | Detection can't match a real SGP30. Poll reads 0 ms after trigger (needs 12 ms). VOC decoded from the wrong bytes (`at` 3). Actions lack CRCs | VOC values | yes |
| SGP40 | m | Actions aren't SGP40 commands. An SGP41 at 0x59 matches as SGP40 and fails: add an SGP41 record | - | yes |
| SCD30 | m | Detection possible (firmware version + CRC). Init rewrites ASC and interval to NV memory on each boot | - | light |
| SCD40 | S | Poll lacks the 1 ms wait after 0xEC05. Actions lack CRCs. Detection via 0xE4B8 may work in periodic mode (no stop needed) | - | yes |
| STCC4 | m | Address 0x65 missing; keep detection empty | - | 0x65 |
| LPS25 | m | Address 0x5C missing; BDU not set | - | light |
| MPRLS | S | Scaling about 0.36% high (+370 Pa at 1 atm); range wrong. Detection possible | scaling | detection |
| MCP9808 | S | `ss` 4096 makes negative temperatures wrong (−1 °C reads −255 °C) | negative values | no |
| TMP117 | m | Init `0x01=0x0220` writes only the pointer | - | no |
| MAX17048 | m | Detection accepts only versions 0x12/0x13 (use 0x001X) | - | light |
| LTR-329 | m | `ir`/`visible` are counts, not lux. Repeat rate 500 ms vs 100 ms poll | units | no |
| LTR-390 | S | No pause after reset, so the config write may be lost (factor 4 error). Poll pause 100 ms where 60 ms suffices; gain implicit | - | yes |
| VCNL4040 | m | `white` isn't lux; `prox` has `d` 0 | units | no |
| VEML6040 | m | No detection: documented reserved CONF bits first | - | yes |
| VEML7700 | S | `als` is raw counts labelled lux (×0.0672 at gain 1/100 ms) | scaling | no |
| VL53L0X | S | Init omits ST tuning and reference calibrations; signal limit 0.5 vs 0.25 MCPS. Needs `identPriority` 3 to protect it at 0x29 | - | yes |
| VL53L1X | S | Init doesn't configure or start ranging (index-only writes): almost certainly doesn't range. No interrupt clear; `valid` test wrong | - | yes |
| VL53L4CD | m | No stop before reconfiguring; interrupt cleared before reading; `valid` test wrong | - | yes |
| VL6180 | m | Init doesn't restore 0x001C (convergence) on re-init after `_conf.rate` | - | Axiom |
| MAX30101 | S | Decode can read past its buffer (N not clamped to 8); no 18-bit mask. Init `0x11=0x21`; no wait after reset; manufacturer is Maxim, not Vishay | - | yes |
| CAP1188 | OK | Optional LED linking | - | - |
| CAP1203 | m | Sensitivity 128x (SparkFun uses 2x). Detection could add manufacturer ID | - | yes |
| Trill ×6 | m | Detection pause 25 ms could be 10 ms. Init lacks scan, prescaler and noise settings. Identify at 0x40-0x4F risks a PCA9685 | - | yes |
| QwiicButton | OK | Optional addresses 0x60-0x6F; latched click bit | - | partly |
| QwiicLEDStick | S | Brightness action uses 0x76 (single LED) not 0x77 (all), range 0-31. Address 0x22 | - | no |
| AdafruitGamepad | m | Accepts HW_ID 0x87 only (ATtiny816 builds report 0x86); add product-code check | - | yes |
| AdafruitSoilSensor | m | Temperature unsigned (`>I` → `>i`, d 65536). Detection 25 ms pause (1 ms enough). Poll pauses 250 ms ×2 | negative values | timings |
| AMG8833 | m | `s:-4,d:64` makes negative temperatures +1023 °C: use `m:"0x0FFF",d:4`. No detection | negative values | detection |
| GravityO2 | S | Ignores the device's calibration key (reg 0x0A), so it's wrong after calibrating | layout (b 3→5), scaling | preferably |
| RoboticalServo | S | Invasive detection runs first at 0x10-0x18 (see §1.2) | - | yes |
| RoboticalWaterPump | S | Type ID 0x0098 should be 0x0099 (Axiom changed it): never matches in RaftCore. Same detection issue | - | ID no |
| RoboticalLightSensor | S | Invasive detection runs first at 0x28 | - | yes |
| BLEBTHome | OK | Record fine; producer bugs (§1.5) | - | no |

## 3. Proposed changes in detail

Grouped by how they can be verified. Exact strings are from the review and compile.

### 3.1 Wrong values today, fixable without hardware (scaling changes)

- **MCP9808:** remove `"ss": 4096` (keep `"sb": 12`).
- **AMG8833:** replace `"s": -4, "d": 64` with `"m": "0x0FFF", "d": 4`.
- **AdafruitSoilSensor:** temperature `"t": ">i", "d": 65536`.
- **MXC400xXC:** temperature `"d": 1.7065, "r": [-40, 85]`.
- **LSM6DS:** gyro `"d": 14.285714` for gx/gy/gz.
- **MPRLS:** choose one:
  - `"d": 77.8665, "a": -21546.1, "r": [0, 172369]` (Pa);
  - or hPa: `"u": "hPa", "d": 7786.654, "a": -215.461, "r": [0, 1724]`.
- **SGP30:** VOC `"at": 3`.
- **VEML7700:** `als` `"d": 14.881, "f": ".2f", "o": "float"`; `white` `"u": ""`.
- **LTR-329:** `ir`/`visible` `"u": "", "t": "<H"`.
- **VCNL4040:**
  - `white`: remove `d`, set `"u": ""`;
  - `prox`: remove `"d": 0`.
- **M5Encoder:** rotation `"t": "<h", "o": "int16", "f": "d"`.
- **MAX30101:**
  - custom code clamps N to 8 and masks `&0x3FFFF`;
  - `r` [0,262143];
  - `"manu": "Maxim"`.
- **GravityO2 (layout change):**
  - poll `0x0A=r2&0x03=r3`, `b` 5;
  - code uses the key (`k/1000`, falling back to 20.9/120 when 0).

### 3.2 Init, address and action bugs, fixable without hardware

- **ADXL313:**
  - `"initValues": "0x310b=&0x2d08="` (±4 g, Measure only);
  - `"detectionValues": "0x00=0xAD1DCB"`.
- **TMP117:** `"initValues": "0x010220="`.
- **MXC400xXC:**
  - `"initValues": "0x0110=p5&0x0d00="`;
  - `"detectionValues": "0x0E=0x02,0x03"`.
- **MAX30101:** `"initValues": "0x0940=p10&0x0903&0x085f&0x00=r1&0x0a27&0x0c4040&0x1121"`.
- **RoboticalWaterPump:** `0x00=0x0098` → `0x00=0x0099`, as in Axiom.
- **QwiicLEDStick:** brightness action `"w": "77", "r": [0,31], "d": 31`. Address 0x22 goes with detection (§3.3).
- **SGP40:** remove `actions`.
- **SGP30 and SCD40:** remove the actions (or flag them), until actions can carry CRCs.
- **LPS25:**
  - addresses `0x5C-0x5D`;
  - `"initValues": "0x2104=p5&0x20C4="` (BDU on).
- **STCC4:** addresses `0x64-0x65`.
- **MMC5603:** `0x1A64=` → `0x1A32=` (50 Hz).
- **LTR-329:** init `0x8002=p20&0x8501&0x8001` (100 ms repeat).
- **MAX17048:** detection `0x08=0b000000000001XXXX`.
- **LSM6DS:**
  - detection `0x0f=0x6a` (drop 0x6C);
  - `_conf.rate` entries use `06fc&0703` (keep the watermark depth);
  - address 0x6B.
- **VL53L0X:** `"identPriority": 3` (protects it from the 16-bit-index detections at 0x29).

### 3.3 Needs hardware

**Detection additions:**

| Record | Detection | Strength |
|-|-|-|
| AHT20 | `=0bX00XXXXX` | weak |
| SHT40 | `0x89=p10&=0b`+48 X+`:crc8w` | |
| SGP30 | `0x202f=p10&=0b0000`+20 X+`:crc8w` (current detection never matches) | |
| SCD30 | `0xd100=p5&=0b`+24 X+`:crc8w` | |
| SCD40 | `0xe4b8=p1&=0b`+24 X+`:crc8w` | |
| MPRLS | `=0b01X00X0X` | |
| VEML6040 | `0x00=0b0XXX0XXX00000000` | |
| AS5600 | config-register check | 20 bits |
| CAP1203 | `0xfd=0x6d&0xfe=0x5d` | |
| AdafruitGamepad | `0x0001=0x86,0x87&0x0002=0b0001011001101111`+16 X | |
| AdafruitSoilSensor | `0x0001=p1&=0x55` | |
| AMG8833 | from `device-detection-review.md` | |
| QwiicLEDStick | `0x00FF=p10&=0xFFFFFF` | |
| Trill | `p25` → `p10` | |

**New record:** SGP41, with detection on 0x2619; an SGP40 NACKs that command.

**Poll and timing fixes:**
- SGP30: init `0x2003=p10&0x2008=`, poll `=r6&0x2008=`.
- SCD40: poll `0xec05=p1&=r9`. The fix is high-confidence on paper; it is unverified only because no SCD40 is to hand.
- LTR-390: init `0x0010=p10&0x0440&0x0501&0x000A`, poll `0x0D=r5&0x0002=p60&0x000A`.
- AdafruitSoilSensor: poll pauses 10 ms / 2 ms.
- AHT20: init `0xbe0800=p10&0xac3300=`.

**ST time-of-flight sensors:**
- VL53L1X: ULD default configuration and start, interrupt clear in the poll, `valid` from status 9.
- VL53L0X: ST tuning settings and VHV/phase calibrations (about 100 steps; SPAD setup can't be done in a string).
- VL53L4CD: stop before configuring; clear the interrupt after reading; `valid` lookup table.
- VL6180: add `0x001c31`.

**Robotical:**
- Detection reads WHOAMI first: `0x00=<id>&0x9904ff63=0x526f626f746963616c`.
- START_APP moves into init.
- Needs: what WHOAMI returns in the bootloader, and the app boot time.

**Trill init:** scan settings, prescaler and noise per type, as Bela's library sets them.

**Other:**
- CAP1203 sensitivity `0x1f6f`.
- SCD30: decide whether to keep the ASC-off write.
- LSM6DS: a separate `LSM6DS3` record for 0x69 parts (STOP_ON_FTH is CTRL4_C bit 0 there).

### 3.4 Elsewhere

- **RaftSysMods BLEManager:** BTHome illuminance offset, button length, encryption flag.
- **raftjs and firmware:** a CRC option for actions on Sensirion parts, or removal of those actions.
- **Documentation:** `distance-sensor-improvement.md` still says VL6180 0x1B=0x09.

## 4. Decisions needed

1. **Scaling corrections (§3.1) change values clients see**, with the layout unchanged. Are these acceptable as bug fixes? The LSM6DS gyro change affects Axiom's IMU users.
2. **MPRLS unit:** Pa (corrected), or hPa to match LPS25?
3. **GravityO2:** accept the layout change?
4. **LSM6DS:** remove 0x6C now, and add an `LSM6DS3` record later?
5. **Sensirion actions:** add CRC support, or remove them?
6. **SCD30:** keep the ASC-off write at each boot?

## 5. Suggested order

1. **§3.1 and §3.2, one record per commit**, with host checks: `DeviceTypesTool check`, the generator, and the reference catalogue tests updated where they pin values.
2. **Axiom checks:** LSM6DS (gyro scale with a known rotation, FIFO depth), VL6180 (re-init), MAX17048. The VL53L0X priority change only needs identification on the Axiom to be unchanged.
3. **§3.3, as devices become available.** Each needs:
   - identification from power-up and after a firmware restart;
   - neighbours at the same address still identified;
   - readings against a reference.
4. **The cross-cutting items (§3.4)** as separate changes in their repos.
