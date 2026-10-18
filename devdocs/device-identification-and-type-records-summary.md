# Device identification and device type records: summary of work

Date: 2026-10-18. Covers the `feature/device-identification` and `feature/device-type-records` branches. Details are
in `device-identification-improvement-plan.md` and `device-type-record-management-plan.md`.

## 1. What changed

| Repo | `feature/device-identification` (vs `main`) | `feature/device-type-records` (vs identification) |
|-|-|-|
| RaftCore | 43 commits, 89 files, +16.3k / -1.3k | 12 commits, 83 files, +6.6k / -2.9k |
| RaftI2C | 16 commits, 16 files, +2.7k / -0.8k | - |
| RaftSysMods | 5 commits, small | - |
| raftjs | 8 commits, +0.9k / -0.2k | 1 commit (type file wrappers) |
| RaftROS | 4 commits, small | - |
| RoboticalAxiom1 | 19 commits, +0.6k | 3 commits, +1.5k / -3.9k (its catalogue fork removed) |
| RaftCLI | - | 3 commits, +0.9k (`raft devtypes`) |

The largest areas, in order: the device type table and its runtime index, the shared grammar, the type file loader,
the identification scheduler, the poll scheduler, the scan schedule, the DeviceManager APIs and settings store, the
record assembler with its git sources, and the CLI tool.

## 2. Identification branch (Stages 0-11)

| Stage | Change | Goals |
|-|-|-|
| 0 | Agreed terms (`deviceTypeIndex`, type key, hardware type ID). Poll, bus activity and identification timing statistics in `devman/busstatus`; bench script | G11, baseline for G8/G9 |
| 1 | Polls on a fixed phase, earliest due first, within a time budget; the bus worker sleeps until the next poll; scanning runs last; LSM6DS-specific code removed from generic paths | G9 |
| 2 | Compiled records `const` in flash, with a key-sorted index. Extended records packed into one allocation each (PSRAM-aware); lock-free reads; explicit add results (added, present, conflict, full) | G1, G2, G3, G4 |
| 3 | `CatalogueIndex` built at runtime from all records: candidates per address, scan lists, reserved addresses, hardware type ID lookup. New record fields: `identPriority`, `scanPriority`, `probeOnly` | G7, G4 |
| 4 | One grammar for detection, init and poll strings, compiled once, with errors reported. Proven identical to the old parser on 269 real strings and 200,000 fuzzed ones. `PollPlan` removes a per-poll copy | G4, G7 |
| 5 | Per-type policy (enabled, disabled, disabled-claim); overrides of compiled records before identification starts; protected types (`expectType`) | G5 |
| 6 | Device type file loaded at startup, with each record checked; identification waits for it (with a timeout) | G6 |
| 7 | Identification state per element with a retry policy, scheduled within a time budget; init outcomes handled; candidates tried by priority and detection strength | G8, G9 |
| 8 | Scoped re-identification (make-before-break); live policy changes; a restart-required flag for type file changes | G6, G10 |
| 9 | Interleaved scan lists, a background sweep of unclaimed addresses, confirmation probes (seen-to-online 120-196 ms → 5-7 ms), multiplexer fixes | G8 |
| 10 | DeviceManager APIs: one settings file (`devsettings.json`), `devenable`, `reinit`, `identinfo`, paged `types`, `reserved` | G10 |
| 11 | Canonical request names (`deviceTypeKey=`, `deviceTypeIndex=`) with the old forms kept; raftjs fields added, old ones marked deprecated | G11 |

Also on this branch:
- the `RAFT_DEVICE_MANAGER` build option, so DeviceManager can be left out;
- RaftROS AutoPub follows identity changes and doesn't announce topics it can't publish;
- a flash size review against `main`;
- fixes found along the way (e.g. AutoPub double delete, generator crashes C6/C11/C12).

**Benefits:**
- Polling meets configured rates.
- New devices come online in milliseconds, and scanning doesn't steal poll time.
- Extended records cost far less memory, with limits raised from 20 to 32 without PSRAM and 256 with it.
- Base and extended records behave the same.
- Records can be disabled, overridden or loaded from a file without a rebuild.
- Device management is available over REST.
- Names are consistent.

All existing APIs, data and the devbin stream are unchanged; old names are kept as aliases.

## 3. Type records branch (record management phases 1a-5)

| Phase | Change | Aims |
|-|-|-|
| 1a | RaftCore's catalogue split into `devtypes/records/<Key>.json`. `DeviceTypeSources.json` selects records per SysType from named sources, with include, exclude and policy. Explicit `identPriority` where order mattered; the combined catalogue kept as a release asset | One file per record; per-SysType selection |
| 1b | Git sources pinned by a lock file (commit and SHA-256 per record), an offline cache, local override folders, source manifests, and decode code checked before it is compiled in | Records from other places |
| 2 | Axiom moved to a selection plus its own `devtypes/`; its 36-record fork deleted | Selection; no forks |
| 2b | `typeSettings` in SysTypes.json for board choices (bus speed and slots, interval, samples, rate, scan priority), layered under saved device settings. Axiom's LSM6DS copy replaced by a setting | Records describe devices, not boards |
| 3 | `devman/typefile/check`, a staged apply that checks before replacing, and per-entry outcomes. Type file build: entries are new, override, unchanged (left out) or policy, and records with no on-device decoder are flagged. raftjs wrappers | Runtime type files |
| 4 | `DeviceTypesTool.py` plus `raft devtypes` (list, add, remove, new, check, update, diff, typefile, push, pull, status). Push uploads, has the device check the file, confirms, applies and verifies after the restart | Upload by the raft CLI |
| 5 | `typeFile.fsImage` puts the type file in the file system image, built against everything the build compiles; the build saves its compiled records for the CLI. The source flag renamed `bundled` (old name accepted) | Factory-flashed type files |

**Benefits:**
- A record is reviewed and committed on its own.
- A project selects the records it needs without forking.
- Third parties can publish records that builds pin reproducibly.
- Board tuning no longer needs record copies.
- Type files are checked by the firmware's own rules before they are applied, can't brick identification, and can be pushed from the command line or shipped in a factory flash.

The "all records at runtime" setup is in place but needs the on-device decoder (D11, decision M8) to be fully useful.

## 4. Verification

- Host tests:
  - RaftCore `linux_unit_tests`, including grammar equivalence, fuzzing, sanitizer runs, catalogue index, poll plan, type file and settings tests;
  - 86 Python script tests;
  - raftjs 210 tests;
  - RaftCLI 82 tests.
- Hardware:
  - FeatherS3: polling, identification, type file check, apply and push, `fsImage` boot;
  - Axiom018: same 47 records and identification order, LSM6DS at 400 kHz on its slots, mux scanning, RSAO.

## 5. Not done, or still open

- Identification Stage 12: improvements to individual records (detection for 12 records, shadowing checks, ST time-of-flight records). This needs the devices on hardware.
- D11, the on-device decoder, for builds with all records at runtime (review M8 first).
- Other runtime routes for type files: SD, a single record over REST, BLE from an app.
- RaftCLI native transports (serial, WebSocket, BLE): planned in RaftCLI `devdocs/native-transports-plan.md`.
- Pull requests for all branches; wiki changes pushed when they merge.
