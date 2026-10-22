# Device type records without detection: review

Status: research 2026-10-16. No record has been changed. The work is planned as Stage 12 of
`device-identification-improvement-plan.md` (after Stages 4 and 7). None of the proposed detection strings has been tested
on hardware: none of these devices is on the FeatherS3 bench.

## 1. Purpose

Twelve I2C records in the RaftCore catalogue have no `detectionValues`, so any device that answers at their
address is accepted as that type. Four of them are matched before another record at the same address, which is
then never identified there (generator warning, plan Stage 3). This document goes through each device's
datasheet and drivers to find what could identify it, and whether that would be safe and useful.

Not covered: HX711 and BLEBTHome (not on I2C), and records that have detection but weak detection (for example
MXC400xXC checks a single byte, `0x0E=0x02`).

## 2. Constraints that apply to every proposal

1. **Detection stops at the first failed step** - since Stage 4 (2026-10-16). Before that a mismatch was noted and
   the remaining steps (including their pauses) still ran; only a bus error stopped early. A NACK is a mismatch;
   any other bus error now stops identification of that device for the time being (no fall-through to weaker
   records or the RSAO probe).
2. **A failed detection exposes the device to the RSAO probe.** If no record matches, the RSAO identification
   hook probes the device with a framed 0x99 command (addresses 0x10-0x6F). That is known to stop an STCC4
   until it is power-cycled (`stcc4-identification-notes.md`), and it may do harm to other devices. So a
   detection that can fail on a genuine device - for example only while it is measuring - is worse than empty
   detection, which always matches.
3. **Detection may run on a device that is already running** (re-identification, a firmware restart without a
   power cycle), not only before its `initValues`. A check must work in every state the record puts the
   device in, or fail safe.
4. **A pause in detection blocks the bus task** for its duration (`DeviceIdentMgr` calls `delay()` on the bus
   task), so all polling on that bus stops. A few ms is acceptable; 500 ms or 1.2 s is not, until
   identification can run as resumable steps (plan Stage 7). This already happens today: the Robotical servo and
   water pump records pause 250 ms twice (500 ms) each time a device at one of their addresses is identified,
   and the Trill records 25 ms.
5. **Reading an ID can cost a sample.** For devices that are triggered then read (SHT4x, SGP40), a detection
   between trigger and read loses that sample; the next poll recovers.

## 3. Two improvements to detection itself

- **CRC check (Sensirion CRC-8, poly 0x31, init 0xFF)** - available since Stage 4 as the `:crc8w` suffix. Sensirion devices follow every 2-byte word with a
  CRC. Most have no product ID, only a per-unit serial number, so today the best check is "N bytes came back",
  which an unrelated register-pointer device can also pass. Requiring the CRCs to be valid makes a serial
  number a strong check: random data passes 2 CRCs about 1 time in 65,000 and 3 CRCs about 1 time in 16
  million. Suggested grammar (Stage 4, one grammar): a check suffix such as `0bXXXX...:crc8w` meaning "each
  2-byte word is followed by a valid CRC". This would improve SHT40, SGP40, SCD30, SCD4x and STCC4.
- **Resumable identification with long pauses (Stage 7).** SCD4x and STCC4 need a stop command (500 ms and
  1.2 s) before their ID commands work while measuring. That needs identification steps that wait without
  blocking the bus.

Detection-strength ordering (plan §3.5) also matters here: most of the proposals below are weak signatures
(a few fixed bits), so they must be tried after the neighbours' stronger ID checks, which the strength sort
does automatically.

## 4. Device by device

| Device | Address (shares with) | ID mechanism | Proposed detection | Safe in all states | Strength |
|---|---|---|---|---|---|
| VEML6040 | 0x10 (VEML7700, MXC400xXC, Robotical) | None. Reserved bits of CONF and reserved registers 01h-07h default 0 | `0x00=0b0XXX0XXX00000000&0x03=0x0000&0x05=0x0000&0x07=0x0000` | Yes (reads) | Medium - assumes reserved registers read 0; excludes VEML7700 (0x07 is its ID) and MXC (axes not all 0) |
| AHT20 (AHT21/25, DHT20) | 0x38 (TrillRing) | None. Status byte: bits 4:3 = 1, bits 6:5 = 0 | `=0bX0011XXX` | Mostly - a unit that powers up uncalibrated (CAL=0) fails before init | Weak (4 bits) |
| SHT40 (SHT41/43/45) | 0x44-0x46 (TrillHex) | Serial number 0x89, 6 bytes, per unit | `0x89=p10&=0b` + 48 `X` (strong with a CRC check) | Yes, may lose one sample | Weak now (a Trill also answers); strong with CRC |
| SGP40 | 0x59 | Serial number 0x3682, 9 bytes, per unit. Self-test 0x280E returns 0xD4 but takes 320 ms and heats | `0x3682=p1&=0b` + 72 `X` (strong with a CRC check) | Before init only: "in idle mode"; during measurement undocumented | Weak now; strong with CRC; address has no neighbours |
| MPRLS | 0x18 (MCP9808, Robotical) | None. Status byte: bit 6 = 1 (powered), bits 7, 4, 3, 1 = 0 | `=0b01X00X0X` | Yes | Medium (5 bits); can't match an MCP9808 |
| MT6701 | 0x06 | None (angle registers and EEPROM only) | Keep empty | - | 0x06 is in the reserved 0x04-0x07 range, so other devices there are rare |
| QwiicLEDStick | 0x23 (TrillBar) | None (firmware has no read handler; reads return 0xFF) | `0x00FF=p25&=0xFFFFFF` | Yes (0x00 and 0xFF aren't LED Stick commands) | Medium against a Trill (its reply contains its type), weak otherwise |
| SCD30 | 0x61 | Firmware version 0xD100 (3 bytes), allowed at any time | `0xd100=p10&=0b00000011` + 16 `X` (major version 3) | Yes | Medium; strong with CRC. Clock stretching up to 150 ms; bus <= 100 kHz |
| SCD40 (SCD41/43) | 0x62 | Serial 0x3682 and sensor variant 0x202F (bits 15:12: 0 SCD40, 1 SCD41, 5 SCD43) - idle only | Before init: `0x202f=p1&=0b0000` + 20 `X`. In all states: needs stop (0x3F86, 500 ms) first | No, unless stopped first (500 ms - Stage 7) | Strong (variant), but 0x202F may not exist on pre-2024 firmware |
| STCC4 | 0x64 (0x65 with ADDR high) | Product ID 0x365B = 0x0901018A, fixed, with CRCs: `09 01 73 01 8A D4` - idle only | Idle: `0x365b=p1&=0x090173018ad4`. All states: stop (0x3F86, 1.2 s) first | No, unless stopped first (1.2 s - Stage 7) | Strong |
| GravityO2 | 0x70-0x73 | Version register 0x0F: 0x01 (new firmware), 0xFF (old) | `0x0F=0x01,0xFF` (optionally `&0x03=0bXXXXXXXX0000XXXX0000XXXX`) | Yes | Medium (0x01), weak (0xFF) |
| AMG8833 | 0x69 | None. Unused bits of PCTL, FPSC, INTC and the thermistor read 0 | `0x00=0b00XX000XXXXXXXXX0000000X000000XX&0x0E=0bXXXXXXXX0000XXXX` | Yes except sleep mode (the record never sleeps it) | Medium - assumes unused bits read 0; rejects most IMUs at 0x68/0x69 |

### Notes per device

- **VEML6040** is "not for new designs" (replacement VEML3328). The proposal would fix the VEML6040-before-
  MXC400xXC shadowing. Reading back a written CONF would be stronger but changes a running configuration.
- **AHT20**: the datasheet allows a unit to power up with CAL=0 (then `0xBE0800` - the record's init -
  calibrates it). Such a unit would fail this detection before init, and 0x38 is in the RSAO probe range, so
  `=0bX00XXXXX` (only bits 6:5) might be safer. A Trill Ring's identify response starts 0xFE, which fails
  bit 6.
- **SHT40**: no SHT4x command makes a Trill Hex NACK, so only the order (TrillHex first) or a CRC check
  separates them. Variants SHT40/41/43/45 can't be told apart over I2C. Address suffix A/B/C = 0x44/0x45/0x46.
- **SGP40**: SGP41 shares 0x59 and the same serial command; the record's SGP40 poll command (0x260F) is not
  in the SGP41's command set, so an SGP41 would be identified as an SGP40 and then fail to poll. An SGP41-only
  command (0x2619) could tell them apart (untested).
- **MPRLS**: `=0b01X0000X` would also reject units whose memory integrity check failed.
- **MT6701**: its address can be reprogrammed to 0x46 (EEPROM), which the record doesn't list.
- **QwiicLEDStick**: never send 0x70-0x78 or 0xC7 as detection (they change the LEDs or write EEPROM). The
  jumper address 0x22 isn't listed (it is also inside the Trill Bar range).
- **SCD30**: needs a STOP between write and read (no repeated start) - the two-step form does this. Every
  reference shows firmware 3.x, but other major versions couldn't be ruled out; `=0b` + 24 `X` is the safe
  fallback.
- **SCD4x**: what it does with idle-only commands while measuring is undocumented (probably NACK). An MCP4725
  DAC (common at 0x62) would interpret 0x3F86/0x202F as fast-write commands and change its output - a reason to
  avoid stop-first detection at a shared address. The variant would allow separate SCD40/SCD41/SCD43 records.
- **STCC4**: `stcc4-identification-notes.md` says the product ID can't be used because detection can't pause
  between command and read; the two-step form (`0x365b=p1&=...`) now can. The remaining problem is
  constraint 2: while measuring the check would probably fail and the RSAO probe would then stop the sensor.
  Restarting measurement in its first hour also restarts its 20 s bypass phase.
- **GravityO2**: writing 0x0F to a TCA9548A mux at 0x70 enables channels 0-3 (the check then fails, but the
  mux has changed) - mux addresses must stay excluded as they are today.

## 5. Recommendations

1. **Low risk, worth testing on hardware first**, since each works in every state: MPRLS, GravityO2, SCD30,
   QwiicLEDStick, VEML6040 and AMG8833 (the last two depend on reserved bits reading 0).
2. **AHT20**: use the 2-bit form unless units are known to power up calibrated.
3. **SHT40 and SGP40**: add the CRC check to the grammar (Stage 4), then detect by serial number.
4. **SCD40 and STCC4**: leave empty until identification can run a stop command without blocking (Stage 7),
   unless detection only ever runs on freshly powered devices. Their addresses have no neighbours in the
   catalogue, so empty detection costs little today.
5. **MT6701**: keep empty.
6. **Strength ordering (§3.5)** together with these, so weak signatures are always tried after stronger ones.
7. **Record fixes found on the way**: SGP41 vs SGP40, missing alternative addresses (STCC4 0x65, LED Stick
   0x22, MT6701 0x46).

## 6. Sources

Datasheets and drivers consulted (full lists and quotes are in the research notes):
- VEML6040: https://www.vishay.com/docs/84276/veml6040.pdf; Linux `drivers/iio/light/veml6040.c`
- AHT20: https://cdn-learn.adafruit.com/assets/assets/000/091/676/original/AHT20-datasheet-2020-4-16.pdf;
  https://github.com/adafruit/Adafruit_AHTX0; Linux `drivers/hwmon/aht10.c`
- SHT4x: https://cdn.sparkfun.com/assets/d/5/f/d/6/SHT40_Datasheet.pdf; https://github.com/Sensirion/embedded-i2c-sht4x
- SGP40/SGP41: https://sensirion.com/media/documents/296373BB/6203C5DF/Sensirion_Gas_Sensors_Datasheet_SGP40.pdf;
  https://sensirion.com/media/documents/5FE8673C/61E96F50/Sensirion_Gas_Sensors_Datasheet_SGP41.pdf
- MPRLS: Honeywell MPR series datasheet (32332628); https://github.com/adafruit/Adafruit_MPRLS
- MT6701: https://voltiq.ru/datasheets/MT6701.pdf; https://github.com/I-AM-ENGINEER/MT6701-arduino
- Qwiic LED Stick: https://github.com/sparkfun/SparkFun_Qwiic_LED_Stick (firmware);
  https://github.com/BelaPlatform/Trill-Arduino (Trill identify)
- SCD30: https://sensirion.com/resource/user_guide/scd30/interface_description; https://github.com/Sensirion/embedded-i2c-scd30
- SCD4x: https://sensirion.com/resource/datasheet/scd4x; https://github.com/Sensirion/embedded-i2c-scd4x; Linux
  `drivers/iio/chemical/scd4x.c`
- STCC4: https://sensirion.com/resource/datasheet/STCC4; https://github.com/Sensirion/embedded-i2c-stcc4
- Gravity O2: https://github.com/DFRobot/DFRobot_OxygenSensor
- AMG8833: https://cdn-learn.adafruit.com/assets/assets/000/043/261/original/Grid-EYE_SPECIFICATIONS%28Reference%29.pdf

Unverified points flagged by the research: the SHT4x serial-read time in current datasheets; what SCD4x and
STCC4 do with idle-only commands while measuring; whether 0x202F exists on older SCD4x firmware; whether the
SCD30 major version is always 3; whether VEML6040 reserved registers and AMG8833 unused bits read 0; what the
LED Stick returns on a read; the MCP4725 fast-write interaction.
