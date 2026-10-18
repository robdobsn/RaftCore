# Device Type Record Management: Plan

Status: decisions M1-M7 and M9-M12 agreed, M8 to be reviewed (§5). **Phases 1a, 1b, 2, 2b and 3 done (2026-10-17), phase 4 done
(2026-10-18)** on the `feature/device-type-records` branches of RaftCore, Axiom, raftjs and RaftCLI (see §6); phase 5
(optional items) not started. It follows the device identification plan
(`device-identification-improvement-plan.md`, Stages 0-11 done) and comes before its Stage 12 (record improvements),
which is mostly record-by-record edits that this plan makes easier.

Companion documents: `device-identification-improvement-plan.md` (the runtime type file is its Stage 6, the type
APIs Stage 10), `device-type-management-principles.md` (terms).

---

## 1. Aims

1. **One file per device type record**, so a record can be changed, reviewed and committed on its own, without
   merge conflicts in one large catalogue file.
2. **A per-SysType selection** of the records to compile in, so a project takes the records it needs instead of
   the whole catalogue or a fork of it.
3. **Records from other places**: project folders and git repos, pinned to a tag or commit, so third parties can
   define and maintain their own device types and a build can combine them with RaftCore's.
4. **Runtime type files built from the same description**, and uploaded to a device by the raft CLI, so records can
   be added or changed without rebuilding the firmware.
5. **All records at runtime** as a supported setup: a build with no compiled records, every record loaded from the
   type file.

Constraints (as for the identification plan): no break to existing projects, APIs or data formats; every change
covered by automated tests; RaftCore, Axiom and the other libraries on their `feature/...` branches until merged.

---

## 2. Starting point (2026-10-17)

| | |
|---|---|
| RaftCore catalogue | `devtypes/DeviceTypeRecords.json`: 49 records, 2843 lines, 45 commits |
| Axiom catalogues | `AxiomDevTypes/DeviceTypeRecords.json`: 36 records (57 commits); `RSAOTypeRecords.json`: 11 records |
| Axiom's fork | 35 of Axiom's 36 records are copies of RaftCore records. The differences: an extra `deviceType` field in all 35, `_notes` in 7, and LSM6DS `pollInfo.busHz`/`busHzSlots`. The copies are in a different order. Only VL53L5CX is Axiom's own. So the fork exists to choose a subset, set an order and tune one record |
| How a build selects records | `DEV_TYPE_JSON_FILES` in `features.cmake` (local paths; `/devtypes/...` resolves into RaftCore - `RaftBootstrapPhase2.cmake`), plus `systypes/Common/DevTypes.json` and `systypes/<SysType>/DevTypes.json` if present (`RaftProject.cmake`). Files are merged in order; a key in a later file replaces the earlier record, with a warning |
| Remote sources | None. Raft libraries can be fetched at a tag (`RAFT_COMPONENTS`, `raftdevlibs/` for local copies); device type files can't |
| Record order | Since Stage 7 candidates at an address are ordered by `identPriority`, then detection strength, then catalogue order. Sorting the records by key changes the order at 2 addresses in RaftCore's catalogue (0x26: TrillBar/M5-Weight; 0x29: CAP1188 moves ahead of VL53L0X/L1X/L4CD) and none in Axiom's |
| Runtime type file | `/local/devlib.json`, format `{"devTypes":{...}}` (the catalogue format); 64 KB limit; each record checked (D22); status `devman/typefile`, applied by a restart (`devman/typefile/apply`); `typepolicy … save=1` edits it on the device |
| Whole catalogue as a type file | RaftCore 36 KB minified (without `_notes`), Axiom 24 KB |
| Extended record limits | 32 records / 20 KB without PSRAM, 256 / 256 KB with PSRAM (`typeTable`); about 540 B per record |
| raft CLI | 1.24.1 (1.25.1 on robstudio): `raft fs put/get/list/...` over HTTP; no device type commands |

Things that read `devtypes/DeviceTypeRecords.json` directly: RaftCore `linux_unit_tests/Makefile` and its tests,
`scripts/tests` (`test_device_type_keys.py`, `test_grammar.py`, `test_standard_decode.py`),
`scripts/LibraryBuildScript.py` (PlatformIO), `scripts/RaftBootstrapLinux.cmake`, raftjs
`PseudocodeTranspiler.test.ts`, RaftI2C TestWebUI (`features.cmake`, `AttributeHandler.test.ts`), the
`features.cmake` of RaftROS ExampleDiscoverable and RaftMotorControl test rigs, and several devdocs.

---

## 3. Design

### 3.1 One file per record

- `devtypes/records/<Key>.json` holds exactly what is under `devTypes` today for that key (the record body). The
  key is the file name; a `deviceType` field, if present, must equal it (D13).
- `devtypes/records/<Key>.md` (optional) replaces `_notes`: the datasheet, the detection rationale and history.
  `_notes` stays accepted in the JSON.
- The body is unchanged, so a record copies straight into a runtime type file and back (`typeinfo … full=1`
  already gives it in this form).
- Any source of records uses the same layout: `records/<Key>.json`, `records/<Key>.md`, and an optional
  `raftdevtypes.json` manifest at its root: `{"name": "...", "formatVersion": 1, "minRaftCore": "x.y.z"}`.

**Decided:** the records stay in RaftCore (`devtypes/records/`), not a separate repo. Records use grammar features
and fields the firmware must understand, so they version with it, and RaftCore's CI already runs the grammar,
generator, round-trip and reduced-catalogue tests over them. Because a git repo can be a source (3.3), moving the
records to their own repo later needs no change to projects; that becomes worthwhile if records change much faster
than RaftCore releases or outside contributions grow.

### 3.2 Record order

Order in a file stops meaning anything. The order used is: `identPriority`, then detection strength (as now), then
the device type key (sorted, byte order). Where order matters today only through position, it is made explicit
with `identPriority` before the split - in RaftCore's catalogue, at 0x26 and 0x29; comments such as VL53L5CX's
"must stay after ... before ..." become priorities. The device type index follows the same order (it was only ever
valid for one build and boot).

### 3.3 Selection file: `DeviceTypeSources.json`

A new build-only file - not `SysTypes.json`, which is runtime configuration compiled into the firmware, and not
`DevTypes.json`, which already means a file of records. It is found as `DevTypes.json` is: `systypes/Common/` and
`systypes/<SysType>/`, the SysType's file extending and overriding Common's. The same file can also be used on its
own by the raft CLI (3.7).

```json
{
  "sources": {
    "raftcore": { "builtin": true },
    "local":    { "path": "devtypes" },
    "acme":     { "git": "https://github.com/acme/raft-devtypes.git", "ref": "v1.2.0", "path": "records" },
    "widget":   { "git": "git@github.com:bob/widget-fw.git", "ref": "3f2a9c1e7b...", "path": "devtypes/Widget.json" }
  },
  "builtin": {
    "include": ["raftcore:LSM6DS", "raftcore:MCP9808", "local:*"],
    "policy":  { "TrillRing": "disabled" }
  },
  "typeFile": {
    "include": ["acme:*", "widget:Widget"],
    "policy":  { "TrillRing": "disabled" },
    "fsImage": false
  }
}
```

- **Sources:**
  - `builtin`: RaftCore's records, at the RaftCore version the build uses;
  - `path`: a folder relative to the file, or a single record file;
  - `git`: any repo the user's git can reach (private repos over SSH use the user's credentials). `ref` is a tag,
    branch or commit; `path` is a folder of records or one record file - so "this file in that repo at that
    commit" is a source.
- **`builtin`** selects the records compiled into the firmware, **`typeFile`** those for the runtime type file
  (3.6). `include` takes `source:key`, `source:*`, or a bare key when only one source has it. `policy` sets
  `enabled`, `disabled` or `disabledClaim` (D23 entries). Either selection may be empty.
- **Overrides replace the whole record** (decided): a record with the same key in a `path` source replaces the one
  from another source, with a build message (Axiom's LSM6DS bus speed was first done this way in phase 2; it is now a
  type setting, 3.9, and Axiom uses RaftCore's record). A
  key from two non-local sources is an error unless `include` names the source to use, so a third-party repo can't
  silently replace a RaftCore record.

**Compatibility:** with no `DeviceTypeSources.json`, a build behaves exactly as now (`DEV_TYPE_JSON_FILES`,
`DevTypes.json`). Those stay valid when the file exists: they are added as a `path` source after the others, so
their records keep the current "later file replaces" behaviour.

### 3.4 Assembly

A RaftCore Python script, `scripts/AssembleDevTypes.py`, runs at configure time before the generator:

1. read the selection files and resolve the sources (3.5 for git);
2. apply the selection and overrides; report clashes; check each record (name equals key, the grammar, the
   generator's checks, duplicate hardware type IDs - N2);
3. write one merged `build/raft/DeviceTypeRecords.json` (records sorted as in 3.2), which the existing generator
   reads unchanged;
4. write `build/raft/DeviceTypeSources.manifest.json`: for each record, its source, commit and file hash.

Every record file and the selection files are build dependencies, so adding or changing one regenerates the
headers. The manifest can later be reported by `devman/types` (source of each record) - additive.

### 3.5 Git sources, lock file and cache

- **Lock file:** `systypes/<SysType>/DeviceTypeSources.lock` records the commit each source resolved to and the hash of
  every record file used. A build with a lock uses it, so a tag or branch can't change a build silently; the raft CLI
  refreshes it on request (`raft devtypes update`). It sits next to the existing `dependencies.lock`.
- **Cache:** fetched sources go to `build/raft/devtypes/<source>@<commit>` (shallow, limited to `path`), fetched
  again only when the pinned commit changes, so an offline build works once the cache is filled.
- **Local override:** `raftdevtypes/<source>` (a folder or symlink) replaces a git source, as `raftdevlibs/` does
  for libraries, with the same "broken link fails the build" check - so a third party can edit their records against
  a real build before committing.
- **Compatibility:** a record that needs a newer RaftCore than the build's is reported by the grammar and generator
  checks, by record and source; the manifest's `minRaftCore` gives a clear message first.
- **Trust:** a record is partly code - its decode pseudocode (`resp.c`) is translated to C and compiled into the
  firmware. Pinning by commit and the lock file's hashes are what make a third-party source safe to depend on; the
  pseudocode translator must reject anything outside its language (to be checked as part of this work).

### 3.6 Runtime type files

The `typeFile` selection builds a type file (`devlib.json`) from the same sources:

- records are minified, without `_notes`; the size, record count and extended-record bytes are checked against the
  target's limits (from `devman/typefile` when a device is given, otherwise the defaults);
- each record is reported as **new** (added with a new index), **override** of a compiled record (changes
  identification - shown clearly), **unchanged** (identical to the compiled record - left out), or **policy only**;
- **no on-device decoder**: a new record, or an override that changes the response layout, has no compiled decoder.
  raftjs and the WebUI decode it; DataLogger CSV, RaftROS (which doesn't announce it - N5) and named values don't.
  Reported for each such record;
- `fsImage: true` also puts it in the SysType's FSImage, so a factory flash carries it.

**Firmware addition (additive):** `devman/typefile/check?file=<path>` runs the loader's checks on a file without
applying it, and reports what `devman/typefile` would report (added, replaced, unchanged, disabled, rejected, first
error and warning) plus the records with no on-device decoder. Checks then always use the rules of the firmware the
device runs, not the developer's RaftCore. With it, an upload is:

1. upload to `/local/devlib.new.json`;
2. `devman/typefile/check?file=/local/devlib.new.json`;
3. if good: rename to the type file path, `devman/typefile/apply` (restart);
4. after reconnecting, `devman/typefile`: state applied, the CRC equals the uploaded file's.

`typepolicy … save=1` edits the device's file, so before overwriting it the tool compares the device's CRC with the
one it last uploaded, and offers to pull and compare.

**Other runtime routes** use the type file format as the common currency and the same loader checks (D22), through
the record source mechanism (`registerRecordSource`): a file on SD, a single record sent over REST and merged into
the type file, a file pushed by a phone app over BLE. Changes still apply at restart (Stage 8 decision).

### 3.7 raft CLI

The logic lives in RaftCore Python scripts, versioned with the firmware (and reusing `DeviceTypeGrammar.py`, which
shares test vectors with the C++ grammar); the CLI is a front end. A command works on a project (`-s SysType`), or
on a `DeviceTypeSources.json` anywhere with `--config <file>` (no project, no build needed; the lock file is kept next
to it).

| Command | Does |
|---|---|
| `raft devtypes list` | The resolved records for `builtin` and `typeFile`, each with its source and commit |
| `raft devtypes add/remove <source:key>` | Edit a selection (`--typefile` for the runtime one) |
| `raft devtypes new <key>` | Scaffold a record in a `path` source and check it |
| `raft devtypes check` | The grammar and generator checks, without a build |
| `raft devtypes update` | Refresh the lock file (fetch tags and branches again) |
| `raft devtypes diff` | Local overrides against the record they replace |
| `raft devtypes typefile` | Build `devlib.json` (3.6) and report sizes, kinds and records with no decoder |
| `raft devtypes push <device>` | Stage, check on the device, show what would change, confirm, apply, verify (3.6) |
| `raft devtypes pull <device>` | Get the device's type file and compare it with the project's |
| `raft devtypes status <device>` | `devman/typefile`, and whether the device's file is the project's |

Transport: HTTP (as `raft fs`), done in phase 4. Serial, WebSocket and BLE (RICREST file transfer) are a RaftCLI
project of their own: RaftCLI `devdocs/native-transports-plan.md`. The
restart in `push` needs a reconnect, for which raftjs's reconnect logic is the model.

A graphical tool is for later. The check endpoint means it needs only raftjs (it has `getTypeFileStatus`; it would
gain upload, check and apply wrappers): a WebUI page on the device, or a desktop or web app browsing sources. If
checking without a device becomes important, the C++ loader and grammar can be compiled to WebAssembly so a
browser uses exactly the firmware's rules.

### 3.8 All records at runtime

`"builtin": {"include": []}` with everything in `typeFile` is a supported setup. Already in place: an empty
compiled catalogue builds (the generator handles it), identification waits for the type file at startup (Stage 6),
and splitting records between compiled and file gives the same identification (`test_reduced_catalogue`). Size is
not a problem (RaftCore's whole catalogue is 36 KB as a type file, under the 64 KB limit). What to watch:

1. **On-device decoding (prerequisite).** With no compiled records, no device has an on-device decoder: DataLogger
   CSV, RaftROS and named values stop working, and only clients that decode (raftjs, the WebUI) see values. For
   this setup to be useful beyond streaming to an app, the firmware needs a decoder driven by the record's attribute
   list - **D11** of the identification plan. Standard attributes (types, scaling, offsets, lookup tables) are
   straightforward (raftjs does it in TypeScript); records with custom decode code (`resp.c`) need an interpreter or
   stay unsupported.
2. **RAM instead of flash.** About 540 B of RAM per record. Without PSRAM the default limits (32 records, 20 KB) are
   below RaftCore's 49 records, so such a board needs a larger `typeTable` and the internal RAM for it. The CLI
   checks the selection against the device's limits.
3. **No file, no devices.** A missing or bad file means nothing is identified. Keep system devices that firmware
   expects (`expectType`, e.g. Axiom's fuel gauge) built in even in an otherwise all-runtime build, and set
   `typeFileRequired: true` so a missing file is reported as an error.
4. **Device type indexes** are all extended ones, in file order - fine by contract (one boot), and raftjs clears its
   caches on reconnect.

### 3.9 Type settings: board and product choices outside the record

(Added 2026-10-17.) A record describes the device. Some of what records hold - or what had to be put in a copy of a
record - is a choice for the board or product the device is fitted to, not a property of the device:

| Setting | Why it isn't the device's | Where it goes |
|---|---|---|
| Poll bus speed and the slots it applies to (`busHz`, `busHzSlots`) | The wiring, pull-ups, multiplexer ports and other parts on the bus decide what is safe. Axiom kept a copy of the LSM6DS record only to add 400 kHz on slots 1-4 and 7 | `typeSettings` (below); no longer in records |
| Poll interval and samples kept | Defaults in the record (`pollInfo.i`, `s`); the right rate depends on the product (a data logger, a dashboard) | `typeSettings` (`intervalUs`, `numSamples`) |
| Sample rate set through a `_conf.rate` action | The record lists what the device can do (`map`); which one is used is a choice | `typeSettings` (`sampleRateHz`) |
| Scan priority | Whether the device is expected to be hot-plugged depends on the product (54 of 62 records say "high") | `typeSettings` (`scanPriority`) |

**`typeSettings`** is a DevMan setting in the SysType's configuration (`SysTypes.json`), next to `typePolicy`:

```json
"DevMan": {
    "typeSettings": {
        "LSM6DS": { "busHz": 400000, "busHzSlots": "1,2,3,4,7" },
        "VL6180": { "intervalUs": 50000, "scanPriority": "low" }
    }
}
```

- Each entry uses the names `devman/devconfig` takes (`intervalUs`, `numSamples`, `busHz`, `busHzSlots`,
  `sampleRateHz`). It is applied when a device of the type is identified, through the same path as the devconfig
  settings DeviceManager keeps for a device (Stage 7). A type setting is not remembered or saved as the device's
  own setting.
- **Layers**, each overriding the one before: the record's defaults; `typeSettings`; the device's saved settings
  (Stage 10, `save=1`); a `devconfig` sent while running.
- **`scanPriority`** (`high`, `medium`, `low` or `none`) isn't a device setting. It is set on the type, as
  `typePolicy` is: the catalogue index (the scanner's priority lists) uses it in place of the record's.
- **Why the SysType's runtime configuration** and not the record or `DeviceTypeSources.json`:
  - SysTypes is where the board is described (bus pins, multiplexers, `typePolicy`);
  - it applies to compiled records and to records from a type file alike, which matters for builds with no compiled
    records;
  - it can be changed without a rebuild;
  - records stay the same for every board, and `DeviceTypeSources.json` only chooses records.
- **Timing:** settings apply just after identification (as saved devconfig settings do), so the first polls may run
  with the record's defaults. Initialisation runs at the bus's default speed (as before - the bus speed applies to
  polls).
- **Checking:** at startup each entry is checked. Unknown setting names and invalid values are logged and ignored. A
  key that isn't a device type in this build is logged at the information level, since a type file may not be
  present.
- **Records no longer take `pollInfo.busHz`/`h` or `busHzSlots`/`hSlots`.** Only Axiom's copy of LSM6DS used them,
  and it moves to `typeSettings`. The generator and the type file loader warn about a record that still has them.
- **Two places for a policy:** `builtin.policy` in `DeviceTypeSources.json` sets a record's initial policy in the
  build ("this product never has it"); DevMan `typePolicy` sets it at runtime (a board that can be reconfigured).
- **Later:** `maxBusHz` in a record's `devInfoJson`, so a type setting faster than the device supports is reported.

---

## 4. Phases

| Phase | Work | Repos | Verify |
|---|---|---|---|
| **1a** | Make order explicit at RaftCore 0x26 and 0x29 (`identPriority`); split `DeviceTypeRecords.json` into `devtypes/records/`; `AssembleDevTypes.py` with `builtin` and `path` sources, the selection format (lock format designed in); keep `DEV_TYPE_JSON_FILES` and `DevTypes.json`; point RaftCore's tests and scripts at the assembled file; CI publishes the combined file as a release asset | RaftCore | Host tests of the assembler (selection, clashes, overrides, missing keys, compatibility with no selection file); the full selection gives the same identification order as today's catalogue (reference index test); every existing project builds unchanged |
| **1b** | `git` sources, lock file, cache, `raftdevtypes/` override, manifest | RaftCore | Host tests with a local bare repo (tag, branch, commit, single file, lock honoured, offline build from cache, broken override link fails) |
| **2** | Axiom moves to `DeviceTypeSources.json` + `devtypes/` (VL53L5CX, RSAO records, and an LSM6DS override - replaced by a type setting in 2b) and deletes its fork | Axiom | Axiom018 builds; identification and records identical to today's on hardware (`devman/types`, `typeinfo full=1` per record) |
| **2b** | `typeSettings` (3.9) in DeviceManager, the scan priority override in DeviceTypeRecords, records' `busHz`/`busHzSlots` removed (with a warning); Axiom's LSM6DS back to RaftCore's record, its bus speed in Axiom018's `typeSettings` | RaftCore, RaftI2C (if needed), Axiom | Host tests (parsing, checking and layering of type settings; scan priority override in the index); FeatherS3; on the Axiom, LSM6DS polled at 400 kHz on its slot as before (`devconfig` query, poll bus time) |
| **3** | `devman/typefile/check`; staged apply; type file build (3.6) with host tests | RaftCore, raftjs (wrappers) | Host tests; FeatherS3: stage, check, apply, verify; a bad file is reported and not applied |
| **4** | raft CLI `devtypes` commands (3.7) over HTTP | RaftCore (`DeviceTypesTool.py`), RaftCLI | Against the FeatherS3 (HTTP) |
| **5** | Optional: `fsImage`, other runtime routes, D11 on-device decoder (prerequisite for 3.8), GUI | as needed | Per item |

Stage 12 of the identification plan (record improvements) can start after phase 1a, one record per commit.

---

## 5. Decisions

| # | Question | Decision |
|---|---|---|
| M1 | Records in RaftCore or their own repo? | **RaftCore** (`devtypes/records/`) for now; a repo later is possible without changing projects (decided 2026-10-17) |
| M2 | Selection in its own JSON file or in `features.cmake`? | **Own JSON file**, `DeviceTypeSources.json`, so the CLI can edit it safely (decided 2026-10-17) |
| M3 | Overrides: whole record or field patches? | **Whole record** (decided 2026-10-17) |
| M4 | Name of the selection file | **`DeviceTypeSources.json`**, for consistency with `DeviceTypeRecords.json` (decided 2026-10-17) |
| M5 | Record order | **`identPriority`, strength, then key; position never matters** (decided 2026-10-17) |
| M6 | Same key from two non-local sources | **An error unless `include` names the source** (decided 2026-10-17) |
| M7 | Check on the device before applying? | **Yes, `devman/typefile/check`** (additive endpoint) (decided 2026-10-17) |
| M8 | Schedule D11 (on-device decoder from the attribute list)? | Kept in the plan (phase 5); **reviewed before any work starts** (2026-10-17). Needed for the all-runtime setup |
| M10 | Board and product choices (bus speed, poll interval and samples, sample rate, scan priority) | **In DevMan `typeSettings` in the SysType's configuration, applied when a device of the type is identified, under the device's own settings** (decided 2026-10-17) |
| M11 | Keep `busHz`/`busHzSlots` in records? | **No** - removed, with a warning for a record that still has them (decided 2026-10-17; only Axiom used them) |
| M12 | `maxBusHz` in the record | Later (decided 2026-10-17) |
| M9 | A combined catalogue file for consumers that read one | **Built by CI and published as a release asset; RaftCore's own tests use the assembled file** (decided 2026-10-17) |

---

## 6. Progress

### Phase 1a (done 2026-10-17)

- **Order made explicit:**
  - 0x26: M5-Weight `identPriority -1`;
  - 0x29: VL6180 `2`, VL53L0X/L1X/L4CD `1`.

  With these, sorting by key gives today's candidate order at every address and the same scan lists (checked with
  `reference_catalogue_index.py`).
- **Split:**
  - `devtypes/DeviceTypeRecords.json` is now `devtypes/records/<Key>.json` (49 files);
  - `AssembleDeviceTypes.py --split` keeps each record as written and checks that it parses to the original;
  - the generator's headers from the folder are byte-identical to those from the original catalogue sorted by key.
- **`scripts/AssembleDeviceTypes.py`:**
  - loads records from a folder (key order), a record file or a catalogue file;
  - handles `DeviceTypeSources.json` with `builtin` and `path` sources, include/exclude/policy, local override, M6
    and Common + SysType merging;
  - writes the assembled catalogue and the manifest;
  - has `--all` (every RaftCore record as one file) and `--split`.

  `git` sources give "not supported yet". The `typeFile` section is read, but not yet used by the build.
- **Build (`RaftBootstrapPhase2.cmake`):**
  - `DeviceTypeSources.json` in `systypes/Common` and the SysType is assembled at configure time;
  - an error stops the configure with the message;
  - selection files and record files re-run it;
  - the assembled file comes before `DEV_TYPE_JSON_FILES` and `DevTypes.json`;
  - `/devtypes/...` together with a selection is an error;
  - `/devtypes/DeviceTypeRecords.json` resolves to the records folder, and folders are allowed in
    `DEV_TYPE_JSON_FILES`.

  The generator reads folders; files keep their old meaning, so a file without `devTypes` has no records.
- **Other consumers:**
  - `RaftBootstrapLinux.cmake` and `LibraryBuildScript.py` (PlatformIO) read the folder;
  - RaftCore's host tests assemble `gen/DeviceTypeRecords.json`;
  - `check_device_type_grammar.py` and the Python tests read the folder;
  - the release workflow attaches `DeviceTypeRecords.json` (M9).
- **Tests:**
  - `scripts/tests/test_assemble_device_types.py`, 15 tests: loading, every selection rule, errors, policies,
    merging, output and manifest, split, and RaftCore's records assembled = folder;
  - all host tests pass.
- **Firmware builds:**
  - Axiom018: the generated header is byte-identical;
  - RaftROS FeatherS3 example (uses `/devtypes/DeviceTypeRecords.json`): the same 53 records, RaftCore's in key
    order; on the board both devices are identified with the same candidate order as before, and ROS publishes them;
  - a scratch copy with a `DeviceTypeSources.json` (3 RaftCore records, TrillRing disabled, a local record) on the
    FeatherS3: `devman/types` showed the 8 expected types with TrillRing disabled, both devices were identified, and
    TrillRing was no longer a candidate;
  - editing a record and adding a record file each regenerated the headers, and a bad key stopped the configure with
    its message.
- **Docs:**
  - wiki page *Selecting Device Types* (`DeviceTypeSources.md`);
  - *Adding an I2C Device Type*, *Device Type Record Format*, *SysTypes*, *I2C Device Identification and
    Polling* and Home updated;
  - `devtypes/README.md`.

### Phase 1b (done 2026-10-17)

- **Git sources** (`AssembleDeviceTypes.py`):
  - spec `{"git", "ref", "path"}`; `path` defaults to `records`; `ref` is a tag, branch or full commit hash (a short
    hash is an error with a hint);
  - fetch: shallow, with `--filter=blob:none` and a sparse checkout of `path` and `raftdevtypes.json`. Each step
    falls back when the server doesn't allow it, including a plain fetch when contents can't be fetched later by
    hash (a real problem found by the tests with protocol v0);
  - cached by commit in `build/<SysType>/raft/devtypes/<source>-<url hash>/<commit>`.
- **Lock** (`systypes/<SysType>/DeviceTypeSources.lock`, format 1):
  - it records each source's `git`, `ref` and `commit`, and each git record's `source`, `commit` and `sha256`;
  - a source whose `git` and `ref` are unchanged uses the locked commit: by hash, or (if the server refuses) by the
    ref, which must still be the locked commit, otherwise an error says to update;
  - `--update` resolves again; a new or changed source updates the lock with a message;
  - a cached record that differs from the lock is an error;
  - no lock is written for a selection without git sources.
- **Local copy:** `raftdevtypes/<source>` replaces a git source (message); a broken link is an error.
- **Source manifest:** `raftdevtypes.json` `formatVersion` (1) and `minRaftCore` (against RaftCore's
  `library.json` version) are checked.
- **Decode code** (the trust item in 3.5): `PseudocodeHandler.check_code()` allows only the decode language:
  - `buf`, `out.<attribute>`, the code's declared variables, integer type names (also as casts);
  - `if`/`else`/`while`/`next`/`return`, numbers and operators.

  Characters the lexer would have dropped, other names and function calls are problems. A record from a git source
  with any is rejected. For other records the generator reports `DECODE CODE WARNING`, so existing builds don't
  break; all 10 records with decode code in RaftCore, Axiom, RSAO and the RaftI2C tests pass. To do later:
  - make it an error everywhere, after a release with the warnings;
  - checks the language can't express: bounds of `buf` indexes, and loops that don't end.
- **Build:**
  - `RaftBootstrapPhase2.cmake` passes `--cache-dir`, `--lock` and `--override-dir`;
  - the lock file is an input, so editing it re-runs the selection;
  - a glob on `raftdevtypes/*` re-runs it when a local copy is added or removed.
- **Tests:**
  - `test_assemble_git_sources.py` (10, with local bare repos): tag, branch, commit and single file; a moved tag
    keeps the lock; refetch by hash; `--update`; offline from the cache; a server without fetch by hash (protocol
    v0); a tampered cache; the local copy and broken link; the source manifest; decode code from git rejected;
    RaftCore + git key clash (M6); no lock without git sources;
  - `test_decode_code_check.py` (4);
  - all host tests pass.
- **Checked by hand:**
  - from GitHub: RaftCore at `v1.58.1`, path `devtypes/DeviceTypeRecords.json`, in about 1.5 s with a 312 KB cache
    (only `devtypes` checked out);
  - a FeatherS3 firmware build in the Docker build container with a git source (a local bare repo): fetched, locked
    and compiled in; repeat builds don't reconfigure; a moved tag kept the locked record; a new `raftdevtypes/acme`
    was picked up.

### Phase 2 (done 2026-10-17, Axiom)

- **The selection:**
  - Axiom018 selects its records in `systypes/Axiom018/DeviceTypeSources.json`: 34 RaftCore records by key, and
    `axiom:*` (Axiom's `devtypes/`, 13 files);
  - 29 of Axiom's copies were identical to RaftCore's apart from `deviceType` and `_notes`, and 5 differed only by
    RaftCore's explicit `identPriority` (phase 1a), so they now come from RaftCore;
  - `devtypes/` holds LSM6DS (Axiom's `busHz`/`busHzSlots`), VL53L5CX and the 11 RSAO records;
  - `AxiomDevTypes/`, with its one-off split scripts, is removed.
- **Checked:**
  - the same 47 records, the same candidate order at every address and the same scan lists (reference index);
  - the generated record code is identical apart from the 5 `identPriority` values, and every poll struct is the
    same; the binary is 16 bytes smaller;
  - on the Axiom (serial console), compared with the firmware before:
    - `typeinfo … full=1` for all 47 records is the same, apart from the 5 `identPriority` values and `dtIdx`;
    - `devman/types` has the same keys, sources and policies, and `listdevs` is the same;
    - LSM6DS, VL6180 and VL53L5CX are identified, with the same candidate order.
- **Seen on the way** (not changed): the serial console drops a response of about 8 KB (`typeinfo … full=1` for
  RoboticalVCP and RoboticalLiquidSensor, records of 3.4-3.9 KB) without an error.

### Phase 2b: type settings (done 2026-10-17)

- **`DeviceManager/DeviceTypeSettings.h`** (host-tested):
  - reads DevMan `typeSettings`: values as numbers or text, `busHzSlots` as a list or text; names starting with `_`
    are notes;
  - checks each setting the way `devconfig` does; problems are logged at startup and left out;
  - `layer()`: the device's own settings replace the type's.
- **DeviceManager:**
  - `reapplyDevConfig()` (on Identified and TypeChanged, and after re-verify and reinit) now applies the type's
    settings under the device's remembered or saved ones;
  - settings applied this way are not remembered as the device's own, so `save=1` saves only the device's settings;
  - scan priorities are set on the types at setup, and again after the type file loads; at that point keys that
    aren't device types are logged at the information level.
- **DeviceTypeRecords:**
  - `setScanPriority(key, level)` and `getScanPriority(idx)` (by index, like policies; a change starts a new
    generation, so the scanner rebuilds its lists);
  - `CatalogueIndex::build` takes the effective scan priority.
- **Records:**
  - `pollInfo.busHz`/`h`/`busHzSlots`/`hSlots` are no longer read;
  - the generator and the type file loader warn about a record that has them.
- **Axiom:**
  - `devtypes/LSM6DS.json` removed, and `raftcore:LSM6DS` selected;
  - Axiom018 `SysTypes.json` DevMan `typeSettings.LSM6DS` holds `busHz` 400000 and `busHzSlots` "1,2,3,4,7".
- **Tests:**
  - `test_device_type_settings` (22 checks);
  - `test_catalogue_index`: type scan priority (high, low and none for an extended record, none for a base record; the
    record itself unchanged; errors);
  - generator and type file loader warnings for record bus speed;
  - all host tests pass.
- **Hardware:**
  - **Axiom:** the LSM6DS gets the type settings when it is identified (boot log). `devconfig` reports 400 kHz on
    slots 1, 2, 3, 4 and 7, as before. The bus time per poll is 3874 us against 3731 us before; at 100 kHz it would
    be about four times longer.
  - **FeatherS3** (scratch copy with `typeSettings` for MPU6050 and APDS9960):
    - the type settings applied (20 ms interval, 3 samples);
    - a device interval set over REST took precedence and was kept through re-identification;
    - `save=1` saved only the device's interval;
    - after a restart: the saved device interval (40 ms) with the type's 3 samples; with the file removed, the
      type's 20 ms.

### Phase 3: runtime type files (done 2026-10-17)

- **The loader** (`DeviceTypeFileLoader`, host-tested):
  - `Result.records`, filled when `keepRecordOutcomes` is set: per entry, `key`, outcome (added, replaced,
    unchanged, disabled, rejected), `error`, `warnings`, and `noDecoder` (a record with data but no on-device
    decoder);
  - `checkJson()` applies a file to a scratch `DeviceTypeRecords`: the compiled records, the device's records
    registered by code (not those from its current type file), protected types, ext data sources and the same
    limits - what a restart gives - and leaves the device's table unchanged;
  - new accessors `getProtectedKeys()` and `getExtDataSources()`.
- **Endpoints:**
  - `devman/typefile/check?file=<path>` (default: the type file): file, bytes, CRC, `valid`, counts, first error and
    warning, `entries`;
  - `devman/typefile/apply?file=<path>`: checks first; if nothing is rejected, the file is moved to the type file's
    path and the device restarts, otherwise `failCheck` with the check's fields and nothing changes;
  - `devman/typefile?entries=1`: the startup outcomes, which are kept from the load. The startup load also records
    the keys the file added or replaced, which a check leaves out of its scratch table;
  - a query after `devman/typefile` is no longer taken as an action.
- **Type file build** (`AssembleDeviceTypes.py --typefile`, host-tested):
  - from the `typeFile` selection, against the compiled records (the `builtin` selection, or `--compiled
    <catalogue>`);
  - each record is new, override or unchanged (left out); policies for compiled types become `{"disable": ...}`
    entries (a type file can't enable a compiled type);
  - `noDecoder` uses the firmware's rule (the compact `resp` JSON differs, or the record is new);
  - minified, without `_notes`, within `--max-bytes` (64 KB); `--report` writes JSON.
- **raftjs:**
  - `getTypeFileStatus(entries)`, `checkTypeFile(file?)` and `applyTypeFile(file?)`, with the types
    `DevManTypeFileEntry` and `DevManTypeFileCheck`;
  - 210 tests pass.
- **Tests:** `test_device_type_file` `testCheckJson` (17 checks: the device's table unchanged; the current file's
  record added again; an override keeps its decoder with the same layout and has none with a new one; conflict
  with a code record; protected; policy entry; unknown policy; ext data source), and 4 type file build tests. All
  host tests pass.
- **FeatherS3:**
  - a file with a bad record: check reported it per entry, and apply refused it (the staged file was left as it
    was);
  - a good file: check, then apply from `/local/devlib.new.json`, then restart. The status after the restart had the
    same CRC and the same outcome for each entry as the check, and ROS still published both devices (the MPU6050
    override kept its decoder);
  - a type file built by `AssembleDeviceTypes.py --typefile` for the FeatherS3's compiled records: the device's
    check agreed with the script for every entry (new with no decoder, override, policy), and the unchanged record
    was left out;
  - the board was left with no type file.

### Phase 4: raft CLI over HTTP (done 2026-10-18)

- **`scripts/DeviceTypesTool.py`** (RaftCore), the logic behind `raft devtypes`, so the rules are those of the
  RaftCore a project builds with: `init`, `list`, `add`, `remove` (a record selected by a wildcard is excluded), `new`
  (a record file in a path source, to fill in - the check is `raft devtypes check`), `check` (the generator's checks
  on the compiled selection, and the grammar and decode code checks on type file records), `update`, `diff` (local
  copies against what they replace, and type file overrides against the compiled record) and `typefile` (with the
  file's CRC as the device reports it). `--json` for `list`, `check` and `typefile`. An `add` that doesn't resolve
  leaves the file as it was. 10 host tests.
- **Assembler:** the `typeFile` section includes nothing unless it says so (`builtin` still includes everything), and
  an assembly records the records each local copy replaced (for `diff`).
- **RaftCLI `raft devtypes`** (alias `dt`; branch `feature/device-type-records`): the commands above run the tool in
  the project's RaftCore (`--raftcore`, else `raftdevlibs/RaftCore`, else `build/<SysType>/_deps/raftcore-src`);
  selection files are Common's and the SysType's (`--common` to change Common's), or `--config <file>` with its lock
  next to it. Device commands:
  - `push <ip>`: builds the file within the device's `maxFileBytes`; does nothing if the device already has it
    (`--force`); warns if the device's file (its current CRC, from a check of its own file) isn't the one last pushed
    from here (kept in `build/<SysType>/raft/devtypes-pushed.json`); uploads `devlib.new.json`; shows the device's
    check; refuses if not valid or the CRC differs; asks (`--yes`), or stops and removes the upload (`--dry-run`);
    applies; waits for the device; checks it loaded the file (state applied, same CRC, nothing rejected);
  - `pull <ip>`: downloads the file, and lists the keys that differ from the project's;
  - `status <ip>`: the startup outcomes, limits, whether a restart is due and whether the file is the project's.
- **Serial transport** (and WebSocket and BLE) is planned in RaftCLI `devdocs/native-transports-plan.md`.
- **FeatherS3:** a `--config` selection with a new record (no decoder), an override and a policy. The dry run's device
  check agreed with the tool, and the upload was removed. The push applied and was verified after the restart. A
  second push did nothing. `pull` after a local change listed the changed key. A file changed on the device by
  `raft fs put` gave the warning on the next push. The board was left with no type file.
- **Axiom project:** `list`, `check` and `diff` on Axiom018's selection (47 records, the 3 known grammar warnings).
