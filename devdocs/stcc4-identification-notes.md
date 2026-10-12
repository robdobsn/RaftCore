# STCC4 CO2 sensor: identification, polling and the RSAO probe hazard

Notes behind the `STCC4` record in `devtypes/DeviceTypeRecords.json`. The sensor is the
Sensirion STCC4 miniature CO2 sensor, normally paired with an SHT41 for RH/T compensation,
at the fixed I2C address 0x64.

## Why the record has no detection values

The STCC4 has a product-ID command (`get_product_id`, 0x365B, returning 0x0901018A), but it
cannot be used by the device-type detection path. Detection issues a single write-then-read
transaction with no inter-command delay, and the STCC4 NACKs a read issued immediately after
a command. So `detectionValues` is empty: the address is unique in the catalogue and the
record claims whatever answers at 0x64.

## Init and poll

- Init: `start_continuous_measurement` (0x218B).
- Poll: `read_measurement` (0xEC05), then a 1 ms pause, then read 12 bytes. These are four
  words - CO2, temperature, humidity, status - each followed by a CRC byte. The CRC bytes at
  offsets 2, 5, 8 and 11 are skipped with the attribute `at` offsets.
- Conversions: CO2 raw = ppm; temperature = -45 + 175 * s / 65535; humidity =
  -6 + 125 * s / 65535 (the same formulae as the SHT4x).

Verified working with stable continuous readings of about 390 ppm, 26 C and 62 %RH.

## The RSAO identification probe must never touch it

Address 0x64 is inside the range the RSAO delegated-identification hook probes (0x10 to
0x6F). That hook sends a framed 0x99 vendor command. The STCC4 interprets those bytes as
commands and stops measuring: the product ID stays readable, but `read_measurement` and the
measurement-start commands then NACK until the sensor is power-cycled.

RaftI2C's `DeviceIdentMgr::identifyDevice` therefore runs address-based identification
against the standard device-type records first, and only falls back to the delegated hook for
addresses that have no standard record. The STCC4 has one, so it is never probed. Any change
to that ordering, or any new record-less device at an address in the probe range, needs to
keep this in mind.
