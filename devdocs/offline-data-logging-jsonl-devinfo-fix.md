# Offline Data Logging JSONL Devinfo Fix

## Date

2026-04-29

## Context

The Axiom Experiment App batch 6 ODL e2e test added a raw JSONL smoke test:

- connect to Axiom over BLE;
- open the offline data logging workspace;
- select only LSM6DS;
- capture a short raw JSONL log;
- preview the saved log as raw text;
- download the log through the UI;
- parse every line as JSON.

The first test run captured and downloaded the expected `.jsonl` file, but JSON
parsing failed on the `devinfo` line.

## Symptom

The failing JSONL record contained a plug-and-play device info object with
`poll` encoded as a quoted string while its value was already JSON:

```json
"poll":"{"c":"..."}"
```

That makes the overall line invalid JSON. Any JSONL consumer that parses each
line with `JSON.parse` fails before it can inspect later data records.

## Root Cause

`DeviceTypeRecord::getJson(true)` builds the plug-and-play device type JSON.
Before this fix it concatenated several fields directly into JSON strings:

- `type`
- `addr`
- `det`
- `init`
- `poll`

`pollInfo` is JSON content, not plain text, so wrapping it in quotes corrupts
the JSON object. The other string fields were also inserted without JSON string
escaping, so a quote or backslash in any field could corrupt the same JSONL
record.

## Fix

`components/core/DeviceTypes/DeviceTypeRecord.h` now:

- includes `RaftUtils.h`;
- escapes string fields with `Raft::escapeString(value, true)`;
- emits `poll` as a nested JSON object instead of a quoted raw string;
- preserves the existing object insertion behavior for `devInfoJson`.

This keeps raw JSONL logs parseable while preserving the same semantic fields in
the `devinfo` record.

## Verification

Built and flashed Axiom009 with local Raft libraries:

```sh
raft build --no-docker -s Axiom009 -c
raft flash -s Axiom009 --port /dev/cu.usbmodem2101
```

Then reran the app-side real BLE JSONL smoke test:

```sh
BLE_DEVICE_REGEX='Axiom009' npm run test:e2e:offline-logging-jsonl-raw-real
```

The test passed. The captured JSONL contained:

- 1 header record;
- 1 valid `devinfo` record;
- 5 valid raw LSM6DS data records;
- a `.jsonl` saved-log file that previewed as raw text, downloaded through the
  UI, parsed successfully, and was deleted after validation.
