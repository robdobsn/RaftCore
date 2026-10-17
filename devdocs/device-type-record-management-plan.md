# Device Type Record Management: Plan

Status: proposal, 2026-10-17; decisions M1-M5, M7 and M9 agreed, M6 still proposed, M8 to be reviewed (§5).
Nothing here is implemented yet. It follows the device identification plan
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
  from another source, with a build message - Axiom's LSM6DS bus speed becomes `devtypes/LSM6DS.json` in Axiom. A
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

Transport: HTTP first (as `raft fs`); serial (RICREST file transfer) second, for devices reached over USB. The
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

---

## 4. Phases

| Phase | Work | Repos | Verify |
|---|---|---|---|
| **1a** | Make order explicit at RaftCore 0x26 and 0x29 (`identPriority`); split `DeviceTypeRecords.json` into `devtypes/records/`; `AssembleDevTypes.py` with `builtin` and `path` sources, the selection format (lock format designed in); keep `DEV_TYPE_JSON_FILES` and `DevTypes.json`; point RaftCore's tests and scripts at the assembled file; CI publishes the combined file as a release asset | RaftCore | Host tests of the assembler (selection, clashes, overrides, missing keys, compatibility with no selection file); the full selection gives the same identification order as today's catalogue (reference index test); every existing project builds unchanged |
| **1b** | `git` sources, lock file, cache, `raftdevtypes/` override, manifest | RaftCore | Host tests with a local bare repo (tag, branch, commit, single file, lock honoured, offline build from cache, broken override link fails) |
| **2** | Axiom moves to `DeviceTypeSources.json` + `devtypes/` (VL53L5CX, RSAO records, LSM6DS override) and deletes its fork | Axiom | Axiom018 builds; identification and records identical to today's on hardware (`devman/types`, `typeinfo full=1` per record) |
| **3** | `devman/typefile/check`; staged apply; type file build (3.6) with host tests | RaftCore, raftjs (wrappers) | Host tests; FeatherS3: stage, check, apply, verify; a bad file is reported and not applied |
| **4** | raft CLI `devtypes` commands (3.7), HTTP then serial | RaftCLI | Against the FeatherS3 (HTTP) and Axiom (serial) |
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
| M6 | Same key from two non-local sources | Proposed: an error unless `include` names the source |
| M7 | Check on the device before applying? | **Yes, `devman/typefile/check`** (additive endpoint) (decided 2026-10-17) |
| M8 | Schedule D11 (on-device decoder from the attribute list)? | Kept in the plan (phase 5); **reviewed before any work starts** (2026-10-17). Needed for the all-runtime setup |
| M9 | A combined catalogue file for consumers that read one | **Built by CI and published as a release asset; RaftCore's own tests use the assembled file** (decided 2026-10-17) |
