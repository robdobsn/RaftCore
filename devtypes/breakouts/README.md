# I2C breakouts

The I2C breakout boards (STEMMA QT, Qwiic, Grove and similar) known to work with RaftCore, those still to be
tested, and those still to be obtained. A record in `../records` describes a chip; this list describes the
products (a vendor's board) that carry it, and the tests done on them. Many products can use one record.

| File | One row per |
|---|---|
| `breakouts.csv` | product, whether owned or not |
| `tests.csv` | test of a record on a breakout |

Which boards are owned (`owned.csv`: `id,qty,notes`) is personal, so it is kept outside RaftCore and passed to the
script with `--owned` or the environment variable `RAFT_BREAKOUTS_OWNED`. A breakout with no row there, or
`qty` 0, is one still to obtain.

## breakouts.csv

- `id`: `<vendor>-<product id>`, or `<vendor>-<product name>` where there is no product id. Never change an id once a
  test or owned.csv uses it
- `vendor`, `product`, `vendor_pid`, `url`
- `chip`, `chip_manufacturer`, `datasheet`
- `category`: light, distance, motion, environment, gas, temperature, touch, input, adc, display, led, motor, power,
  rfid, io, mux, magnetic, presence, biometric, output, camera
- `raft_record`: the key of the RaftCore record that supports it (`records/<key>.json`), blank if none
- `connector`: `stemma-qt`, `qwiic`, `stemma`, `grove`, `none`, or blank if not known
- `vcc_min`, `vcc_max`: the supply range of the board, not the chip. `vcc_max` of 5 or more means 5V tolerant. Most
  SparkFun Qwiic boards are 3.3V only
- `addresses`: default first, e.g. `0x29` or `0x28-0x2b` or `0x1d,0x53`
- `status`: `active`, `discontinued` or `retired` (by the vendor)
- `priority`: `high`, `medium`, `low` or blank - how much a record for it, a test of it or getting one is wanted
- `notes`

## tests.csv

- `date` (YYYY-MM-DD), `raft_record`, `breakout_id`, `tester`, `host` (the board it was tested on),
  `raftcore_version`
- `checks`: what was checked, separated by spaces:
  - `detect`: found and identified as `raft_record` at its address, and polled
  - `data`: the decoded values are right (e.g. a known light level, distance, orientation, touch)
  - `alt-addr`: also detected at an alternate address (jumper or address pin)
  - `neighbours`: not taken by, and not taking, another record sharing its address, with that board attached
  - `power-cycle`: detected and identified again after a power cycle
  - `rsao`: the effect of Axiom's RSAO probe (the lone 0x99 write and read) checked, as in the record's
    `hwProbeEffects`
- `result`: `pass`, `partial` or `fail`, for the checks listed
- `notes`

A breakout counts as **validated** when it has a `pass` with at least `detect` and `data`.

## Testing a breakout

1. Attach the breakout to a host running a build that includes its record, with the boards that share its address
   (the report lists them) attached too where possible.
2. Run `python3 scripts/breakouts/breakouts.py bench <breakout id> --ip <host IP>`. It checks the device is found,
   identified as the right record and polled without misses, and prints the `tests.csv` row it would add.
3. Check the values in the dashboard (or another client) against known conditions.
4. Add the row: run it again with `--record pass` (or `partial`, `fail`), `--checks` and `--notes`.

## Scripts

`scripts/breakouts`:

```
python3 breakouts.py check [--out report.md]     # validate the CSVs against the records and report the gaps
python3 breakouts.py bench <id> --ip <host IP>   # check a breakout on a host and record the test
python3 breakouts.py enrich [--write]            # fill blank Adafruit fields from adafruit.com
python3 raft_inventory.py [--out records.csv]    # RaftCore's records as CSV
```

The report lists the boards owned with no record (with the records at the same addresses), owned and supported
but not validated, not owned (the list to obtain), records with no owned board, and tests not tied to a breakout.
`scripts/tests/test_breakouts.py` checks the two CSV files against the records.
