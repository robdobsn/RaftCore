# Device Identification and Device Type Management: Staged Improvement Plan

Status: plan agreed 2026-10-15 (decisions in §8). Stages 0 to 11 done on the `feature/device-identification`
branches (Stages 0-5 2026-10-15, 6-10 2026-10-16, 11 2026-10-17; see each stage's status).

Companion documents:
- `device-type-management-principles.md`: the principles (P1–P15) and the canonical terms. Cited as *(P#)*.
- `device-identification-review.md`: the defect-level review. Findings are cited by their IDs (C#, I#, A#),
  and sections as *(review §#)*.

---

## 1. Purpose

This document turns the goals below into a sequence of stages. Each stage can be merged and tested on its
own. Each stage either fixes known defects or adds a capability, and it leaves the system working.

The plan covers RaftCore (type table, DeviceManager, generator), RaftI2C (scanner, identification,
polling), and the consumers that are affected (raftjs, RaftROS, Axiom).

### 1.1 Goals

| # | Goal |
|---|---|
| G1 | Keep base device type records mainly in flash |
| G2 | Hold extended device type records memory-efficiently. Raise the limit of 20 if possible |
| G3 | Work on ESP32 parts with and without PSRAM, with higher limits when PSRAM is present |
| G4 | Handle device type records as efficiently as possible in processor time |
| G5 | Allow base records to be disabled at runtime, for example by an extended record carrying a disable flag |
| G6 | Load extended records from a file at startup, and do not identify devices until it is loaded. When the file changes, either reset the system or restart identification |
| G7 | Do the derivations that `ProcessDevTypeJsonToC.py` does today (address map, scan lists) at startup and whenever the record set changes, so base and extended records are treated the same |
| G8 | Find new or changed devices quickly without taking significant bus time from polling |
| G9 | Make polling meet every device's polling requirement, including the expected rate |
| G10 | Provide finer-grained device management through improved DeviceManager APIs |
| G11 | Make the naming of IDs, indices, keys and names consistent |

### 1.2 How the stages are ordered

1. **Fix what users already see.** The poll rate and presence errors come first (Stage 1). They are small,
   local changes.
2. **Lay the foundations before the features.** Storage (Stage 2), the runtime-built index (Stage 3) and the
   single parser (Stage 4) are what disabling records, loading records from a file and identifying devices
   per element all depend on.
3. **Make extended records first-class, then load them from a file** (Stages 5–6).
4. **Then the identification model, live reload, scanning, the APIs and the wire-level names** (Stages 7–11).
   Each depends on the stages before it.
5. **Improvements to the records themselves** (Stage 12) come after the identification model, which some of
   them need.

Naming (G11) is decided once, in Stage 0. Each later stage then uses the new names for the code it touches.
Stage 11 handles the remaining renames that clients can see.

### 1.3 Way of working

- **One branch per repository for the whole plan:** `feature/device-identification` in each repository that
  changes (RaftCore, RaftI2C, RaftSysMods, RaftROS, raftjs, and others as needed). Every stage is committed
  there, as one or more commits per stage. Pull requests are opened when the work (or an agreed group of
  stages) is complete, not per change.
- **Keep the branches current:** merge `main` into them regularly, so the final pull requests don't carry large
  conflicts.
- **Wiki:** the wiki has no pull requests. Its changes are committed locally and pushed when the matching
  pull requests are merged, so the wiki never describes behaviour that isn't on `main`.
- **Every stage keeps `make -C linux_unit_tests check` passing** (and the RaftROS and raftjs tests where they
  are touched), and records its hardware checks.

---

## 2. Starting point (2026-10-15)

### 2.1 Already on `main`

| Item | Where |
|---|---|
| C1: `rN` heap overflow bounded | RaftCore |
| C2: generated decoder shift direction | RaftCore (`DecodeGenerator.py`, with a regression test) |
| C3: an unprefixed detection value is now invalid and makes the record unmatchable; LTR-329, LTR-390 and M5-Weight data fixed | RaftCore and RaftI2C (`843f586`) |
| C4: LTR `=p20` init data | RaftCore data. The parser still accepts a bare `pNN` as write data |
| C14: `appendRawField`; null-bus guards; `listdevs` filter | RaftCore |
| I12: every queued request gets a callback with a result code | RaftI2C |
| Deferred REST responses (`cmdraw` and `cmdjson` no longer block the main loop) | RaftCore, RaftI2C, RaftWebServer, RaftSysMods |
| NACKed write left the bus unusable (bus resync after a partial access) | RaftI2C |
| Console logging no longer stalls the main loop when nothing reads the console | RaftCore, RaftSysMods |
| `SpiramAwareAllocator` detects PSRAM on ESP-IDF 5+ | RaftCore |
| Binary detection values parsed as hex (§10.8) fixed, with a test of every base record (PR #12, 2026-10-15) | RaftCore |
| Linux unit tests: one program per area, all checking real code, run by CI on pushes to `main` and on PRs (`make -C linux_unit_tests check`) | RaftCore |
| `DeviceNames` class (device names and roles) moved out of DeviceManager | RaftCore |
| `RaftDeviceID::fromString` rejects malformed IDs; `ProtocolExchange` reports combined activity of all sessions; a stream to an unknown endpoint is refused | RaftCore |

### 2.2 Still open, by area

| Area | Open findings |
|---|---|
| Type table and generator | C5 (`s` defaults to 0), C6, C7, C8, C9, C10, C11, C12, C13, C15; 276 B of index lists in DRAM (the larger tables are already in flash, §2.4); extended records re-parse address strings on every lookup; name lookup is a linear `strcmp` |
| Identification | I3, I4, I9, I10, I11, I17; ordering shadows records (review §5.2); no identity-change event (A1) |
| Presence and scanning | I1, I2, I7, I8, I16; scan lists are a snapshot taken in `BusScanner::setup` (review §5.16) |
| Polling | I5, I6, I14; the poll-rate drift described in §2.3 |
| Extended records | Cap of 20, shared with static devices; no disable; override leaves two indices for one name; no file loader on `main` |

### 2.3 Why poll rates are slower than expected

Found while preparing this plan, by reading the code. Items 1 and 3 are confirmed on hardware (§2.4); the pass
period is about 8–12 ms rather than 5 ms, because a pass's own work is added to the 5 ms delay.

1. **Interval measured from when the poll ran, not from when it was due.**
   - `DeviceStatus::getPendingIdentPollInfo()` sets `lastPollTimeUs = timeNowUs` when a poll is found due.
   - The bus worker runs one pass, then calls `vTaskDelay(5 ms)` (`I2C_BUS_LOOP_YIELD_MS`). A pass therefore
     takes 5 ms plus however long the pass's work takes.
   - A device is polled at the first pass after it falls due, and its next interval is measured from that
     moment. The lateness of each poll is therefore added to every period. It is never recovered.
   - Expected effect: the actual period is the interval rounded *up* to a whole number of passes. For
     example:
     - `i=10` gives about 10.5–11 ms;
     - `i=25` gives about 26–31 ms;
     - `i=100` gives about 101–106 ms.

     The rate is 2–20% low, and the error grows as the interval gets shorter.
2. **Multi-step polls lose their pause time.**
   - A step ending in `pN` splits the poll. Each later step sets `lastPollTimeUs` again.
   - The next full poll is then timed from the *last step*, not from when the poll started.
   - So the period becomes the interval, plus the sum of the pauses, plus the lateness from item 1.
3. **One poll per pass, first due by list position** (I5).
   - When several devices are due together, all but the first wait at least one more pass (5+ ms).
   - The device that waits is always the one later in `_addrStatus`, so the error is not shared fairly.
4. **No interval means poll every pass.** A missing `i` defaults to 0, so that device is due on every pass.
   This makes item 3 worse.

### 2.4 Baseline measurements (Stage 0, 2026-10-15)

Measured on the FeatherS3 bench (MPU6050 at 0x68 and APDS9960 at 0x39 on an I2C bus at 100 kHz), with the
`SysTypeFeatherS3C3Test` firmware built from `feature/device-identification` (RaftCore `46e678b` plus the Stage 0
statistics). Tool: `scripts/bench/device-bench.js`, 20 s per scenario, two runs, each after a reboot. The runs
agree to within about 0.5% (raw results: `bench-results/2026-10-15-feathers3-baseline-run{1,2}.json`).

**Poll rate against the configured rate** (mean of the two runs; delivered = rate a raftjs client received):

| Scenario | Device | Configured | Polled | Error | Max interval | Delivered |
|---|---|---|---|---|---|---|
| defaults | MPU6050 | 20 Hz (50 ms) | 18.48 Hz | −7.6% | 71 ms | 18.52 Hz |
| defaults | APDS9960 | 10 Hz (100 ms) | 9.50 Hz | −5.0% | 116 ms | 9.49 Hz |
| all 100 ms | both | 10 Hz | 9.64 Hz | −3.6% | 110 ms | 9.66 Hz |
| all 50 ms | both | 20 Hz | 18.72 Hz | −6.3% | 62 ms | 18.77 Hz |
| all 25 ms | both | 40 Hz | 33.47 Hz | −16.3% | 38 ms | 33.56 Hz |
| all 10 ms | MPU6050 | 100 Hz | **9.65 Hz** | **−90%** | 461 ms | 9.62 Hz |
| all 10 ms | APDS9960 | 100 Hz | 78.09 Hz | −22% | 26 ms | 78.33 Hz |
| mixed | MPU6050 at 10 ms | 100 Hz | 74.05 Hz | −26% | 31 ms | 74.22 Hz |
| mixed | APDS9960 at 100 ms | 10 Hz | 9.44 Hz | −5.6% | 117 ms | 9.49 Hz |

**Findings:**
1. **Every device polls slower than configured** (§2.3 item 1), by 3.6% at 100 ms rising to 16% at 25 ms. The
   mean interval is the configured one plus 3.5–5 ms, about half a worker pass.
2. **Starvation** (§2.3 item 3, review I5). With both devices at 10 ms, the MPU6050 gets 9.6 Hz while the
   APDS9960 gets 78 Hz, because the scheduler always takes the first due device in its list. Its longest gap
   was 461 ms, with about 190 missed periods in 20 s.
3. **One poll per pass caps the bus at the pass rate.** The worker runs 86–125 passes per second (8–12 ms each,
   not the 5 ms delay alone), so at most about 88 polls per second were made when both devices wanted 100 Hz.
   A single device at 10 ms reaches only 74 Hz.
4. **Scanning is the largest single cost.** With nothing changing on the bus, the scanner made 340–590 probes
   per second and took 21–29% of the worker's time (213–294 ms per second), more than polling at the default
   rates (12%). Scanning shrinks as polling grows, because it uses the time left in each pass. This is
   input for Stage 9.
5. **The worker is busy 39–58% of the time** (all parts of a pass, excluding the 5 ms delay).
6. **Clients receive what is polled** (delivered within 1% of polled), so these figures are not limited by
   publishing or the network.
7. **Identification after boot:** the APDS9960 was seen at 171–194 ms after the bus started, online at
   295–320 ms and identified at 360–384 ms; the MPU6050 was seen at 617–626 ms, online at 641–650 ms and
   identified at 653–664 ms. Coming online takes two successful probes, about 25 ms apart for the MPU6050 but
   about 125 ms for the APDS9960.
8. **Main loop:** average 0.9–3.2 ms, longest pass about 16–18 ms.

**Memory** (from the firmware's per-file size report and symbol table):
- The base record array (`baseDevTypeRecords`, 2,120 B), the per-address index (512 B) and counts (128 B) are
  already in flash: although the generated code doesn't declare them `const`, GCC places these never-written
  arrays in `.rodata`. Only the per-address candidate lists (`baseDevTypeIndexByAddr_0x..`, 276 B in total) are
  in RAM. §4's "~2.5 KB DRAM" was wrong; making the tables `const` (Stage 2) saves about 276 B and makes the
  flash placement guaranteed rather than an optimisation.
- The Stage 0 statistics add 72 B (84 B since Stage 1 added the bus time per poll) to each bus element's
  `DeviceStatus` (one per address seen on a bus) and about
  200 B per I2C bus.
- Extended records were not measured: the bench SysType has none. Stage 2 measures them.

**Not measured** (needs other hardware): scanning across mux slots, and the time to find a device that is
plugged in while running.

---

## 3. Target design (end state)

This is where the stages lead. Each stage below moves part of the way there.

### 3.1 Two sources of record, one type table

- **Base records** are generated at build time, are `const`, and stay in flash, including their arrays.
- **Extended records** are added at runtime. They come from:
  - the device type file;
  - application code, for example RSAO or a SysMod;
  - static devices, i.e. devices declared in `DevMan/Devices`.
- **The type table** is the single place that maps a device type index to a record.
  - It is an array of handles. The entries for base indices point into flash by default.
  - Each entry also holds the record's **policy byte** (enabled, disabled, or disabled-claim) and its
    **source** (base, file, code, or static device).
- **Override keeps the index if it is applied before identification starts** (D2, refined by §3.8).
  - An extended record with the same device type key as a base record, loaded before the type table is
    `Ready`, replaces that base record's *entry*. It does not get a new index.
  - An override applied later (live reload) would get a **new** index, the key resolving to it and the old index
    staying bound to the old record until nothing uses it. Live reload was dropped from Stage 8 (2026-10-16), so
    after `Ready` an override with a base record's key just gets a new index for the rest of the session.
  - So a device type key always maps to exactly one *current* device type index, and an index never changes
    its meaning within a session (fixes P3/P8 consistency).
- **Disable is a policy, not a deletion.**
  - A file entry `{"deviceType":"VCNL4040","disable":true}` sets that entry's policy.
  - The record stays visible to `typeinfo`, decoding and subscriptions. Only identification ignores it
    (review §7.2).

### 3.2 Extended record storage

- **One allocation per record.**
  - A small fixed header holds offsets, sizes, flags, scan priority, identification priority and the
    hardware type ID.
  - It is followed by the strings, packed and NUL-terminated, and optionally by the compiled operations
    (§3.4).
  - This replaces six `String` objects, i.e. six heap blocks each with its own allocator overhead.
- **Allocated in PSRAM when present.** It uses `SpiramAwareAllocator`, otherwise internal RAM.
- **Capacity comes from configuration, with defaults that depend on PSRAM.**
  - The idle cost is one 4-byte handle per slot. Nothing is reserved up front.
  - The real limit is a **byte budget**, not a record count (see §4).
- **Records never move.** A replaced record is retired, not freed, for the rest of the session (a record change is
  applied by a restart - Stage 8 revised). This keeps today's guarantee that pointers stay valid, and states it in
  code.
- **Optional, for boards without PSRAM:** keep `devInfoJson` **in the file** rather than in RAM, and record
  only its offset and length.
  - `devInfoJson` is about 80% of a record's bytes, and it is read only for `typeinfo`, for `_conf` actions,
    and by a runtime decoder (if one is added later).
  - Those reads are rare and happen on the main task.

### 3.3 The catalogue index, built at runtime

All of this is built from the type table, honouring each entry's policy, at startup and again whenever the
table changes (when the generation, described below, increases). It replaces the generator's derived output
(G7):

| Structure | Form | Approximate RAM |
|---|---|---|
| Address → identification candidates | CSR: `uint16_t start[0x79]` plus a `uint16_t` index list, sorted by identification order (§3.5) | ~250 B + 2 B per (record, address) pair; ~400 B for today's catalogue |
| Address → all known records (`AllKnown`, used for RSAO reservation) | A second CSR, or one flag bit per entry in the first | ~150 B |
| Scan priority lists | `uint8_t` address lists, one per level | ≤ 360 B |
| Key → index | A `uint16_t` array sorted by device type key, searched by binary search | 2 B per record |
| Hardware type ID → index | A sorted array of `(uint16_t hwTypeId, uint16_t index)` | 4 B per record that has one |
| Reserved-address bitmap (for re-addressable devices) | 128 bits | 16 B |

- **Generation.** A `uint32_t` that increases on every change. Readers take one snapshot of the
  structure (built off to one side, then the pointer is swapped under the table mutex), so lookups don't
  hold a lock.
- **Base-only builds** spend under 1 KB of RAM and a few milliseconds at boot.
- **The generator keeps validating** records, and emits the per-record fields (§3.1) rather than derived
  tables. A host unit test checks that the runtime builder gives the same address map and scan lists as the
  old generated tables for the shipped catalogues.

### 3.4 One grammar, compiled once

- **One tokenizer for detection, init and poll strings** (review §9.1 RaftCore item 2).
  - It turns a string into compact typed operations: `Write`, `Read(n | expr)`, `Check(alternatives)`,
    `Pause`.
  - It checks the string strictly and reports the position of any error.
- **When it runs:**
  - for extended records, when the record is registered (rejected with the reason if invalid);
  - for base records, at build time through the same rules in Python, with shared test vectors.
- **What is stored at runtime:**
  - Extended records store the compiled operations in their record allocation, which is usually smaller than
    the strings.
  - Base records are compiled on first use and the result is cached per identified device type, as a
    `shared_ptr<const PollPlan>`. This also removes the copy of `DevicePollingInfo` made on every poll
    (review §5.1).

### 3.5 Identification order is explicit

- **Candidates at an address are ordered by:**
  1. `identPriority` (explicit, optional);
  2. detection strength (more check bits first);
  3. records with no detection values last, and only if nothing else matches;
  4. source order as the final tie-break.
- **The index builder sorts once.** At runtime, identification just walks the list.
- **The generator warns about shadowing** (for example VEML6040 and MXC400xXC).

### 3.6 Identification is gated on the catalogue being ready

- The type table has a state: `Loading` or `Ready`.
- While it is `Loading`, buses scan for presence but do not identify. Elements stay "online, pending
  identification".
- On `Ready`, and on every later generation change, the identification scheduler processes the affected
  elements. This replaces the dependence on `postSetup` order (review §9.2-H).

### 3.7 The bus task is a scheduler with budgets

Each worker pass:
1. services the bus-task handler;
2. processes queued requests;
3. runs polls that are due, earliest deadline first, until the pass's poll budget is used;
4. runs identification work within its own budget;
5. runs scanning within the time that is left.

The worker sleeps until the next deadline (`ulTaskNotifyTake` with a timeout), not for a fixed 5 ms, and a
queued request wakes it at once.

### 3.8 Index stability and safe decoding

Added 2026-10-15 in answer to: "is the published device type index unchanging when records are added or
disabled, and how do we make sure data isn't decoded with the wrong record during a transition?"

**How it works today** (checked in code):
- **Inside the firmware, samples and their index are kept together.**
  - Identification creates a new `PollDataAggregator` for the device (RaftI2C `DeviceIdentMgr.cpp:144-153`).
  - Clearing an identification drops the aggregator with the type (`BusStatusMgr::clearDeviceIdentification`
    → `DeviceStatus::clear()`).
  - Publishing reads the index and the samples together under the status mutex
    (`getBusElemPollResponses`, used by `getQueuedDeviceDataJson/Binary`).

  So a published record never carries samples from one type labelled with another type's index.
- **Adding a record** appends an index, so existing indices don't change. **Disabling** doesn't exist yet;
  as planned (a policy byte) it doesn't change any index either.
- **Decoding looks the record up by index when it decodes** (`getPollDecodeFn(deviceTypeIndex)`, raftjs
  typeinfo by index). That is only correct if an index always means the same record.

**Where it can go wrong:**

| # | Case | Result |
|---|---|---|
| H1 | An override rebinds an index that devices are already using (D2 as first written, applied live) | Samples polled under the old record are decoded with the new record's layout, by firmware and by clients that already cached the index |
| H2 | Indices differ between sessions: extended record indices follow file order; base indices follow JSON order and firmware build | A client that keeps its cache across a reboot, reconnect or OTA decodes with the wrong record. raftjs never invalidates its cache today |
| H3 | raftjs also caches typeinfo **per device ID**, and doesn't check `_i` again | When a device at the same address is re-identified as a different type, raftjs keeps decoding it with the old type |
| H4 | Firmware consumers cache the decoder per device (RaftROS AutoPub, DataLogger) | Same as H3, inside the firmware (review A1) |
| H5 | A multi-step poll is in progress when the device is re-identified | The remaining steps of the old poll plan complete, and their result could be stored in the new type's aggregator |

**Two identifiers in every published record.** The terms matter here:
- The **device ID** (bus number in the devbin status byte, plus the element address; `deviceKey` in raftjs)
  says *where* a device is. It **is** reused: a different device, or the same device identified as a
  different type, can appear at the same device ID.
- The **device type index** (`devTypeIdx` in devbin, `_i` in devjson) says *which record decodes this
  record's samples*. Under rule 1 it is **never** reused within a firmware boot.

Every devbin and devjson record carries both, so a client can always decode a record from its own index,
without knowing anything about the device's history.

**Rules** (agreed as D14, rules 4 and 5 revised 2026-10-15; they apply to Stages 2, 5, 7 and 8):
1. **An index is bound to one record's content for the whole firmware boot.** It is never reused or rebound.
   - Before `Ready`, nothing has been identified or published, so a file override can take the base
     index (§3.1).
   - After `Ready`, any change to a record's content gets a new index. The old index is retired, and stays
     resolvable for decoding until no device and no stored sample uses it.
   - A policy change (disable or enable) never changes an index.
2. **A type change on a device swaps the aggregator and the index together**, under the status mutex. This
   is already true, and Stage 7's make-before-break must keep it true: it keeps the old aggregator while the
   type is unchanged, and replaces it at the moment the type changes.
3. **Each poll carries the identity it was started under.** Re-identification cancels any partial poll for
   that device, and a result whose identity no longer matches is dropped (H5).
4. **A client's "session" for type data is one subscription.** Index-keyed type data is valid from a
   subscribe until the next one.
   - The firmware forgets subscriptions when it reboots, and raftjs re-subscribes after every reconnect
     (`RaftConnector._reestablishAfterReconnect`). So every new firmware boot is seen by raftjs as a new
     subscription. Within one subscription, rule 1 means the cache never needs clearing.
   - Across a reboot an index *can* change meaning: after an OTA (base order), after a type file change,
     or with a boot-time override (D2). So raftjs clears its index-keyed type cache when it (re)subscribes
     (R1). This is a client-only change.
   - **No change to devbin or devjson is needed.** The earlier proposal of a type table ID in the publish
     streams is withdrawn (D16). A table ID may still be reported in `devman/types` for diagnostics, but no
     client depends on it.
5. **Decode by the record's index, never by the device ID.** A client must not bind decode metadata to a
   device ID. For each published record it looks up the type data for *that record's* index, and keeps
   per-device-ID state (name, role, last index seen) separately. When the index for a device ID changes, the
   device has changed type: the client switches to the new index's type data (fetching it if needed), and
   discards nothing else (H3, H4). Firmware consumers (RaftROS, DataLogger) follow the same rule using the
   index in each data callback.

With these rules, an index means one thing for a whole boot, every sample carries the right index, and a
client only ever needs to clear index-keyed caches at the start of a subscription.

**What "clearing caches" means, precisely:**

| raftjs cache | Keyed by | When it is cleared | Why |
|---|---|---|---|
| Type data (decode metadata) | Device type index (`bustype:<bus>:<idx>`) | On (re)subscribe only | An index can mean a different record in a new firmware boot, never within one |
| Type data per device ID (`_cachedDeviceTypeRecs[deviceKey]`, today) | Device ID | **Removed as a decode cache.** Replaced by the device ID → last index map plus the index-keyed cache | A device ID can change type within a boot, so binding decode data to it is wrong, and clearing it at the right time would be guesswork |
| Instance data (device name, role) | Device ID | On (re)subscribe, and when the name or role is changed through raftjs | Names and roles belong to the location, not the type |

Old raftjs keeps today's behaviour: it decodes a device ID with the type it first saw there. That already
fails today after a CRC re-identification or an OTA without an app reload. The firmware can't fix this
without changing the streams, so apps using an older raftjs must be reloaded after an OTA or a type file
change.

### 3.9 Record identity and names

Added 2026-10-15 in answer to: "what are the key, `deviceType`, `devInfoJson.name` and `devInfoJson.type`
for, and which of them identifies a record?"

**What the catalogues contain** (checked in every device type JSON file across RaftCore, RaftI2C, Axiom and
the RaftROS examples, 2026-10-15):

| Field | Present | Agrees with the key | Read by |
|---|---|---|---|
| The key in `devTypes` | Always | — | The generator: C struct and decoder names (`poll_<key>`) |
| `deviceType` | In every record | **Yes, in every record** | The generator copies it into `DeviceTypeRecord::deviceType`, which is the runtime identifier: name lookups, `typeinfo?type=`, logs, RaftROS AutoPub. The Axiom device library checks that it equals the key |
| `devInfoJson.type` | In every record | No: `AdafruitGamepad` has `Gamepad`; `RoboticalThermocoupleAmp` has `Thermocouple` | No reader in RaftCore, RaftI2C or RaftROS. **Clients do read it** (found 2026-10-15): raftjs's simulated channel uses it to choose device behaviour and fill `_t`, and raftjs's e2e scripts find devices by it (e.g. `"RoboticalVCP"`). Apps very likely do the same, since firmware never sent the real key |
| `devInfoJson.name` | In every record | — | The text shown to users, through raftjs |
| `devInfoJson.desc` | In every record | — | Longer description for UIs |

So `deviceType` is a pure copy of the key, and `devInfoJson.type` is a copy that has drifted in 2 records but
that clients rely on.

**Proposal:**

| Concept | Source | Rule |
|---|---|---|
| **Device type key**: the unique identifier of a record | The `devTypes` key | The only place the identifier is written. Unique within a catalogue. Stable across builds and sessions. Used in files, REST (`type=`), C++ lookups, RaftROS and logs. Never shown to users as a label |
| **Type display name** | `devInfoJson.name` | Shown to users. Need not be unique |
| **Type description** | `devInfoJson.desc` | Unchanged |
| **Device type index** | Assigned by the firmware | Session-scoped handle for one *version* of a record (§3.8). Wire names `_i` and `dtIdx` unchanged |

- **`deviceType` is optional in the JSON** (D13, done 2026-10-15).
  - The generator fills `DeviceTypeRecord::deviceType` from the key (the field keeps its name; it holds the
    key). The file loader (Stage 6) will do the same.
  - A record with `deviceType` is accepted if it equals the key; otherwise the build fails.
  - The RaftCore catalogue no longer has the field. Other catalogues (Axiom, RaftROS examples, RaftI2C tests)
    still have it and need no change. Generated output is byte-identical either way.
- **`devInfoJson.type` stays, permanently, with its current values** (D13 revised 2026-10-15).
  - Clients rely on it (see the table above), so removing it, or changing the two values that differ from
    the key, would break them.
  - `typeinfo` returns a top-level `deviceTypeKey` (done 2026-10-15), which new client code should use.
  - The generator warns about any new record whose `devInfoJson.type` differs from its key, so no new drift
    appears.
- **Overrides.** A record's identity is its key. An override is a new *version* of the same key, from a
  different source.
  - `typeinfo` reports the version that is current (`src`: `base`, `file`, `code` or `static`).
  - `devman/types?all=1` also lists the versions that have been replaced, so a UI can show "the file
    overrides the base VCNL4040".
  - Disabling is applied to a key, so it applies to whichever version is current.
- **Static devices** already get their key from configuration (`type`, e.g. `ACCDEMO`). C8 (two instances
  with the same key and different content) is handled by the registration result in Stage 2.

**Term** (D12, done 2026-10-15). The identifier is the **device type key**. It was called the "device type
name", which is easily confused with `devInfoJson.name` (shown to users, not unique). Principles §2, the plan
and the wiki use the new term, and `devman/typeinfo` returns `deviceTypeKey`. Inside the Raft libraries,
parameter names, local variables, comments and log text use "key". Names that are part of an API or contract
keep their spelling and hold the key: `DeviceTypeRecord::deviceType`, `DeviceTypeRecordDynamic::deviceTypeName`,
`getDevTypeInfoJsonByTypeName`, the REST parameter `type=`, and JSON and wire field names.

---

## 4. Memory budget for extended records

Estimates from the current catalogue sizes, except where marked as measured (§2.4).

| | Today | Proposed, no PSRAM | Proposed, with PSRAM |
|---|---|---|---|
| Base record array and address index | 276 B DRAM (measured, §2.4: the large arrays are already placed in flash by the compiler) | 0 DRAM (const, in flash) | 0 DRAM |
| Idle cost of extended record slots | ~2.2 KB internal (`reserve(20)`) | 4 B × capacity (128 B for 32) | 4 B × capacity (1 KB for 256) |
| Typical extended record (median `devInfoJson` ~380 B) | ~520 B + 6 heap blocks of overhead | ~480 B, 1 block (internal) | ~480 B, 1 block (PSRAM) |
| Same, with `devInfoJson` kept in the file | n/a | ~100–150 B | not needed |
| Suggested default capacity | 20 | 32 records, 20 KB byte budget (16 KB proposed; measured in Stage 2) | 256 records, 256 KB byte budget |
| Catalogue index (§3.3) | 0 (generated) | < 1 KB + 2–6 B per record | same, in PSRAM |

Configuration keys (in the DeviceManager config):
- `typeTable/maxExtendedRecords` (Stage 2)
- `typeTable/maxExtendedBytes` (Stage 2)
- `typeTable/devInfoInFile` (with the type file, Stage 6)

---

## 5. Stages

Each stage lists its aim, the changes, the repositories it touches, how it is verified, and what it depends
on. "Bench" means the FeatherS3 rig (MPU6050 and APDS9960) plus Axiom hardware where RSAO behaviour is
involved.

### Stage 0: Agree terms; add measurements

**Aim:** settle names before changing code, and take a baseline to measure every later stage against.

**Status (2026-10-15): done**, except the measurements that need other hardware (scanning across mux slots,
hot-plug timing) - since made on the Axiom in Stage 9 (2026-10-16). Naming: D1, D12, D13 and the `deviceTypeIndex` renames are done; the hardware type ID moved to
Stage 3 (D24). Measurements: the firmware statistics below, `scripts/bench/device-bench.js`, and the baseline in
§2.4. Also ported from the device library branch (§10.7 item 12): the two DeviceManager regression tests, run
on-device, and the on-device DeviceNames test now tests the real `DeviceNames` class.

**Firmware statistics added** (always on; reported by `devman/busstatus`, with `reset=1` to restart the
measurement):
- `pollTiming` per polled device (`DevicePollTimingStats` in each `DeviceStatus`, 72 B): interval count, mean,
  min and max, a histogram of the difference from the configured interval with its 95th percentile, missed
  periods and achieved rate, measured from one full poll start to the next;
- `activity` per I2C bus (`BusI2CActivityStats`): for the last complete ~1 s window, the worker's passes, busy
  time, longest pass, time in each part of a pass, scan probes and poll steps;
- `identTiming` per I2C bus: for each element, when it was first seen, came online and was identified, in ms
  since the bus started.

**Naming decisions** (G11). Adopt the canonical terms in *principles §2*, with these additions:

| Decision | Proposal |
|---|---|
| Names for the two record sources | **base** and **extended**, as in principles §2 and the code (D1). Not "static"/"dynamic" or "built-in"/"runtime" |
| Class names | Unchanged. `DeviceTypeRecordDynamic` is external API (overridden by Axiom `DevicePower`, RaftMotorControl `MotorControl` and `DemoDevice`), so it keeps its name and is documented as the class that holds an extended record's data |
| Index spelling in code | **Decided 2026-10-15:** `deviceTypeIndex` for parameters, locals and comments inside the Raft libraries (replacing `deviceTypeIdx`, `devTypeIdx`, `typeIdx`). Public names and the wire names `_i` and `dtIdx` are unchanged |
| Vendor type identifier | **Moved to Stage 3** (D24): named when RaftCore takes over the ID-to-record mapping |
| Lookups | **Not renamed** (they are API). Stage 3 adds `getIdentCandidates(addr)` and `getKnownTypesAtAddr(addr)` as new functions; the existing ones stay |
| Record identifier and its copies | §3.9: the `devTypes` key is the identifier ("device type key"); `deviceType` is optional in the JSON and filled from the key; `devInfoJson.type` is kept for clients |

Rule for the later stages (agreed 2026-10-15): names inside the Raft libraries that are not part of an API
(parameters, locals, comments, log text) follow these terms in the code each stage touches. Names that are part
of an API or contract - public functions and fields, REST parameters, JSON and wire fields - are not renamed;
new functions or fields may be added alongside them.

**Measurements:**
- **Poll timing.** For each device, report the mean, p95 and maximum actual interval against the configured
  interval, plus the number of missed deadlines. `DEBUG_POLL_TIMING` exists, but it tracks only 8 entries and
  is compile-time only. Make it a runtime stat exposed in `devman/busstats` (or a new `devman/pollstats`).
- **Scan.** Time from plug-in to `isNewlyIdentified`, by priority list and by slot. Also probes per second
  and the share of bus time used by scanning.
- **Bus utilisation.** Time spent in polls, scans and requests per 1 s window.
- **Memory.** DRAM used by the type table, idle and per extended record, with and without PSRAM.
- **Script:** a bench script (in the style of `deftest.js`) that records all of the above to JSON, so later
  stages can be compared run against run.

**Verify:** the baseline numbers confirm or correct §2.3 and §4.

**Repos:** RaftCore (stats API), RaftI2C (instrumentation), bench scripts.

---

### Stage 1: Polling accuracy and presence correctness

**Aim:** devices are polled at their configured rate, fairly, and a device that has gone away is reported as
offline (G9).

**Status (2026-10-15): done**, except items 3 and 10 (see below) and I8, which moves to Stage 9. What was done:
- **Scheduling (items 1–3).** `DevicePollingInfo` holds `nextPollDueUs` and `nextStepDueUs`;
  `DeviceStatus::getNextPollDueUs()` and `getPendingIdentPollInfo()` keep polls on a fixed phase, restart the
  schedule (no burst) when a poll is a whole interval late, and time split-poll steps separately (a failed step
  is retried after its pause, not on every pass). `DevicePollScheduler` (RaftCore, generic over the bus's
  record type) picks the earliest due record, round robin on ties, and polls only online devices.
  The tie-break on the selected slot and on bus speed (items 3 and 10) is not done: due times are in
  microseconds, so exact ties are rare and the bench showed no need. Revisit if mux switching shows up in the
  Stage 9 measurements.
- **Worker (items 4, 5).** Polls run earliest due first until `pollBudgetUs` (default 2 ms) is used. The worker
  sleeps with `ulTaskNotifyTake()` until the next poll is due (at least one tick, at most `loopYieldMs`), and
  `BusI2C::addRequest()` wakes it. The pass order is now handler, requests, mux/IO, polls, scan: scanning comes
  last, runs at most once per `loopYieldMs`, and stops in time for the next due poll (but gets at least
  0.5 ms).
- **Defaults (item 6).** Missing `i` = 100 ms; any interval (record or `devconfig`) is clamped to at least 2 ms;
  `s` defaults to 1 and is at least 1 (also for `ext` records, item 9).
- **Admission (item 7).** `pollLoadPct` in `devman/busstatus`, from each device's measured bus time per poll
  (`busUs` in its poll timing; estimated from the poll operations until it has been polled). A warning is
  logged at 70%. Measured polls take 3–4.6 ms each at 100 kHz, about twice the bit-time estimate.
- **Aggregator (item 8).** `uint32_t` offsets and counts, at least 1 result, at most 32 KB per device,
  `clear()` resets the latest value.
- **Device-specific code (item 11)** removed: the LSM6DS FIFO status check in `handlePollResult()` (it ran on
  every device with 4 or more bytes of data), the LSM6DS debug decode, the FIFO counters in `DeviceStatus`
  the FIFO word count in the poll timing debug, and the LSM6DS FIFO decode in the `getBusElemPollResponses()`
  debug log (now a generic hex dump). A search of RaftCore, RaftI2C and RaftSysMods for record names, addresses
  and registers found no other device-specific code. The one remaining tie to a record is in the BLE bus:
  `BLEBusDeviceManager` looks up the `BLEBTHome` record by key and `BLEAdvertDecoder` decodes the BTHome
  advertising format. That is a protocol decoder for the BLE bus rather than a device. The record key now comes
  from the BLE bus config (`deviceTypeKey`, default `BLEBTHome`), and if no record has that key BLE devices are
  not reported (before, a failed lookup left the type index at 0, which is another record).
- **Presence.** I1 (a slot that can't be selected counts as a failed poll), I7 (only online devices are
  polled; `goingOfflineOnSlot()` and `informBusStuck()` report online devices and then delete them, and erase
  unreported ones), I2 (`goingOfflineOnSlot()` matches records by slot), I14 (CRC recovery entries are freed
  when the record is deleted or its identification is cleared), I16 (warning when the address table is full),
  I15 (debug defines off). **I8 moves to Stage 9:** the scanner visits all addresses of one slot before the
  next, so clearing a main-bus device's "found" bit lets it be found through a mux slot first (and recorded
  against that slot) if it comes back mid-sweep. It needs a "does it answer on the main bus too" check.
- **raftjs R1** (and R3, R7 for R1): type data is resolved by each record's own index; when a device's index
  changes it switches to the new type's data (the per-device request is made again for its name and role,
  which are kept); `clearDeviceTypeCaches()` empties the type caches, and `RaftConnector` calls it (through the
  optional `RaftDeviceMgrIF.clearDeviceTypeCaches`) before each subscribe; a request started before a clear
  doesn't refill the cache.
- **Tests.** Host: `test_device_poll_scheduler` (fixed phase, no catch-up burst, split poll timing, interval
  clamp and change, no drift over 10 s with a 7 ms pass, no starvation, fair overload, EDF order, offline
  records skipped), poll info defaults in `test_device_type_records`, bus time in `test_device_poll_timing`.
  On-device (RaftI2C): aggregator limits and `clear()`. raftjs: five decode-by-index tests and a connector
  test.

**Results** (FeatherS3 bench, as §2.4; raw results `bench-results/2026-10-15-feathers3-stage1.json`):

| Scenario | Device | Configured | Before: polled (max interval) | After: polled (max interval) | After: delivered |
|---|---|---|---|---|---|
| defaults | MPU6050 | 20 Hz | 18.48 Hz (71 ms) | 20.00 Hz (54 ms) | 20.04 Hz |
| defaults | APDS9960 | 10 Hz | 9.50 Hz (116 ms) | 10.00 Hz (104 ms) | 10.02 Hz |
| all 100 ms | both | 10 Hz | 9.64 Hz (110 ms) | 10.00 Hz (103 ms) | 10.0 Hz |
| all 50 ms | both | 20 Hz | 18.72 Hz (62 ms) | 20.00 Hz (54 ms) | 20.04 Hz |
| all 25 ms | both | 40 Hz | 33.47 Hz (38 ms) | 39.99 Hz (34 ms) | 40.07 Hz |
| all 10 ms | MPU6050 | 100 Hz | **9.65 Hz** (461 ms) | 72.8 Hz (28 ms) | 73.0 Hz |
| all 10 ms | APDS9960 | 100 Hz | 78.09 Hz (26 ms) | 72.0 Hz (26 ms) | 72.1 Hz |
| mixed | MPU6050 at 10 ms | 100 Hz | 74.05 Hz (31 ms) | 99.88 Hz (19 ms) | 100.1 Hz |
| mixed | APDS9960 at 100 ms | 10 Hz | 9.44 Hz (117 ms) | 10.00 Hz (107 ms) | 10.01 Hz |

- Mean intervals are within 0.04% of the configured interval wherever the bus has the capacity. The 95th
  percentile of the interval error is 2–4 ms (polls take 3–4.6 ms at 100 kHz, so a device that falls due
  during another's poll waits for it).
- "All 10 ms" asks for about 85% of the bus in polls alone (`pollLoadPct` reports it, and the warning is
  logged). The two devices now share what the bus can do evenly (73 and 72 Hz) instead of one being starved.
- Scanning yields to polling: 444 probes per second at the default rates (baseline 340–590) but 84 per
  second in "all 10 ms" (baseline 352). Stage 9 sets a minimum scan share.
- Identification at boot is unchanged within the run-to-run spread (APDS9960 identified at 341–397 ms,
  MPU6050 at 652–734 ms over six boots; baseline 360–384 and 653–664 ms). The MPU6050's time from seen to
  online varied from 25 ms to 129 ms between boots (baseline 24 ms in both runs), which depends on where the
  sweep is when it is first seen. Stage 9 measures it properly.
- Not tested (needs other hardware): unplugging a mux (I1/I2), a multi-step poll device, three devices.

**Found during Stage 1:**
- Fixed: raftjs processed the devices of a devjson message in an un-awaited `forEach(async ...)`, so devjson
  records were not serialised the way devbin records are, and the state callback ran before they were
  decoded. Each device is now awaited in message order.
- Fixed: raftjs `RaftMsgHandler` re-arms two housekeeping timers for ever, which kept Node processes (tests,
  scripts) from exiting. They are now unref'd in Node.
- Left for now (decided 2026-10-15): RaftI2C `linux_unit_tests` is stale (it fetches RaftCore from GitHub and
  uses types that no longer exist). The Stage 1 scheduler logic is in RaftCore so it is tested by RaftCore's
  suite instead. The RaftI2C on-device tests build against RaftCore `main` from GitHub, so they need a
  `raftdevlibs` link to test a branch.

**Polling changes (RaftCore `DeviceStatus`, RaftI2C `BusStatusMgr`, `DevicePollingMgr`, `BusI2C`):**
1. **Due time from the schedule.**
   - Store `nextPollDueUs`. When a full poll *starts*, set `nextPollDueUs += interval`.
   - If the device is late by more than one interval, set the next due time to `now + interval` and count a
     missed deadline. Do not poll in a burst to catch up.
2. **Separate the step time from the poll time.** Partial-poll steps use their own `nextStepDueUs`, so the
   pauses no longer push the next full poll later.
3. **Earliest deadline first.**
   - `getNextDuePoll` returns the due record with the earliest `nextPollDueUs`. A linear pass over at most 50
     records under the lock is fine.
   - On a tie, prefer the slot that is already selected (this saves mux switches), then go round robin.
4. **More than one poll per pass.** Poll due devices until the pass's poll budget is used (configurable;
   default about 2 ms). Today it is one device per pass.
5. **Sleep until the next deadline.**
   - Replace the fixed `vTaskDelay(_loopYieldMs)` with `ulTaskNotifyTake(timeout)`.
   - The timeout is the time to the earliest next poll, step or scan slot. It is at least one tick, and at
     most `loopYieldMs`.
   - `BusAccessor` notifies the task when a request is queued, so requests also wait less.
6. **Interval defaults and limits.**
   - A missing `i` gets a default (proposal: 100 ms).
   - Any `i` is clamped to a configurable minimum (proposal: 2 ms).
   - Also C5: `s` defaults to 1 and is clamped to at least 1.
7. **Admission warning.**
   - Estimate each device's transaction time from its poll operations and bus speed, and sum the bus
     utilisation over all devices.
   - Log a warning above a threshold (for example 70%), and report it in the stats.
   - Example: two VCPs at 25 ms on a 100 kHz bus need ~102%.
8. **Aggregator width** (I6): `uint32_t` offsets and sizes, a byte cap in `resize()`, and `_latestValue`
   reset in `clear()`.
9. **Externally supplied records** (`pollInfo.ext`, e.g. VL53L5CX through its ULD task) are never polled.
   The interval default and clamp (item 6) and the admission estimate (item 7) skip them, but `s` must still
   be at least 1 (§10.5).
10. **Bus speed in the tie-break.** Records with `busHz` (e.g. LSM6DS at 400 kHz) switch the bus speed for
    their poll. When choosing between due devices, also prefer one that needs no speed change (§10.5).
11. **Remove device-specific code from the core** (P4) while these files are open: the LSM6DS FIFO debug
    decode in `BusStatusMgr::handlePollResult()` and the FIFO word-count timing in `DevicePollingMgr`.
    Either remove them or drive them from record data (§10.5).

**Identification fix (urgent):** done 2026-10-15 (PR #12). Binary detection patterns parsed as hex (§10.8) is
fixed, and `test_device_type_records` runs every shipped detection string through the parser.

**Client change (raftjs, independent of the firmware, D18):**
- **R1, decode by index** (§3.8 rules 4–5, §9.3): released with or before this stage. It needs no firmware
  change and fixes wrong decoding that already happens today after a CRC re-identification. Every later
  stage that can change a device's type (5–8) assumes clients have it.

**Presence changes (RaftI2C):**
- **I1:** a slot-enable failure during a poll counts as a poll failure.
- **I7:** only ONLINE records are polled, and OFFLINE always moves to PENDING_DELETION.
- **I2:** `goingOffline` compares composite (slot-encoded) addresses.
- **I8:** the "found on main bus" tracker bits are cleared when the record is deleted.
- **I14:** CRC recovery entries are freed when the record is deleted or its identification changes.
- **I16:** log when `ADDR_STATUS_MAX` is reached.
- **I15:** turn off the identification debug defines that are on by default.

**Verify:**
- The Stage 0 poll stats show a mean interval within ±1% of the configured value for `i` ≥ 10 ms, with no
  growing drift. A multi-step device (one with `pN` in its poll) polls at its configured rate.
- Three devices with intervals of 10, 10 and 50 ms all hit their rates.
- Unplugging a mux takes its slot devices offline within three poll intervals.
- Existing unit tests pass. New host tests cover the scheduler: EDF order, no drift, no catch-up burst.

**Risk:** a device that relied on being polled more slowly than configured now gets its true rate, so bus
load rises. The admission warning (item 7) makes this visible.

**Repos:** RaftCore, RaftI2C, raftjs (R1).

**Depends on:** Stage 0 (for measurement only).

---

### Stage 2: Type table storage

**Aim:** base records cost no DRAM, extended records are compact and can use PSRAM, and the limit of 20 is
raised (G1–G4). Behaviour does not change.

**Status (2026-10-15): done.** What was done:
- **Generator:** the record array and the address index are `const` (with `const` pointers); a
  `baseDevTypeIndexByKey` table (record indexes sorted by key, byte order) is emitted; C6 (crash when the
  first record has no addresses), C11 (`0x` read values counted the prefix) and C12 (addresses above 0x77
  dropped silently - now a build error) are fixed. Generated output for the RaftCore and Axiom catalogues is
  unchanged apart from `const` and the new table.
- **Extended records** (`DeviceTypeRecordPacked`): one allocation per record, `SpiramAwareAllocator`, holding
  a `DeviceTypeRecord` view whose strings point into the same allocation. A fixed array of record pointers
  (sized by the limits when the first record is added) and an atomic count published with release semantics:
  reading by index takes no lock (checked with ThreadSanitizer). Records never change or move.
- **Lookups:** `getRecord(deviceTypeIndex)` and `getRecord(key, index)` return `const DeviceTypeRecord*` (base
  records in flash); key lookups are binary searches (extended records first, so a key can be overridden).
  `getDeviceInfo()` and `getPollDecodeFn()` are wrappers.
- **Adding:** `addRecord()` returns `Added`, `AlreadyPresent` (same key and content: the existing index),
  `Conflict` (same key, different content - D17), `Full` (records, bytes or memory) or `Invalid` (no key); the
  index is `DEVICE_TYPE_INDEX_INVALID` unless added or present. `addExtendedDeviceTypeRecord()` keeps its
  signature and meaning. DeviceManager logs why a static device's record wasn't added, and the device then
  has no type (C7: never index 0). "Replaced" is left to Stage 5.
- **Limits:** `DeviceManager` config `typeTable/maxExtendedRecords` and `typeTable/maxExtendedBytes`, defaults
  256 / 256 KB with PSRAM and 32 / **20 KB** without. D3 said 16 KB, but a typical record measured 538 B, so
  16 KB held only 30.
- **Also fixed:** `DemoDevice` gave its default record a poll data size of 1 (the comment called it a type
  index) instead of 14.
- **Tests:** host `test_device_type_table` (lookups of every base record by index and key, add results,
  limits, byte budget, override of a base key, 256 records added out of key order, reads by index while
  adding); generator `test_generator_tables.py` (const, key order, C6, C11, C12); on-device
  `DeviceTypeTable_test` (default limits with and without PSRAM, memory freed) and two DeviceManager tests
  (static devices of the same type share a record; a conflicting one gets no type).

**Results** (FeatherS3; on-device tests built with and without `CONFIG_SPIRAM`):

| | Without PSRAM | With PSRAM |
|---|---|---|
| Base record tables in DRAM | 0 (all 92 table symbols, 3,273 B, in flash; 276 B in DRAM before) | 0 |
| Typical extended record (538 B packed) | 554 B internal RAM | 548 B PSRAM + 6 B internal RAM |
| Default limits | 32 records, 20 KB: 32 typical records fit | 256 records, 256 KB: 256 fit (138 KB) |
| Before (20 records, six `String`s each; estimated in §4, not measured) | about 2.2 KB reserved up front, plus about 520 B and six heap blocks per record | same, in internal RAM |

Identification, `typeinfo` by device and by key, and poll rates on the bench are unchanged.

**Changes:**
1. **Generator** emits `baseDevTypeRecords[]`, `baseDevTypeCountByAddr[]` and `baseDevTypeIndexByAddr*`
   as `const`. (The field descriptor tables and scan lists are already `const`.) Also C6, C11, C12.
2. **`RuntimeDeviceTypeRecord`** uses the packed single-allocation layout (§3.2), allocated with
   `SpiramAwareAllocator`.
3. **Extended record store:**
   - a fixed-capacity array of record handles, sized from configuration, with defaults that depend on PSRAM
     (§4);
   - an atomic count published with release semantics, so a read by index needs no lock (review §9.1
     RaftCore item 9);
   - a byte budget, checked when a record is added.
4. **Lookups without copies.** `const DeviceTypeRecord* getRecord(deviceTypeIndex)` returns a pointer to the
   flash record, or to a view of the extended record. The current `getDeviceInfo(idx, DeviceTypeRecord&)`
   becomes a wrapper around it.
5. **Registration result.**
   - `addRecord()` returns `{Added, Replaced, AlreadyPresent, Full, Invalid}` plus the index (C7).
   - DeviceManager never assigns index 0 when the add fails.
6. **Static devices of the same type with different data sizes** (C8): the second registration with the same
   key and different content is rejected with a clear error, and the configuration must give it a distinct
   `type`. Keys are not suffixed automatically, because that would rename RaftROS topics (§9.2 item 14, D17).
7. **Name lookup by binary search** over a sorted index. For base records it is generated (`const`); for
   extended records it is maintained when a record is added.

**Verify:**
- Linker map: the device type tables use no DRAM (276 B before, §2.4).
- Extended record cost per record matches §4, with and without PSRAM (FeatherS3 has PSRAM; test a build with
  `CONFIG_SPIRAM` off).
- Add 32 records (no PSRAM) and 256 records (PSRAM).
- All existing unit tests and the decode regression test pass.

**Repos:** RaftCore (Axiom only if it calls the renamed APIs).

**Depends on:** Stage 0 names.

---

### Stage 3: The catalogue index, built at runtime

**Aim:** the address map, scan lists and reserved addresses are built at runtime from all records, so extended
records are first-class (G7, G4).

**Status (2026-10-15): done**, except that `identPriority` is the only explicit ordering: the detection-strength
ordering of §3.5 (items 2 and 3) is left to Stage 7, because it would change which record matches today. What was
done:
- **Record fields** on `DeviceTypeRecord` (and `DeviceTypeRecordDynamic`, with defaults): `scanPriority`,
  `flags` (`FLAG_PROBE_ONLY` from JSON `probeOnly` or `excludeFromAddrMap`; `FLAG_HAS_HW_TYPE_ID`),
  `identPriority` and `hwTypeId` (**D24 decided:** RaftCore holds it; C++ name `hwTypeId`; JSON `deviceTypeId`).
  The generator emits them with each record and validates them. Code that builds records with the existing
  constructor is unchanged.
- **`CatalogueIndex`**, built from all records when the generation changes and shared as an immutable snapshot
  (`getCatalogueIndex()`): identification candidates per address in CSR form (descending `identPriority`, then
  extended records as added, then base records in catalogue order - the order used before), reserved addresses,
  scan lists from each record's `scanPriority`, and hardware type ID -> index.
- **Queries:** `getIdentCandidates()`, `getKnownTypesAtAddr()`, `isAddrReserved()`, `getRecordByHwTypeId()`,
  `getGeneration()`. `getDeviceTypeIdxsForAddr()` (used by Axiom's RSAO address reservation) and
  `getScanPriorityLists()` are kept and come from the index.
- **RaftI2C:** `DeviceIdentMgr` uses `getIdentCandidates()`. `BusScanner` reloads its scan lists (keeping scan
  boost and each list's position) when the generation changes, so the snapshot taken at setup (§5.16) is gone.
- **Generator:** no longer emits the address map or the scan lists (the sorted key index stays, in flash). New
  warnings: a key in more than one file, and a record with no detection values ahead of another at its address.
- **Tests:** `test_catalogue_index` checks that the runtime index equals what the generator derived (computed
  by `scripts/tests/reference_catalogue_index.py`, the old algorithm) for the RaftCore catalogue in CI, and was
  also run for Axiom's catalogue (47 records, RSAO records included); plus extended records' scan priority,
  candidate order, probe-only records, hardware type ID lookup, and rebuilds. Generator tests for the new fields,
  their validation and the warnings. Axiom's `RSAOTypeRegistry` can now be replaced by `getRecordByHwTypeId()`
  (an Axiom change, not made).

**Results:** on the FeatherS3 the index for the 49 base records builds in 2.1 ms and uses 766 B (targets 5 ms and
1 KB; a first version that kept a vector per address took 4.5 ms). Identification times are unchanged. The
generated record array grows by 5 bytes per record (in flash).

**Found:** the shadowing report finds records with no detection values ahead of others at the same address in
the RaftCore catalogue, so the later record is never identified there: VEML6040 before MXC400xXC (0x10),
QwiicLEDStick before TrillBar (0x23), AHT20 before TrillRing (0x38) and SHT40 before TrillHex (0x44-0x46).
`test_device_type_keys.py` lists them. Fixing them changes identification, so they are part of Stage 12, with the
detection-strength ordering in Stage 7.

**Changes:**
1. **Record fields that today exist only at build time** become fields of every record, base and
   extended:
   - `scanPriority`;
   - `probeOnly` (replacing `excludeFromAddrMap`);
   - `identPriority`;
   - the hardware type ID (D24: C++ name decided here; the JSON field stays `deviceTypeId`).
2. **`CatalogueIndex`** (§3.3) is built at startup and rebuilt when the generation changes:
   - the identification candidates per address, in the explicit order of §3.5;
   - the known records per address;
   - the scan lists;
   - key → index and hardware type ID → index;
   - the reserved-address bitmap.
3. **Generation counter.** It increases whenever records are added, replaced, or have their policy changed.
   `BusScanner` and `DeviceIdentMgr` check it once per pass and pick up a new index when it changes. This
   removes the snapshot of the scan lists in `BusScanner::setup` (review §5.16).
4. **Separate queries by purpose** (review §9.1 RaftCore item 8): `getIdentCandidates()` for
   identification, `getKnownTypesAtAddr()` and `isAddrReserved()` for RSAO reservation.
5. **The generator** stops emitting the derived tables. It keeps validating, and adds:
   - a warning for duplicate keys across files;
   - a warning for a key that differs from `deviceType`;
   - a shadowing report (an empty or weaker detection ahead of another record at the same address).
6. **Lookup by hardware type ID** (D24). Axiom's RSAO code can then generate or drop `RSAOTypeRegistry`
   (review §9.1 Axiom item 2).

**Verify:**
- A host test checks that the runtime-built address map and scan lists equal the old generated ones for the
  RaftCore and Axiom catalogues.
- Boot-time cost (Stage 0 measurement): index build under 5 ms and under 1 KB for base-only builds.
- An extended record with `scanPriority: "low"` is no longer forced into list 0.

**Repos:** RaftCore, RaftI2C, Axiom (RSAO registry, later).

**Depends on:** Stage 2.

---

### Stage 4: One grammar, one parser, compiled operations

**Aim:** invalid records are rejected with a reason when they are added, and identification and polling don't
re-parse strings (G4; this is also a prerequisite for loading untrusted files in Stage 6).

**Status (2026-10-16): done for the first release of D19** (problems reported, not yet rejected), except the
`PollPlan` that removes the copy of `DevicePollingInfo` on every poll (see below). What was done:
- **`DeviceTypeGrammar`** (RaftCore) compiles detection, init and poll strings into steps (write, read length or
  `r{…}` expression, check alternatives, pause, CRC) and reports issues with their position. It replaces the
  string helpers in `DeviceTypeRecords`. For every string it accepts it gives exactly what the previous parser gave
  - checked by `test_device_type_grammar` against a copy of that parser for every string in every known
  catalogue (RaftCore, Axiom, RSAO, RaftI2C test, device-library example: 269 strings, all identical) and 200,000
  fuzzed strings (clean under ASan and UBSan).
- **Rules:** a bare `pNN` is a pause (C4); odd-length hex is a warning, keeping the old padding (C9); `rN` in
  detection reads and compares nothing (C10); a bad `r{…}` is an error (C13); more than one `=`, non-hex write
  data, `0x` with no digits, `rN` above 65535 and pauses above 65535 ms are errors (the old parser turned them into
  garbage or allocated without limit); init values that aren't written (`0x…`) or are byte counts (`0b…`) are
  warnings. Errors: a detection string isn't used, an init or poll step is dropped.
- **`:crc8w`** (for Stage 12): each 2-byte word read must be followed by a valid Sensirion CRC-8.
- **Compiled once:** `DeviceTypeRecords::getCompiledRecord()` compiles a record's three strings when first needed
  and keeps them by device type index (an index always means one record); an extended record is compiled when
  added, so its problems are logged then. Identification uses the compiled steps: on the host, getting a
  record's detection steps takes 0.78 us instead of 3.44 us.
- **Identification (RaftI2C):** detection is tri-state (I4) - a NACK is no match, any other bus error stops the
  attempt with no fall-through to weaker records and no identification hook (it is retried later). Detection
  stops at the first step that doesn't match (before, every step and pause still ran). `decodePollResponses`
  no longer copies the record.
- **Generator:** reports the grammar's errors and warnings per record (`GRAMMAR ERROR/WARNING`, the build still
  succeeds), and warns if the poll size it computes differs from the firmware's. `DeviceTypeGrammar.py` is a
  copy of the C++ grammar; both run the shared vectors (`scripts/tests/grammar_vectors.json`) and a Python test
  fuzzes them against each other (30,000 strings, identical including issue positions).
- **Impact report** (`scripts/check_device_type_grammar.py`) over every known catalogue: no errors; three
  warnings - MAX30101 `0x11=0x21`, TMP117 `0x01=0x0220` (init values not written) and MXC400xXC `0x01=0b00010000`
  (a read, not a write). They are listed in Stage 12.
- **Bench:** identification and poll rates unchanged from Stage 1 within run-to-run noise.

**Not done:**
- **The second release of D19** (reject instead of warn) - after this one has been out with the warnings.
- ~~**`PollPlan`**~~ - **done 2026-10-17**, see below.

**`PollPlan` (done 2026-10-17):** a device's poll requests and response CRC settings are moved, when it is identified,
into a `PollPlan` (RaftCore `DeviceTypes/PollPlan.h`) shared by every poll as a `shared_ptr<const PollPlan>`. The poll
worker takes a `DevicePollJob`: the plan, the first request to send, and the bus speed settings, which are live
(`devconfig` can change them). A request with a dynamic read length (e.g. the LSM6DS FIFO read) is copied for the
poll, so the shared plan is never changed. A plan held by a poll stays valid if the device is identified again. The
existing interfaces are unchanged:
- `DevicePollingInfo` keeps its fields;
- `DeviceStatus::getPendingIdentPollInfo()` still returns a full copy;
- polling info filled in some other way gets a plan at its first poll;
- `handlePollResult()` is unchanged.

Measured:
- Host test `test_poll_plan`: getting a poll makes no heap allocations. The copy made 3 (LSM6DS) or 4 (VL53L0X).
  Both give the same polls at the same times.
- FeatherS3 (temporary instrumentation in `BusStatusMgr`, removed afterwards): the copy took 170-250 us per poll,
  under the status mutex, on top of the 55-100 us of choosing the poll. That time is gone.
- Unchanged: poll rates on the FeatherS3 (10.0 and 20.0 Hz, none missed), and the LSM6DS FIFO reads on the Axiom
  (bus time per poll 3728 us against 3738 us before).

**Changes:**
- **Tokenizer and compiler** (§3.4), replacing `extractBufferDataFromHexStr`,
  `extractMaskAndDataFromHexStr` and `extractReadDataSize`.
- **Rules:**
  - a bare `pNN` is a pause (finishes C4);
  - reject odd-length hex (C9);
  - reject more than one `=`;
  - `rN` in detection means "read N bytes, compare nothing" (C10);
  - a bad `r{…}` is an error (C13).
- **Rollout** (D19): one release in which the new rules only warn (at build and at load), with an impact
  report over every known catalogue; then they reject.
- **Shared test vectors** (JSON), run by a C++ host test and by the Python generator. The Axiom
  `device-descriptor-spec.md` is the starting point for the specification.
- **Compiled operations** are stored in the extended record. Base records are compiled on first use and
  cached per type (`PollPlan`).
- **Tri-state detection** (I4): `checkDeviceTypeMatch` returns `Match`, `NoMatch` or `BusError`. A
  `BusError` stops the attempt: no fall-through to weaker records, and no fallback hook.
- **CRC check in detection** (added 2026-10-16, for Stage 12): a check that each 2-byte word read is followed
  by a valid Sensirion CRC-8 (poly 0x31, init 0xFF), e.g. a suffix such as `:crc8w`. It makes a serial-number
  read a strong check for Sensirion parts that have no product ID (`device-detection-review.md` §3).

**Verify:**
- Every shipped catalogue compiles without errors.
- Fuzz the tokenizer on the host.
- Identification and poll results on the bench are unchanged.
- CPU time per identification attempt and per poll drops (Stage 0 measurements).

**Repos:** RaftCore, RaftI2C, generator.

**Depends on:** Stage 2 (storage of the compiled operations).

---

### Stage 5: Record policy and override

**Aim:** base records can be disabled at runtime, and an override really replaces a base record
(G5).

**Status (2026-10-16): done in RaftCore, RaftI2C and Axiom.** Axiom's Axiom018 SysType gives the fuel gauge entry
`"expectType": "MAX17048"`, so the MAX17048 is protected by configuration rather than a hard-coded rule. DevicePower
doesn't use a device at the fuel gauge address that was identified as another type (it logs the mismatch once and
keeps waiting); DeviceManager also logs any device identified as something other than its `expectType`. What was
done:
- **Policy** per type-table entry (`DeviceTypeRecord::POLICY_ENABLED`, `POLICY_DISABLED`,
  `POLICY_DISABLED_CLAIM`): an initial policy in each record, changeable with `DeviceTypeRecords::setPolicy()` (by
  index or key) and from the SysType's `DevMan/typePolicy` map. The catalogue index takes it into account: only
  enabled records are candidates; enabled and claim records reserve their addresses; claim records also block the
  identification hook there (`isAddrHookBlocked()`). A type identified by the hook is not used if it is disabled.
- **Override before Ready** (D2): a record with a base record's key replaces it at its index (`AddResult::Replaced`;
  the base record stays in flash, a replaced override is retired until the table is destroyed). The decoder and
  field descriptions are inherited when the `resp` layout is the same (ignoring whitespace). One override per base
  record: a second, different one is a `Conflict` (it would change what an index already given to a device
  means). After Ready a record with a base key gets a new index, as before. **Ready** is set by RaftI2C at the first
  identification attempt (`markReady()`); Stage 6 will set it when the type file has been loaded.
- **`disable` entries** (D23): a compiled record can carry `"disable": true` or `"disable": "claim"`, and a later
  catalogue file can set an earlier record's policy with an entry that has only `disable`. (Extended records from a
  type file get the same handling in Stage 6.)
- **Protected types** (review §9.2-F): a `DevMan/Devices[]` entry with `role: "system"` and `expectType` protects
  that key - it can't be disabled, overridden or replaced (`AddResult::Protected`, `PolicyResult::Protected`).
- **Every policy change and override increases the generation**, so the catalogue index and the scanner's lists are
  rebuilt; the compiled record of an overridden index is dropped. Devices already identified keep their type until
  they are identified again (applying a change to them is Stage 7).
- **`typeinfo`** reports `src` (`base`, `override`, `extended`) and `policy` (additional fields).
- **Tests:** host `test_device_type_policy` (disable and the next candidate, claim, protection, override before and
  after Ready, one override per key, decoder inheritance, generation, compiled record following an override);
  generator tests for `disable` entries. On the bench, `typePolicy: {"APDS9960": "disabled"}` left the device at
  0x39 online but unidentified, with the MPU6050 identified as usual and `typeinfo` reporting the policy. The
  FeatherS3 has no identification hook, so "not probed by a hook" is covered by the host tests and code only.

**Changes:**
1. **Policy byte per type-table entry:** `Enabled`, `Disabled` or `DisabledClaim` (review §9.2-B).
   - A `Disabled` record is not an identification candidate, so the next candidate (or the hook) gets a
     chance.
   - With `DisabledClaim`, the device is not identified, but its address still counts as claimed: the
     fallback hook is not run there, and the address stays reserved.
   - The policy affects identification only. `typeinfo`, decoding and subscriptions are unchanged.
2. **Override keeps the index when applied before `Ready`** (§3.1, §3.8).
   - Adding an extended record with the key of a base record before `Ready` replaces that entry's handle.
   - The old record is retired, not freed.
   - The device type key keeps a single index. (A record with a base record's key added after `Ready` gets a new
     index.)
   - The decoder is inherited when the `resp` schema is byte-identical (review §9.2-G).
3. **An extended record may carry `"disable": true`** (with or without a body) to set the policy for that
   name.
4. **Protected types and addresses come from SysTypes** (review §9.2-F). `DevMan/Devices[]` entries with
   `role: "system"` plus `expectType` cannot be disabled or overridden. This replaces Axiom's hard-coded
   MAX17048 / 0x36 rule.
5. **Every policy change increases the generation.** Applying the change to devices already identified waits
   for Stage 7. Until then a change applies after a restart or to newly appearing devices, and this is
   documented.

**Verify:**
- Unit tests:
  - disable a base record → the next candidate matches;
  - `DisabledClaim` → the hook is not called;
  - override → one index per name, and `typeinfo` reports `src`.
- Bench: disable APDS9960 → the device at 0x39 stays unidentified and is not probed by a hook.

**Repos:** RaftCore, RaftI2C, Axiom (protection rules).

**Depends on:** Stages 2 and 3.

---

### Stage 6: Device type file at startup; identification waits for it

**Aim:** extended records are loaded from a file before any device is identified. A change to the file is
applied by a controlled restart (G6).

**Status (2026-10-16): done in RaftCore and RaftI2C.** What was done:
- **Loader** (`DeviceTypeFileLoader`, RaftCore DeviceTypes): applies `{"devTypes":{…}}` text to the type table.
  It walks the file once and stores `pollInfo` and `devInfoJson` without whitespace (B11). Each record is checked
  before it is added (D22): `deviceType` equals the key (D13); addresses in the generator's format, up to 0x77 (N1);
  `deviceTypeId`, `identPriority`, `scanPriority` and `excludeFromAddrMap`/`probeOnly` read as the generator reads
  them; detection, init and poll strings compiled by the Stage 4 grammar (an error rejects the record); reads per
  command, `pollInfo.s` and the poll ring (result size × samples) within the limits; and `devInfoJson.resp.b` equal to
  what the runtime's poll info produces. A rejected record doesn't stop the others, and the first error is kept as
  `"<key>: <reason>"` (idea 14). An empty `devTypes` is valid (idea 5). A record with neither `addresses` nor
  `deviceTypeId` is a warning (N4).
- **Disable entries:** `"disable": true` or `"claim"` sets the policy of a type that is present (the rest of the
  entry is ignored, as on the branch); a full record with a new key is added already disabled (as the generator does,
  D23); a key that isn't present is rejected as "unknown key" (B3, N3). Protected types are refused.
- **DeviceManager:** `DevMan/typeFile`, defaulting to `DeviceLibrary/devLibFile` and then `/local/devlib.json` (D20);
  `typeFileRequired`; `typeFileTimeoutMs`; `typeFileLimits` (64 KB file, 1024-byte reads, 256 samples and a 64 KB
  per-record ring by default - the ring is `uint32_t` since Stage 1, so the last is a configurable cap rather than the
  branch's hard 65,535, D22). The file is registered as a record source in `setup()`, before the buses start, and
  loaded at the start of `postSetup()`, when every SysMod's `setup()` has run, so FileManager has mounted the file
  system.
  The CRC16-CCITT of the file is kept (the `filehash` algorithm, idea 4).
- **Readiness** (§3.6): `DeviceTypeRecords::registerRecordSource()` / `markRecordSourceReady()` /
  `areRecordSourcesReady()`, with a per-source timeout (10 s default, logged). RaftI2C's `BusScanner` doesn't identify
  while a source is outstanding: the element is online but unidentified, and is identified as soon as the records are
  complete, without waiting for the 3 s unidentified-retry interval. `markReady()` (identification has started) is
  unchanged, so a file override always lands before Ready and keeps the base index (D2).
- **REST:** `devman/typefile` (path, state, present, applied, CRC, counts - added, replaced, unchanged, disabled,
  rejected, warnings - first error and warning, records and bytes used and their limits, `identReady`);
  `devman/typefile/apply` replies, then restarts; `devlib` gives the same status with the branch endpoint's fields and
  meanings (B9).
- **Static devices** already register their records through `addRecord()` (Stage 2), so they share the limits and
  are in the `records`/`bytes` counts (item 5).
- **Not done:** the optional FileManager watch on writes to the file (an upload is applied by calling `apply`).

**Tests:** `linux_unit_tests/test_device_type_file.cpp` (88 checks): the branch's example file
(`fixtures/example-devlib.json`) loads with 1 added, 1 replaced, 1 disabled; every rejection rule; field parsing;
overrides, policies, protection, conflicts and limits; record sources and their timeout; and every record of the RaftCore
catalogue, loaded from a file, is identical to the generated record (devInfoJson compared as JSON, since the generator
re-prints floats and escapes non-ASCII), with the same identification candidates at every address (idea 6, as a test
rather than a reduced-catalogue build). On-device unit tests pass (113, with the known FileSystem image failure).

**Bench (FeatherS3, 2026-10-16):** no file → state `none`, both devices identified. A file overriding APDS9960,
disabling MPU6050, adding a new type and holding one bad record, uploaded with `fileupload` and applied with
`devman/typefile/apply`: after the restart APDS9960 was the override at its base index 51, 0x68 was not identified, the
new type had index 53, the bad record was rejected with its reason, and `crc` equalled `filehash`. With the load
delayed 3 s by a temporary test hook, APDS9960 was identified 52 ms after the file loaded, not before. A truncated
file gave state `error` ("not valid JSON") and the compiled records were used.

**Release notes - files written for the feature/device-library firmware** (D21; Nikos to be told):
- An override **replaces** the compiled record; the compiled record is no longer tried if the override's detection
  doesn't match (the branch's example file `_notes` says it is). Fix the override's detection rather than relying on
  fall-through.
- A disabled type is hidden from identification only: `typeinfo`, decoding and subscriptions still find it, and its
  address stays reserved.
- `scanPriority` and `excludeFromAddrMap`/`probeOnly` are honoured (the branch ignored them); a missing `scanPriority`
  means the address isn't in a priority scan list, as for compiled records.
- Stricter checks: addresses must be `0xHH` or `0xHH-0xHH`; command strings with grammar errors are rejected; a
  `disable` entry for an unknown key is reported as rejected.
- No re-identification of the whole bus after loading (B5): devices are identified once, after the file has loaded.
- The branch's DeviceLibrary SysMod isn't needed with this firmware: Axiom `main` doesn't have it, so there is nothing
  to migrate there (B6's `DeviceTypeIdSink` is replaced by `hwTypeId`, Stage 3).

**Changes:**
1. **Loader in RaftCore DeviceManager.** Configuration:
   - `DevMan/typeFile` (e.g. `/local/devtypes.json`);
   - `DevMan/typeFileRequired` (what to do if the file is missing).
   - **Format:** the same as `DeviceTypeRecords.json`, i.e. `{"devTypes":{…}}`. Entries may be full records,
     overrides (same name as a base record) or `{"disable":true}`.
   - **Validation:** each record is validated by the Stage 4 parser and the Stage 2 budgets. A bad record is
     rejected on its own and the first error is kept.
   - **Result:** a CRC of the applied content is kept, for comparison with `filehash`. This is the Axiom
     `DeviceLibrary` design, moved into the platform.
2. **Catalogue readiness** (§3.6).
   - The type table starts as `Loading`.
   - The loader, and any SysMod that registers records in code, marks its part complete through
     `markRecordSourceReady(sourceId)`. The table becomes `Ready` when every registered source is ready, or
     when a timeout passes (logged).
   - Buses scan but do not identify until it is `Ready`.
   - If the file system is already mounted in `DeviceManager::setup()`, the file is loaded there,
     synchronously and before the buses start, so normal boots never wait. (Check the SysMod order.)
3. **Applying a change to the file.**
   - `devman/typefile/apply` (and an optional watch on writes to that path through FileManager) applies a
     change by a **reset**, with a reply first.
   - This is the simple, safe first version - and, since Stage 8 was revised (2026-10-16), the way record changes
     are applied: `restartRequired` tells the client one is needed.
4. **Status reporting.** `devman/typefile` returns the path, state, counts (added, replaced, disabled,
   rejected), the first error, the CRC, the capacity used and the byte budget used.
5. **Static devices** register their records through the same path, so they share the budget and the
   reporting.

**Verify:**
- Boot with a file that adds one record, overrides one and disables one; check `listdevs` and `typeinfo`.
- A missing file and a corrupt file each behave as configured.
- Upload a new file over HTTP, then `apply` → the device restarts and uses it.
- No device is identified before the table is `Ready`. Check this by delaying the source with a test hook.
- The Axiom `DeviceLibrary` can be reduced to a thin wrapper, or removed.

**Repos:** RaftCore, RaftI2C (gating), Axiom (migration).

**Depends on:** Stages 4 and 5.

---

### Stage 7: Identification model

**Aim:** identification is scheduled per element, its outcome is visible, and consumers are told when a
device's identity changes (needed to make G6's restart-identification and G10 work).

**Status (2026-10-16): done in RaftCore, RaftI2C, RaftROS and Axiom.** What was done, by item:
1. **State per element** (`BusAddrRecord`): `identState` (`BusIdentState`: Pending, Identified, Deferred, NoMatch,
   BusError, InitFailed), attempts, `nextIdentMs`, the generation and hook epoch of the last attempt, a re-verify
   request, and the type last reported. `busstatus` identTiming shows each element's state and attempts.
2. **Scheduler** (`DeviceIdentMgr::taskService`, on the bus task each pass, `identBudgetUs` 5 ms beyond the first
   element, at most 8 elements): round robin over due elements. The scanner no longer identifies; the bus-wide 3 s
   retry and Stage 6's awaiting list are gone. Retry policy (`BusIdentSchedule`, decided 2026-10-16): Deferred every
   3 s (fixed - Axiom's RSAO commissioning debounce of 2 s relies on being asked again at about that rate); NoMatch
   (hook said NotMine) after 3 s backing off to 60 s, and at once when the generation changes or a hook is
   registered (hook epoch) - rather than never, because Axiom's RSAO hook also returns NotMine for a transient probe
   failure; BusError and InitFailed 100 ms backing off to 10 s. Only an element that answered its last access is
   tried (so a device that has just left an address isn't probed there - Axiom's parked "retry tests the debounced
   state" issue).
3. **Init outcome:** a NACKed init step is counted, initialisation is repeated once, and the device is used (with a
   warning) - soft resets are often NACKed, so failing those would break devices that work today. A bus error
   other than a NACK gives InitFailed (not used; retried with backoff).
4. **Identity events:** `BusAddrStatus::identEvent` (`BusIdentEvent`: Identified, TypeChanged, Lost) and
   `prevDeviceTypeIndex`, derived when the change is reported from the type last reported
   (`BusAddrRecord::takeStatusChange`). A cleared identification (CRC re-identify, re-verify mismatch) reports
   nothing until it is identified again, so a re-identification gives one TypeChanged (or Identified, or Lost) rather
   than Lost then Identified. Going offline reports Lost. `isNewlyIdentified` is kept (set for Identified and
   TypeChanged); a producer that doesn't set `identEvent` (BLE) gets Identified from `isNewlyIdentified`. An attempt
   that changes nothing isn't reported.
5. **Re-verification:** a poll that succeeds after a failed one resets the failure count (a device now goes offline
   after *consecutive* failures, as documented - before, failures accumulated across successes) and re-runs that
   type's detection; a match re-initialises (reported as Identified with the same type), a mismatch identifies the
   device again. Hook-identified (probe-only) types and detections with long pauses are not re-verified.
6. **Settings survive** (with N8): DeviceManager remembers `devconfig` settings per device ID for the type they were
   made for, and replays them on Identified/TypeChanged for that type - after a re-identification, re-verification
   (re-initialisation loses register writes such as `sampleRateHz`) or an absence. Another type forgets them. Kept
   in DeviceManager rather than on `BusAddrRecord`, because the record is deleted when the device goes offline.
7. **Consumers:** RaftROS AutoPub re-attaches on TypeChanged and detaches on Lost (the "fork's change" wasn't found;
   done here); the ExampleDiscoverable MainSysMod likewise. DeviceManager logs TypeChanged and Lost. Axiom
   DataLogger and DevicePower subscribe through `DeviceManager::registerForDeviceData` instead of on the bus (a bus
   registration was displaced by DeviceManager's dispatcher on every identification, and lost when the device went
   offline); DataLogger drops samples from a device re-identified as another type (logged once); DevicePower checks
   the type of each sample against `expectType`; VL53LxCX stops driving a sensor no longer identified as VL53L5CX.
8. and 11. **Pauses:** identification is a resumable job (`DeviceIdentMgr::IdentJob`): a detection or init step
   followed by a pause of 10 ms or more ends that element's work until the pause is over; shorter pauses wait inline.
   **Found and fixed:** the address bar was inverted (`BusStatusMgr::barElemAccessGet` barred an address once its bar
   had *expired*, and released it on an access *during* the bar) - so a pause's bar never held, and the first access
   after one could be barred until another bar was set, depending on whether the `delay()` overran by a millisecond.
   A barred access is now "no information" for the scanner and polling, not a failure.
9. **Axiom RSAO:** no code change needed with this retry policy (Deferred keeps its 3 s cadence; hook registration
   bumps the epoch so a NoMatch element is offered at once). Axiom's devdocs that say "retried every ~3 s" describe
   Deferred correctly; NotMine now backs off.
10. **Strength ordering:** candidates are sorted by `identPriority`, then `DeviceTypeGrammar::detectionStrength`
    (bits compared; CRC bytes count 8; an erroring string is -1), then the previous order. Same in the generator's
    shadowing report and `reference_catalogue_index.py`. It changes the order at 10 RaftCore addresses, 3 Axiom and 1
    FeatherS3 test address; the four Stage 3 shadowing cases are gone (VEML6040 now after MXC400xXC, QwiicLEDStick
    after TrillBar, AHT20 after TrillRing, SHT40 after TrillHex). See Stage 12 item 2 for what to check.

**Tests:** `test_bus_ident.cpp` (41 checks: retry policy, due rules, event derivation, event sequences of a record,
producer default); the catalogue index, generator and shadowing tests updated for the new order. On-device unit tests
pass (113, with the known FileSystem image failure). Axiom018 and the FeatherS3 build.

**Bench (FeatherS3, 2026-10-16):** a temporary hook disabled MPU6050 and cleared 0x68 (a type-file copy,
MPU6050Alt, then matched): `typeChanged 52 -> 53` within 50 ms of the clear, and back; the ROS publisher for
`/raft/imu_1_68` was re-declared (new GID) after the TypeChanged and not after a same-type re-verify. An injected poll
failure gave a re-verify ("still matches, re-initialised") and DeviceManager re-applied `intervalUs`. APDS9960 disabled:
0x39 tried 6 times in 105 s (0, 3, 9, 21, 45, 93 s) - 33 or so before. Identification follows coming online by 20-56
ms. The "ten devices at once under 1 s" case wasn't reproducible with two devices; the scheduler handles 8 due
elements a pass and each pass is about 5 ms apart, against one element per 3 s before.

**Changes** (review §9.1 RaftI2C items 2, 3, 7 and §9.2-C):
1. **Per-element identification state** on `BusAddrRecord`:
   - `identState` (`Pending`, `Identified`, `Deferred`, `NoMatch`, `BusError`, `InitFailed`);
   - attempt count;
   - `nextIdentMs`;
   - the generation it was identified against.
2. **An identification scheduler** on the bus task, with its own budget per pass.
   - It picks due elements round robin, and each element backs off exponentially on its own.
   - It replaces the single bus-wide 3 s retry (I9).
   - `NoMatch` is not retried, and the fallback hook is not re-run (I10), until the generation changes or
     the device reappears.
3. **Init outcome** (I11): NACKs during init are reported, init is retried, and `InitFailed` comes with
   backoff.
4. **Identity-change event.**
   - `BusAddrStatus` gains `identEvent` (`Identified`, `TypeChanged`, `Lost`) and the previous device type
     index.
   - It is raised wherever the identity changes, including `clearDeviceIdentification`.
   - `isNewlyIdentified` is kept for compatibility.
5. **Re-verification.** After a poll glitch (a failure followed by a success), re-run *that type's*
   detection. Re-initialise if it still matches; re-identify if it doesn't (I3).
6. **Per-device settings survive.** `devconfig` overrides are held on `BusAddrRecord`, separately from
   `DeviceStatus`, and are re-applied when the type is unchanged (review §9.1 RaftI2C item 10).
7. **Consumers** handle `TypeChanged` and `Lost`:
   - RaftROS AutoPub (upstream the fork's change);
   - DeviceManager subscribers;
   - Axiom DataLogger, DevicePower and VL53LxCX.
8. **Detection pauses** (I17): move them out of the bus-owner lock, or count them against the
   identification budget.
9. **Lands together with the Axiom RSAO change.** The hook stops being re-offered every ~3 s, and Axiom's
   commissioning debounce depends on those retries today (§9.2 item 13).
10. **Detection-strength ordering** (§3.5 items 2 and 3, deferred from Stage 3): candidates at an address are
    sorted by `identPriority`, then the number of bits their detection checks, with no-detection records
    last, then catalogue order. This changes which record matches at some addresses (the shadowing cases
    found in Stage 3), so it is verified with the Stage 12 detection changes.
11. **Detection pauses that don't block the bus.** Long pauses (a sensor's stop command takes 500 ms to
    1.2 s; the Robotical records already pause 500 ms) run as resumable identification steps rather than a
    `delay()` on the bus task. Stage 12 depends on this for SCD4x and STCC4.

**Verify:**
- Ten devices unidentified at once recover in under 1 s, not about 30 s.
- A forced CRC re-identify produces `TypeChanged`, and RaftROS re-attaches.
- An unidentifiable device gets the hook probe once, not every 3 s.

**Repos:** RaftCore, RaftI2C, RaftROS, Axiom.

**Depends on:** Stages 1, 3 and 4.

---

### Stage 8: Re-identification and live policy (revised 2026-10-16)

**Aim (revised):** devices can be identified again on request, and a policy change takes effect at once. A change to
device type *records* (the type file, or later a records API) is applied by a restart, as in Stage 6 - G6 allows
"reset the system or restart identification".

**Why revised:** changing records while running means replacing or freeing records that buses, paused
identification jobs, consumers (AutoPub, DataLogger, DeviceManager) and REST calls on other tasks may still hold;
giving a live override a new index while the old one stays resolvable; keeping buffered samples from being decoded
with a new layout; and staging and rolling back a file that fails part-way. That is at least as much work as Stage 7
and touches every consumer and raftjs again, for something expected to be rare - and identification already waits
for the type file at startup (Stage 6), so a restart applies a change cleanly. Decided with Rob 2026-10-16.

**Status (2026-10-16): done in RaftCore and RaftI2C.**
1. **Scoped re-identification:** `RaftBusDevicesIF::requestReIdentify(BusReIdentRequest)` with scopes All,
   Unidentified, Address, TypeIndex and Outranked (devices identified as a type that a given type is now tried
   before); `DeviceManager::requestReIdentify()`; `devman/reident?deviceid=|type=|all=1|unidentified=1` (brought
   forward from Stage 10). Requests are queued and applied on the bus task. **Make-before-break:** an identified
   device is polled as its type until the result is known; the same type matching changes nothing (no re-init, no
   event); another type is initialised and reported as TypeChanged; no match is Lost; a bus error or a deferral keeps
   the type and is tried again (3 times). Hook-identified (probe-only) devices are only re-identified by address.
   Unidentified devices in scope are tried at once.
2. **Live policy:** `DeviceManager::setTypePolicy()` and `devman/typepolicy?type=&policy=` (brought forward from
   Stage 10; not saved): disabling re-identifies the devices of that type; enabling re-identifies the devices it now
   outranks (and the generation change retries unmatched ones).
3. **Restart for records, with a dirty flag:** `devman/typefile` reports `restartRequired` when the file on disk
   differs from the one loaded (CRC, or presence) or `DeviceManager::markTypeFileChanged()` was called - so a
   client making several changes restarts once, when it chooses (`devman/typefile/apply`). Rule for Stage 10: an API
   that changes records edits the type file and calls `markTypeFileChanged()`; it doesn't change the live table.
4. **Fixed on the way:** an identification job that paused across a records change stamped its outcome with the new
   generation, so a NoMatch wasn't retried when the change could have matched it; outcomes now carry the generation
   and hook epoch the job started with.

**Tests:** `test_bus_ident` (55 checks) adds the scope rules. **Bench (FeatherS3):** `reident?all=1` - both devices
same type, no event, no missed polls; disabling APDS9960 - Lost in 70 ms; enabling - identified again in 90 ms;
with the MPU6050Alt type file, disabling MPU6050 - TypeChanged to MPU6050Alt in 50 ms, enabling - TypeChanged back
(Outranked) in 45 ms; `restartRequired` false at boot, true after uploading a different file, false after `apply`.

**Not done (dropped; a possible future stage if live record editing becomes common):** reloading the file without a
restart, live overrides with a new index (§3.8 rule 1's live case, D2's refinement), freeing retired records
(§9.2-I), and the raftjs index-cache changes that go with them. Until then nothing is freed or re-assigned within a
session, so record pointers stay valid for its life.

---

### Stage 9: Scanning

**Aim:** new and changed devices are found quickly, scanning uses a bounded and adaptive share of the bus,
and the main bus and all slots keep being covered (G8).

**Status (2026-10-16): done in RaftCore, RaftI2C and Axiom**, including the multiplexer items listed at the end.
What was done:
- **Schedule** (`BusScanSchedule`, RaftCore, host-tested): the three lists from the catalogue index are scanned at
  every slot position (main bus twice); list 0 a whole position at a time, and after each run of it, list 1 a chunk
  of size/3 and list 2 of size/9 (fractions carried over) - the same share of probes per list as before, but a
  list-0 address comes round after one chunk of each lower list instead of a whole lower list. Replaces the
  previous list switching (which also scanned the first address after a list wrapped on the next list's slot).
- **Background sweep (item 2, D9):** addresses in the lowest list that nothing can claim - no record identifies or
  claims them, the identification hook doesn't handle them, not a multiplexer or `scanBoost` address - are probed
  spread over `backgroundSweepMs` (10 s). `RaftBusDevicesIF::setNewDeviceIdentAddrRanges()` lets a hook declare its
  addresses (the hook is then offered only those); a hook without ranges handles every address, as before. Axiom
  RSAO registers its default address and assignable range (0x0F, 0x10-0x6F). FeatherS3 lists: 28/54/5 addresses
  with 29 in the background.
- **Confirmation probe:** an address that answers once is probed again on the next pass, so it is online within a
  few ms instead of when the scan comes round (boot: seen to online 120-196 ms before, 5-7 ms now).
- **Share (item 1):** slow scanning earns time at `scanShareMaxPct` (30%) scaled down by the poll load to
  `scanShareMinPct` (10%), and each pass spends it. Fast mode is limited to `fastScanMaxMs` (2 s, item 4).
- **I8 (item 4):** a device that answers on a slot but isn't known there is first probed on the main bus with the
  slots disabled; if it answers there it is recorded on the main bus.
- **Item 5** was done in Stage 7 (the scheduler identifies; the scanner reports).
- `devman/busstatus` reports `scan` (mode, list sizes, background count, share in use, background, confirmation
  and main-bus-check probe counts).

**Bench (FeatherS3, no multiplexer, 2026-10-16):** simulated unplug/replug of the MPU6050 (0x68 NACKed for 5 s, 3
times; same hook on the previous RaftI2C for comparison): identified again 45, 110, 45 ms after it came back (before:
194, 206, 86 ms); with the APDS9960 polled at 10 ms (about 47% poll load) 50, 185, 264 ms (before: 471, 429, 474 ms).
The 200 ms target for a high-priority main-bus address is met at light load. Boot identification: APDS9960 278-283 ms,
MPU6050 607-649 ms (before 469 and 758). Poll rates unchanged (`device-bench.js`: every scenario the bus can carry
at its rate with no misses; "all 10 ms", which needs 88% of the bus, as before). Probes per second are lower (341 vs
450 at default rates) as the share cap applies and probes go to addresses that can be identified.

**Axiom018 with a multiplexer (2026-10-16, over Tailscale and the serial console):** LSM6DS on slot 7, VL53L5CX on
slot 1 and VL6180 on slot 2 identified (VL6180 first at 0x29 with its `identPriority`); lists 25/22/56 with 13 in the
background; the RSAO hook registered its two ranges; the main-bus check ran for each new slot device (12 in a run)
without recording any against the main bus. Slot devices come online at boot about 1.4 s sooner (2.2 s after the
bus starts against 3.7 s). Simulated unplug/replug of the VL6180 on slot 2 (polls, detection and scan probes NACKed
for 5 s, 3 times; same hook on the previous RaftI2C): identified again 731, 996, 1,099 ms after it came back (before:
2,868, 2,034, 2,573 ms), about 185 ms of it the VL6180's own identification (its init has a 100 ms pause) - close
to the 1 s target. The LSM6DS on slot 7 misses about 10 polls in 100 s with gaps up to 0.3-0.5 s on both the new
and previous firmware (§11).

**Multiplexer items (done 2026-10-16, tested on the Axiom):**
- **Slot coverage by priority (item 3):** the schedule visits slots in a weighted cycle (smooth weighted round
  robin, so visits are spread): main bus 2, a slot with an online device `slotKnownWeight` (2), a slot with a change
  in the last `slotChangedMs` (10 s) `slotChangedWeight` (3), others 1. Every slot is in every cycle, so the worst
  case for a quiet slot is bounded by the cycle length (sum of the weights; 13 on the Axiom against 10 unweighted).
  `BusStatusMgr` keeps the last change time per slot. A slot stays selected for consecutive probes on it (the
  previous slot is deselected before another is selected, so only one is ever live), which raised the Axiom's
  probes per second from 263 to 313.
- **Multiplexer supervision (found while testing item 4):** a multiplexer address was only probed when the scan came
  round to it - about every 7 s on the main bus - and a hub's slots are reached through cached slot masks, so a hub
  unplugged and replugged could leave its devices unreachable for many seconds. One multiplexer address is now
  probed every `muxCheckMs` (100 ms), in turn, on its connection slot: a multiplexer going or coming is seen in
  300-360 ms. When one goes, the devices on its slots are reported gone (`goingOfflineOnSlot`) - otherwise a device
  that isn't polled (the VL53L5CX) stayed online through the outage.
- **A multiplexer change rescans only its slots (item 4):** after startup, a change marks that multiplexer's slots
  as changed and runs a fast scan (up to `fastScanMaxMs`) instead of restarting from the multiplexer-only phase.
- **Bench (Axiom):** VL6180 on slot 2 unplugged/replugged (simulated, 3 times): identified again 232, 748, 965 ms
  after it came back (Stage 9 before the slot weights: 1,282, 1,011, 1,333). Hub unplugged/replugged (the
  multiplexer at 0x75 and everything on its slots NACKing for 5 s, 3 times): slot devices identified again 309-482
  ms after it came back, against 1,180-1,406 ms with the old full restart (same build otherwise); the VL53L5CX is
  reported lost and restarted. FeatherS3 (no multiplexer) bench unchanged.

**Proposed changes**, to be confirmed against the Stage 0 measurements:
1. **Budget as a share of the bus, not a fixed time per pass.**
   - Scanning gets a target share of bus time, for example 10% when polls are running and up to 50% when
     they are not. Measured utilisation (Stage 1 item 7) adjusts it.
   - This replaces the fixed 2 ms per pass in slow mode. It lets an idle bus find devices faster, and keeps a
     busy bus free for polling.
2. **Scan where a device could be identified.**
   - The scan lists come from the catalogue index (Stage 3) and so include extended records at their declared
     priority.
   - Addresses that no record and no registered hook can claim move to a slow "background sweep". They still
     appear as unidentified elements in diagnostics, but far less often. A hook registers the address range
     it handles (RSAO: `0x0F` plus its assignable range), so RSAO commissioning is not slowed down.
3. **Slot coverage by priority.**
   - Slots where something changed recently, slots with known devices, and the main bus are covered more
     often.
   - A full slot cycle is still guaranteed within a configured maximum, so a hot-plug into a quiet slot is
     found within a known worst-case time.
   - Each list's sweep visits the addresses of one slot in a row, so the mux is switched once per sweep.
4. **Fixes to the phases:**
   - the main-bus "found" bits are cleared when the device goes (I8, moved here from Stage 1): before a device
     found on a slot is recorded there, check that it doesn't also answer on the main bus with the slots
     disabled, so a main-bus device that returns mid-sweep isn't recorded against a slot;
   - a minimum scan share while polling is heavy (Stage 1 measured 84 probes per second with the bus 83% busy
     polling);
   - a mux change rescans only that mux's slots, not the whole bus;
   - the fast mode after `requestScan(fast)` is limited in time.
5. **Identification is triggered by the scanner but not run inside it.** The scanner reports
   offline→online edges, and the identification scheduler (Stage 7) runs the work.
6. **Latency targets** (to be confirmed from the measurements):
   - a high-priority address on the main bus is identified in under 200 ms;
   - on any slot of an 8-slot mux, in under 1 s;
   - an unknown address, within the background sweep period.

**Verify:** plug-in latency, the bus share used by scanning, and poll timing (no regression from Stage 1),
each measured with 0, 1 and 2 VCPs polling.

**Repos:** RaftI2C (and RaftCore for the configuration and the hook range registration).

**Depends on:** Stages 1, 3 and 7.

---

**Update 2026-10-18 (RaftI2C):** online devices polled less often than once a second are now scanned as well. Before, an online polled device wasn't scanned at all, so its removal was seen only after 3 failed polls: 10-15 s at a 5 s interval. Devices polled at 1 s or faster are still left to their polls (`BusScanner::PRESENCE_FROM_POLLS_MAX_INTERVAL_US`). Verified on Axiom018 with an SHT41: removal is seen within a few seconds, with no spurious offline over 5 minutes.

### Stage 10: DeviceManager APIs

**Aim:** finer-grained management of device types and devices (G10). Every endpoint has a C++ equivalent on
`DeviceManager`.

| Endpoint | Purpose |
|---|---|
| `devman/types` | The type table: index, device type key, source, policy, generation. Paged |
| `devman/typeinfo` | As today, plus `deviceTypeName`, `src`, `policy` and `generation` |
| `devman/typepolicy?type=&policy=enabled\|disabled\|claim` | Change a record's policy at runtime - **done in Stage 8** (without saving; saving to the type file remains here, and then sets `restartRequired`) |
| `devman/typefile`, `devman/typefile/apply` | Status and apply (Stage 6; `restartRequired` Stage 8). Any endpoint here that changes records edits the type file and calls `markTypeFileChanged()` - it doesn't change the live table |
| `devman/identinfo?deviceid=` | Identification state, attempts, candidates in order, the last error |
| `devman/reident?deviceid=\|type=\|all` | Scoped re-identification - **done in Stage 8** |
| `devman/reinit?deviceid=` | Run init again without re-identifying |
| `devman/devenable?deviceid=&poll=0\|1` | Stop or resume polling of one device while it stays identified |
| `devman/devconfig` | As today, plus `save=1` to persist settings keyed by device ID, re-applied when the device is identified again |
| `devman/pollstats`, `devman/scanstats` | The Stage 0 measurements, permanently available |

Also:
- **Persisted device names** (P9): device names and device settings share one store keyed by device ID.
- **A firmware rule for the display name:** the device name if one is set, otherwise the type display name
  plus the element address. Consumers (RaftROS, the data logger, raftjs) use this rule.
- **Every endpoint that does bus work** uses deferred responses.

**Repos:** RaftCore, raftjs, the wiki REST page.

**Depends on:** Stages 5–8, for the items that rely on them.

**Status (2026-10-16): done in RaftCore, RaftI2C and raftjs**, with these decisions:
- **One settings file**, `/local/devsettings.json` (DevMan `deviceSettingsFile`), keyed by device ID:
  `{"devices":{"1_68":{"name","role","type","devconfig":{...},"poll":false}}}` (`DeviceSettingsStore`, host
  tested). `save=1` on `setname`, `setrole`, `devconfig` and `devenable` writes it. It is loaded in `postSetup`
  after the type file. Names and roles apply at once. devconfig settings apply when the device is identified as
  the saved `type` (the type key, so they survive index changes).
- **`devenable`** sets `BusAddrRecord::pollingDisabled`. A device that is not polled stays identified and online,
  and it can still be sent commands. The setting is held in DeviceManager by device ID, so it persists while the
  device is offline, and it is re-applied on Identified/TypeChanged.
- **`reinit`** is queued to the bus task (`DeviceIdentMgr::serviceReInitRequests`). That runs the init steps, then
  re-applies the devconfig settings through the StillMatches path. The response is deferred and completed from
  `loop()`, with a 5 s timeout. If the transport can't defer, it returns `queued`.
- **`identinfo`** reports the BusStatusMgr state plus the candidates in the order they are tried, and the hook.
  **`types`** is paged (default 50). **`reserved`** lists the addresses claimed by type records only, not
  addresses a sysmod reserves.
- **`typeinfo … full=1`** returns `record` in the type file format (`recordToFileEntry`, which round-trips every
  catalogue record, host tested). **`typepolicy … save=1`** edits the type file (`setPolicyInFile`, host
  tested) and sets `restartRequired`.
- **The display name** is the name if one is set, else devInfoJson `name` (or the key) plus the device ID:
  `getDeviceDisplayName()`, and `displayName` in `listdevs` and per-device `typeinfo`. `listdevs` now escapes
  names.
- **`pollstats`/`scanstats` dropped**: `busstatus` already has them, and it is now documented on the wiki.
- **N9 fixed:** `registerForDeviceData("bus:addr@slot")` applies the slot, and a device name is resolved.
- **raftjs:** wrappers only (`listDevices`, `setDeviceName/Role`, `getDeviceTypes`, `getIdentInfo`,
  `reIdentify`, `reInitDevice`, `setDevicePolling`, `setTypePolicy`, `getTypeFileStatus`,
  `getReservedAddresses`, `getDeviceDisplayName`), with tests.
- **Bench:**
  - On the FeatherS3, every endpoint was exercised. A restart kept the name, the stopped polling and devconfig
    (100 ms interval re-applied). `typepolicy save=1` created a type file, then emptied it.
  - On the Axiom (serial console), `identinfo` and `devenable` were checked on slot devices (`1_229`), and the
    deferred `reinit` of the LSM6DS completed.

---

### Stage 11: Remaining naming changes visible to clients

**Aim:** finish G11 at the API and client level without breaking existing clients.

- **REST:**
  - accept `deviceTypeIndex` alongside `type=<number>`, and `deviceTypeKey` alongside `type=<key>`;
  - document the canonical form.
- **raftjs** (additions only, §9.1 rule 4, R4):
  - new fields `displayName`, `deviceTypeKey` and `busNum`;
  - `DeviceTypeInfo.name`, `DeviceState.deviceType` and `busName` keep their current meaning, are marked
    deprecated, and change only at a raftjs major version.

  This needs a matching raftjs release and a note for the WebUI.
- **Wire formats** `_i`, `dtIdx` and devbin keep their names. The new fields are additions.
- **Wiki:** fix the documentation drift in review §6 and the pages for the record format and REST API.

**Depends on:** Stage 10.

**Status (2026-10-17): done in RaftCore, raftjs and the wiki.**
- **REST:**
  - `typeinfo`, `typepolicy`, `reident` and `demo` accept `deviceTypeKey=` and `deviceTypeIndex=`
    (`DeviceManager::getRequestedType`). If more than one is given, the index is used first, then the key,
    then `type=`.
  - `type=` behaves as before: a value starting with a digit is tried as an index, then as a key.
  - The wiki REST page documents the canonical forms ("Naming a device type"). Responses are unchanged.
  - Bench on the FeatherS3: every form, the not-found and missing errors, and the legacy `bus=&type=` lookup.
- **raftjs** (additions only):
  - `DeviceState.displayName`, `deviceName`, `deviceTypeKey` and `busNum` (all optional fields), plus
    `DeviceTypeInfo.typeDisplayName` and `deviceTypeKey`.
  - These are taken from the firmware's `displayName` and `deviceTypeKey`. Older firmware falls back to the
    same rule, and to `devinfo.type`.
  - `DeviceTypeInfo.name`, `DeviceState.deviceType` and `busName` keep their values and are marked
    `@deprecated`.
  - The stale-name problem (principles §2.3) is fixed: `setDeviceName` and `setDeviceRole` update the cached
    data and the state at once, and `addDeviceNameChangedCallback` reports changes.
  - Tests cover new and older firmware, devjson, and the rename.
  - Note for WebUI and app authors: raftjs `devdocs/device-identity-fields.md`.
  - The dashboard example uses `displayName`.
  - A raftjs minor release (2.6) is needed. The version number has not been changed.
- **Wiki:** the publishing page has a section on showing devices in a client. The review §6 drift table was
  checked again; every item is already fixed on the wiki. `system-device-tagging-plan.md` is corrected for C8.

---

### Stage 12: Device type record improvements

Added 2026-10-16. Improvements to the records themselves - their detection, initialisation and polling - as
opposed to the machinery that uses them. Deferred until after Stage 7 because several items depend on it
(detection-strength ordering, pauses that don't block the bus) and on Stage 4 (the CRC check). Items can be
taken one at a time after that, in any order; each needs the device to verify on hardware.

**Aim:** every record identifies its device reliably, without matching other devices at the same address and
without disturbing a device that is already running, and initialises and polls it correctly.

**Items:**
1. **Detection for records that have none** - the twelve I2C records reviewed in `device-detection-review.md`
   (VEML6040, AHT20, SHT40, SGP40, MPRLS, MT6701, QwiicLEDStick, SCD30, SCD40, STCC4, GravityO2, AMG8833). The
   review gives the ID mechanism and a proposed detection string for each, and the constraints that apply:
   - a detection that can fail on a genuine device (for example only while it is measuring) exposes it to the
     RSAO probe, so it is worse than none;
   - detection must work on a device that is already running, not only before its `initValues`.

   In summary:
   - low-risk checks for MPRLS, GravityO2, SCD30, QwiicLEDStick, VEML6040 and AMG8833;
   - a weak status-bit check for AHT20;
   - serial-number checks for SHT40 and SGP40, once the Stage 4 CRC check exists;
   - SCD4x and STCC4 after Stage 7, since they need a stop command first;
   - MT6701 stays empty.
2. **The shadowing cases** found in Stage 3: VEML6040 before MXC400xXC (0x10), QwiicLEDStick before TrillBar
   (0x23), AHT20 before TrillRing (0x38) and SHT40 before TrillHex (0x44-0x46). The Stage 7 strength ordering puts the
   records without detection last, so the others can now be identified. **Check on hardware** that the device the
   no-detection record describes doesn't pass the detection now tried first (an AHT20 against TrillRing's 8-bit check,
   an SHT40 against TrillHex's, a VEML6040 against MXC400xXC's, a QwiicLEDStick against TrillBar's) - giving those
   records detection (item 1) removes the risk. Also changed by the ordering: VL6180 (8 bits) would be tried after
   VL53L0X/L1X/L4CD/L5CX (16 bits) at 0x29, so it was given `identPriority: 1` (2026-10-16, RaftCore and Axiom) to
   keep it first, as on `main` (in RaftCore since `2`, with VL53L0X/L1X/L4CD at `1`, so that key order keeps today's
   order - record management plan phase 1a) - its detection only reads its read-only model ID, so trying it first is safe for
   the other parts; review it with item 4. AS5600 (6 bits) after AdafruitSoilSensor and TrillCraft at
   0x36, MCP9808 before ADXL313 at 0x1d, CAP1188 before CAP1203 at 0x28.
3. **Weak detection in records that have some** - for example MXC400xXC checks one byte (`0x0E=0x02`). Review
   these the same way.
4. **ST time-of-flight sensors (placeholder).** The VL6180 record was found (2026-10-16) not to initialise and
   poll the device correctly; it is being fixed separately. Review the other ST parts for the same kind of
   problem at the appropriate time: VL53L0X, VL53L1X and VL53L4CD in the RaftCore catalogue, and Axiom's
   VL53LxCX support (VL53L5CX, supplied through its ULD task as an `ext` record). Check their initialisation
   sequences, poll commands and data layout against ST's API/ULD drivers.
5. **Other record fixes found so far:**
   - SGP40 vs SGP41: an SGP41 at 0x59 would be identified as an SGP40 and then fail to poll (it doesn't have
     the SGP40's measurement command 0x260F). Detect the variant, or add an SGP41 record;
   - alternative addresses missing from records: STCC4 0x65 (ADDR pin high), Qwiic LED Stick 0x22 (jumper),
     MT6701 0x46 (reprogrammed);
   - the Robotical servo and water pump records pause 500 ms in detection, blocking the bus each time a device
     at one of their addresses is identified (fixed by Stage 7 item 11, or by shorter pauses);
   - init strings that don't do what they appear to (reported by the Stage 4 grammar): MAX30101 `0x11=0x21` and
     TMP117 `0x01=0x0220` write only the register (should probably be `0x1121` and `0x010220`), and MXC400xXC
     `0x01=0b00010000` reads a byte from register 0x01 instead of writing 0x10 to it. Check each against its
     datasheet and fix (in RaftCore and Axiom).

**Review (2026-10-18):** every record has been desk-reviewed against its datasheet and drivers -
`device-type-record-review.md`, with proposed changes, what needs hardware, and decisions needed. It also notes
that since Stage 7 detection pauses of 10 ms or more no longer block the bus during scheduled identification (they
still do in `reIdentifyDevice()` / `identifyDevice()`).

**Verify:** for each record changed - the device is identified from power-up and when already running (after a
firmware restart without a power cycle), its neighbours at the same address are still identified correctly,
and its readings match the device's own reference (vendor tool or driver).

**Repos:** RaftCore (catalogue), Axiom (its catalogue and VL53LxCX).

**Depends on:** Stages 4 and 7.

---

## 6. Goals and findings by stage

| Goal | Stages |
|---|---|
| G1 base records in flash | 2 |
| G2 memory-efficient extended records, higher limit | 2 (packed storage, byte budget), 6 (`devInfoJson` in the file, optional) |
| G3 with and without PSRAM | 2 (allocator and defaults), 0 (measurement) |
| G4 processor efficiency | 2 (lock-free index reads, binary search by name), 3 (CSR index), 4 (compiled operations, `PollPlan`) |
| G5 runtime disable | 5 |
| G6 file at startup and on change | 6 (reset; identification waits for the file), 8 (`restartRequired`; live re-identification and policy) |
| G7 derivation at runtime, first-class extended records | 3 |
| G8 scanning | 3 (lists from the index), 9 |
| G9 polling | 1 |
| G10 DeviceManager APIs | 10 (plus pieces in 6–8) |
| G11 naming | 0 (decide), every stage (apply), 11 (client-visible) |

| Finding | Stage |
|---|---|
| C5, I1, I2, I5, I6, I7, I8, I14, I15, I16 | 1 |
| C6, C7, C8, C11, C12 | 2 |
| Ordering and shadowing (review §5.2), scan-list snapshot (§5.16) | 3 |
| C4 (parser), C9, C10, C13, I4 | 4 |
| A4 (protection by role), review §7.2 | 5 |
| Review §9.2-H (readiness instead of `postSetup` order) | 6 |
| A1, I3, I9, I10, I11, I17 | 7 |
| Review §9.2-I (record lifetime), raftjs cache | 8 |
| C15, raftjs naming, review §6 docs | 10–11 |
| Records without detection, shadowing, record fixes (`device-detection-review.md`), ST ToF review | 12 |

Not in this plan, and tracked in the review: Axiom-only items A2, A3, A5 and A6; I13 (multi-consumer
deletions); the runtime decoder for records that are only data (review §9.2-G). The runtime decoder becomes
more valuable once Stage 6 lands, and should be decided then.

---

## 7. Testing approach

- **Host unit tests** (RaftCore `unit_tests`, RaftI2C tests) for:
  - the type table;
  - the index builder (including the equivalence with the generated tables);
  - the tokenizer;
  - the poll scheduler.

  The scheduler is easy to test on the host because time is passed in.
- **Bench script** from Stage 0, run before and after every stage, with the results stored next to this
  document as dated tables, as in review §13.
- **Hardware matrix:**
  - FeatherS3 (PSRAM) and a build with PSRAM disabled;
  - a board with an 8-slot mux;
  - Axiom with RSAOs, for Stages 3, 5, 7 and 9.
- **Compatibility:** the wire formats don't change in Stages 0–9. raftjs and RaftROS changes are additions
  until Stage 11.

---

## 8. Decisions

All decided on 2026-10-15. Each decision took the recommendation made in this plan. D16 and D18–D24 are
listed in §9.6 and §10.6; those still open are decided at the stage that needs them.

| # | Question | Decision |
|---|---|---|
| D1 | Names for the record sources | **Base** and **extended**, as already used in the code and in principles §2. (Revised 2026-10-15: "built-in"/"runtime" was first agreed, then dropped to avoid renaming code and external APIs.) `DeviceTypeRecordDynamic` keeps its name |
| D2 | Should an override keep the base record's index? | Yes, if the override is applied before the type table is `Ready`: one key, one current index. A record with the key added after `Ready` gets a new index, so an index never changes meaning within a session (live reload was dropped from Stage 8, 2026-10-16). This refinement is confirmed by D14 (§3.8 rule 1) |
| D3 | Default capacities and byte budgets | 32 records / 16 KB without PSRAM; 256 records / 256 KB with PSRAM. Confirm against Stage 0 measurements. **Stage 2:** a typical record measured 538 B, so the default without PSRAM is 20 KB (32 records) |
| D4 | Keep `devInfoJson` in the file on boards without PSRAM? | An option, off by default. Enable it on boards that are short of RAM |
| D5 | First version of "file changed" | Reset first (Stage 6); restart identification without a reset later (Stage 8). **Revised 2026-10-16:** a record change is always applied by a restart (`restartRequired` tells the client one is needed); Stage 8 does scoped re-identification and live policy only |
| D6 | Can identification wait for ever for a missing or slow source? | No. Wait with a timeout (5 s proposed), then continue with the base records and log it |
| D7 | Default and minimum poll interval | 100 ms default, 2 ms minimum |
| D8 | Catch-up policy for late polls | No bursts. Skip to `now + interval` and count the miss |
| D9 | Should addresses that no record can claim be scanned only in a slow background sweep? | Yes. Hooks register the address ranges they handle |
| D10 | Should policy changes made over REST be persisted, and where? | Yes, in the type file as `disable` entries, so there is one source of truth |
| D11 | Should the runtime decoder (review §9.2-G) be scheduled? | Decide after Stage 6. **2026-10-19:** yes - attribute decoding done, code interpreter planned (`runtime-decoding.md`), each behind a build flag |
| D12 | Term for the record identifier | **Device type key** (§3.9). Done 2026-10-15: documentation (principles §2, plan, wiki); `deviceTypeKey` added to the `typeinfo` response; inside the Raft libraries, non-API names (parameters, locals, comments, log text) renamed. Names that are part of an API or contract are unchanged |
| D13 | What happens to `deviceType` and `devInfoJson.type` in the JSON? | **Revised and done 2026-10-15.** `deviceType` is optional: the generator fills it from the key and fails the build if it differs; the RaftCore catalogue omits it, and generated output is byte-identical. `devInfoJson.type` is **kept permanently** with its current values, because clients identify devices by it; the generator warns about new mismatches. (Originally: remove both, inserting `type` into responses for one release.) |
| D14 | Adopt the index rules of §3.8? | Yes. Rules 1–3 and 5 go into Stage 7 (rule 1's live-override case was dropped with live reload, Stage 8 revised 2026-10-16). **Rule 4 revised 2026-10-15:** no type table ID in the publish streams; clients clear index-keyed caches on (re)subscribe (see D16) |
| D15 | Adopt the compatibility policy in §9.1? | **Yes** (decided 2026-10-15) |
| D17 | Static devices with the same key but different content (C8)? | **Reject the second one** with a clear error, and require a distinct `type` in the configuration. Keys are not changed automatically (decided 2026-10-15) |

### 8.1 Follow-ups from the decisions

- Update `device-type-management-principles.md` §2 for D12 (the device type key). Base and extended records
  (D1) need no change there. Do this in Stage 0.
- D3, D6 and D7 contain numbers to confirm or tune against the Stage 0 measurements.
- D11 is revisited when Stage 6 is complete.

---

## 9. Backward compatibility and external contracts

Review added 2026-10-15. It checks each stage against the contracts that code outside RaftCore and RaftI2C
depends on, and lists the changes needed in raftjs and other clients. Evidence was checked in raftjs
(`717eb45`), RaftROS, RaftSysMods and RoboticalAxiom1 `main`.

### 9.1 Compatibility policy (proposed, D15)

raftjs already supports firmware back to Cog v1.9.5 (raftjs `devdocs/legacy-firmware-compatibility.md`),
and apps carrying older raftjs versions are in the field. Both directions therefore matter:

- **New firmware, old client:** must work at least as well as today.
- **New client, old firmware:** must keep working, through capability detection (as raftjs already does for
  the two `typeinfo` URL forms).

Rules:
1. **Wire formats change only by additions that old parsers ignore.** What old raftjs ignores was checked in
   code:
   - devjson: top-level keys and per-device keys starting with `_` are skipped (`RaftDeviceManager.ts`
     `processClientMsgJson`);
   - JSON responses: extra fields are ignored;
   - devbin: **nothing can be added**. Any change to the envelope or record header breaks deployed raftjs
     (item 1 below).
2. **Catalogue JSON keeps accepting old forms for at least one release**, with build warnings, before they
   are rejected.
3. **Public C++ APIs are not renamed** (Stage 0 rule); new functions are added alongside.
4. **raftjs public types** change only by adding fields. A field whose meaning changes gets a new name, and
   the old one is removed only at a major version.
5. **Behaviour changes that clients can see** (rates, presence events, timings) are listed in release notes.

### 9.2 Contracts affected

| # | Contract | Used by | Stages | Change | Compatible? |
|---|---|---|---|---|---|
| 1 | **devbin stream** (envelope, record header, `devTypeIdx`) | raftjs, apps | — | **Unchanged.** An earlier version of this plan put a type table ID in the envelope. That would have broken deployed raftjs: it accepts magic 0xDB–0xDF, then tells EnvelopeV2 from V1 by probing for a valid record at offset 5, then 4 (`processClientMsgBinary`), so a longer envelope fails both probes and the whole message is dropped. The ID is no longer needed (§3.8 rules 4–5) | Yes |
| 2 | **devjson stream** | raftjs, apps, tools | — | **Unchanged** (the `_tt` key proposed earlier is withdrawn) | Yes |
| 3 | **Meaning of `_i` / `devTypeIdx`** | raftjs, RaftROS, DataLogger | 2, 5, 6, 8 | Base indices are unchanged for a given build. A boot-time override keeps the base index (D2). **New:** file records get indices in file order, after the static devices; static devices register first, so their indices stay as today. Within a firmware boot an index never changes meaning (§3.8) | Yes. Across boots, raftjs clears index-keyed type data on (re)subscribe (R1) |
| 4 | **`devman/typeinfo`** | raftjs (two URL forms), apps | 3, 5, 8, 10 | The response adds `deviceTypeKey` (done), `src` and `policy`. `devinfo.type` is unchanged and stays (D13). **The `bus=<n>&type=<index>` form must stay:** current raftjs uses it for *every* devbin record to choose the payload format, not only for old firmware | Yes, if `bus=&type=` keeps its behaviour |
| 5 | **`devman/listdevs`** | raftjs, tools | 7, 10 | Adds fields (identification state) | Yes |
| 6 | **`devman/devconfig`** | raftjs, apps | 1, 7, 10 | `intervalUs` is clamped to ≥ 2 ms (D7). `numSamples` is capped by bytes (I6). Settings survive re-identification (Stage 7). Adds `save=1` | Mostly. A request below the clamp gets less than it asked for, so the response must report the value applied |
| 7 | **Actual poll rates** (behaviour) | Anyone reading samples | 1 | Rates rise to the configured value (from 2–20% below it), and bus load rises with them. The default `i` (D7) affects only records without `i`: every shipped catalogue sets both `i` and `s` (checked: RaftCore, Axiom, RSAO, RaftROS examples) | Behaviour change; release note |
| 8 | **Presence and deletion events** (behaviour) | raftjs, RaftROS, Axiom | 1, 7 | Devices behind a lost mux or a power-cycled slot are now reported offline and deleted, where they used to stay as ghosts. Re-verification can produce `TypeChanged` | Correct, but visible; release note |
| 9 | **Type change at the same device ID** | raftjs | 5–8 | raftjs caches type info per device ID (`_cachedDeviceTypeRecs[deviceKey]`) and fetches it only when the device state has none. `removeDevice()` deletes the device state but **not** that cache entry, and nothing clears the cache on reconnect. So when a device's type changes at the same device ID, a deployed raftjs keeps decoding with the old type until the app is reloaded. A deletion notice from the firmware does not help. This already happens today (CRC re-identification), and Stages 5–8 make it more likely (disable, override, file changes, re-verification) | **No.** Needs raftjs change R1. Old apps must be reloaded after changing the type file |
| 10 | **Device type catalogue JSON format** (RaftCore, Axiom, third-party, device-library files) | Generator, MicroPython generator, Axiom scripts, device library | 3, 4, 6 | D13 (done): `deviceType` is optional and, if present, must equal the key; the generators take the identifier from the key. Catalogues that still have the field stay valid. Axiom's scripts don't read `deviceType`; the Axiom branch's device library already accepts it being absent. `excludeFromAddrMap` → `probeOnly`: used by all 11 RSAO records, so both names are accepted. Stage 4's strict parser may reject records that build today (odd-length hex, a bare `pNN` used as write data, more than one `=`) | Yes, with the transition in D13 and D19 |
| 11 | **Generated artefacts** (`poll_<key>` structs, field descriptors, MicroPython `decode_device_data(deviceType…)`) | RaftROS, DataLogger, MicroPython users | 2, 3 | Names come from the key, which equals `deviceType` everywhere, so they don't change. Stage 3 stops generating the address and scan tables; nothing outside `DeviceTypeRecords.cpp` should include them (to verify) | Yes |
| 12 | **RaftCore C++ APIs** | RaftROS AutoPub, Axiom (DevicePower, DataLogger, VL53LxCX, RSAOSysMod), RaftSysMods `BLEBusDeviceManager` and `BusBLE`, RaftCore `BusSerial` | 0, 2, 3, 7, 8 | Public names are not renamed (Stage 0 rule), so no aliases are needed; `DeviceTypeRecord::deviceType` keeps its name and holds the device type key. `BusAddrStatus` gains fields: keep the existing constructor, which BLE calls positionally. New `RaftBusDevicesIF` virtual functions get default bodies. Stage 8 (revised) frees and re-assigns nothing within a session, so consumers may keep record pointers; a future live-reload stage would need them to keep an index plus the generation instead | Yes for 0–8 |
| 13 | **Ident hook semantics** (`registerNewDeviceIdentHandler`) | Axiom RSAO | 7, 9 | Today an unidentified device is offered to the hook every ~3 s. Axiom's commissioning debounce relies on this: each retry refreshes `requestedMs` (review A2). Stage 7 stops re-running the hook on `NoMatch`, and Stage 9 asks hooks to register their address ranges. `Deferred` must still be retried, and the Axiom change has to land with Stage 7 | **No, unless Axiom changes in step** |
| 14 | **RaftROS topic names** | ROS users (launch files, bags) | 2, 11 | Built from the device type key and the class map, whose values don't change. **Exception:** the C8 option of suffixing a key (`DemoDevice#2`) would rename topics, and `#` isn't valid in a ROS name. See D17 | Yes, with D17 |
| 15 | **SysType configuration keys** | Product SysTypes | 1, 6, 9 | New keys are additions. `loopYieldMs` becomes the *maximum* sleep. `busScanPeriodMs`, `fastScanMaxUnyieldMs` and `slowScanMaxUnyieldMs` become caps on the share-based budget. None of them is set in the Axiom or RaftROS example SysTypes; other products are not known | Yes, if the old keys keep working as caps |
| 16 | **Boot behaviour** | Apps and tools that expect data soon after boot | 6 | Identification waits until every record source is ready, for up to 5 s (D6). If the file system is mounted in `DeviceManager::setup()` the file is loaded there and there is no wait | Small delay at most; release note |
| 17 | **Applying a type file** | raftjs, tools | 6 | `devman/typefile/apply` resets the device: the client is sent the response, then the connection drops | New endpoint; document it |
| 18 | **Unidentified elements** | Diagnostics | 9 | D9 makes unclaimable addresses slower to appear. Unidentified elements are not published in devjson or devbin (`DeviceIdentMgr::getQueuedDeviceDataJson` skips an invalid index), so only diagnostics see the difference | Yes |
| 19 | **raftjs public types** (`DeviceTypeInfo.name`, `DeviceState.deviceType`, `busName`) | Apps, dashboards, Axiom and Cog apps | 11 | The plan changes what these fields hold. Under rule 4 that is not allowed. Instead, add `displayName`, `deviceTypeKey` and `busNum`, keep the old fields with their current meaning, and deprecate them | Yes, as an addition |

### 9.3 Required raftjs changes

| # | Change | When | Works with old firmware? |
|---|---|---|---|
| R1 | **Decode by index** (§3.8 rules 4–5): decode metadata is cached by device type index only, and that cache is cleared when raftjs (re)subscribes. Per device ID, keep the last index seen plus instance data (name, role); when a record's index differs from the last one, switch to that index's type data. Replace today's per-device-ID type cache (`_cachedDeviceTypeRecs[deviceKey]`) with this. The per-device `typeinfo?deviceid=` request is still used, but only for its instance data | **Now**, independently of the firmware stages (D18). It fixes a problem that exists today | Yes (pure client logic) |
| R2 | *(Withdrawn.)* No type table ID is needed by clients; see §3.8 rule 4 and D16 | — | — |
| R3 | **Read `deviceTypeKey` when present** (the optional field was added to `RaftDevTypeInfoResponse` on 2026-10-15). `DeviceTypeInfo.type` stays as it is, since firmware keeps sending it (D13) | With R1 | Yes |
| R4 | **New fields:** `displayName`, `deviceTypeKey`, `busNum` on the device state; deprecate the old meanings | Stage 11 | Yes (fall back to today's derivation) |
| R5 | **New API wrappers:** `types`, `typepolicy`, `typefile`, `identinfo`, `reident`, `reinit`, `devenable` (`pollstats`, `scanstats` dropped: in `busstatus`) | Stage 10 - done | The firmware response is returned, so older firmware gives `rslt` `fail` (unknown command) |
| R6 | **Keep both `typeinfo` URL forms** and the per-record payload-format probe unchanged | Throughout | — |
| R7 | **Tests:** replay captured traffic from old and new firmware (as in the legacy-compatibility work), plus a type-change-at-the-same-device-ID test for R1 | With R1 and R2 | — |

### 9.4 Other clients

| Client | Change needed | Stage |
|---|---|---|
| RaftROS AutoPub | Upstream the fork's re-attach on a type change and dropping callbacks with a stale index (review §9.1). Keep an index plus the table ID, not record pointers. Check that topic names are unchanged | 7, 8 |
| Axiom RSAO | Hook semantics (item 13): debounce from first-seen time (A2), register its address ranges (D9), and change in step with Stage 7. Generate the type registry from `hwTypeId` (Stage 3) | 3, 7, 9 |
| Axiom DataLogger, DevicePower, VL53LxCX | Handle `TypeChanged` and `Lost`. Stop keeping decoders or record pointers for the whole session | 7, 8 |
| Axiom device library (branch) | Align its file format with the Stage 6 loader (`devTypes`, `disable`). Its "suppress by name" meant hidden from lookups too; under Stage 5 it hides from identification only. Move its protection rules to SysTypes roles | 5, 6 |
| Axiom scripts and CI (`split_rsao_records.py`, `verify_split.py`, `check-descriptor-spec.mjs`) | Accept `probeOnly` (they don't read `deviceType`) | 3 |
| RaftSysMods `BLEBusDeviceManager` and `BusBLE` | No change needed for naming (public APIs are not renamed). Optionally adopt identification events | 0, 7 |
| RaftCore `BusSerial` | Same as BLE | 0, 7 |
| Python tools (Axiom `serial_protocol.py`, bench scripts) | None. New scripts should identify types by `deviceTypeKey` rather than `devinfo.type` | — |
| raft CLI | None found | — |
| Wiki (record format, REST API, identification pages) | Update with each stage; mark deprecated fields | All |

### 9.5 Plan corrections made by this review

- **§3.8 rules 4–5 and Stage 8 item 4:** devbin and devjson are not changed at all. Clients decode by each
  record's index and clear index-keyed caches on (re)subscribe; the type table ID in the streams is
  withdrawn.
- **Stage 2 item 6 (C8):** don't suffix keys, because that renames ROS topics and the suffix isn't valid
  there. See D17.
- **Stage 4:** release the strict parser as warnings first (D19). Run it over every catalogue before
  rejecting anything.
- **Stage 7:** lands together with the Axiom RSAO change (item 13).
- **Stage 11:** raftjs changes are additions only (item 19, R4).

### 9.6 Decisions raised by this review

| # | Question | Recommendation |
|---|---|---|
| D15 | Adopt the compatibility policy in §9.1? | Yes |
| D16 | How does a client know its index-keyed type data is stale? | *(Revised 2026-10-15.)* It is never stale within a subscription (§3.8 rule 1). raftjs clears it on every (re)subscribe, which happens after every firmware reboot. **devbin and devjson are unchanged**, and no type table ID is published |
| D17 | Static devices with the same key but different content (C8)? | Reject the second one with a clear error, and require a distinct `type` in the configuration. Don't change keys automatically |
| D18 | Release raftjs R1 ahead of the firmware stages? | Yes. It fixes a problem that exists today, and it is what makes Stages 5–8 safe for clients that update |
| D19 | How is the strict parser (Stage 4) rolled out? | One release that warns (build and load), with an impact report over all known catalogues, then reject |

---

## 10. Alignment with the device library branch

Review added 2026-10-15 of Nikos's `feature/device-library` work, now at:
- **Axiom:** `bbaa98b` (2026-10-15). Five commits since the review: VL53L5CX ported into the device-library
  firmware, PSRAM releases v0.1.19 and v0.1.20, and a SPIRAM allocator fix.
- **RaftCore:** `d032e24`. The only new commit is the SPIRAM fix, which is already on `main`.
- **RaftI2C:** `91ce811`, unchanged.

### 10.1 The branch is already in the field

- `-devlib-host` and `-devlib-zenoh` builds are published to the public firmware updater manifest as test
  releases (Axiom `README.md`, `retain-current-devlib.mjs`).
- So some units may already hold a `/local/devlib.json`.
- A browser UI reads the `devlib` endpoint (the loader's comments describe what "the browser needs to
  know").

The file path, the file format and the `devlib` endpoint are therefore external contracts, and Stage 6 has
to keep them working or migrate them (D20).

### 10.2 The file format

Sources: Axiom `components/DeviceLibrary/DeviceLibraryLoader.cpp`, `devdocs/device-descriptor-spec.md` and
`tests/device-library/example-devlib.json`, all on the branch.

**The shape is the same as `DeviceTypeRecords.json`:** `{"devTypes": {"<key>": {record}}}`, with records
keyed by the device type key and the same record fields. `pollInfo` and `devInfoJson` are JSON objects,
as in the compiled files. A compiled record can be pasted into a library file unchanged.

**It is not identical.** Field by field:

| Field | `DeviceTypeRecords.json` (generator) | Library file (loader) | Matches the plan? |
|---|---|---|---|
| Top-level keys | Only `devTypes` is read; other keys are ignored | `devTypes`; other keys such as `_notes` are ignored | Yes |
| Record key | Used for C names; `deviceType` gives the runtime name | **Is** the runtime name | Yes (D13) |
| `deviceType` | Required in practice (an empty name is generated without it) | Optional; must equal the key if present, otherwise the record is rejected | Yes: this is exactly D13's transition rule |
| `_notes` (per record) | Ignored | Ignored | Yes |
| `addresses` | Parsed by the generator into the address map | Passed through. A record with neither `addresses` nor `deviceTypeId` registers but can never match; the spec calls this "the one structural mistake the device cannot report" | Yes; Stage 6 should warn about it |
| `detectionValues`, `initValues` | Not validated (C3 is now enforced at runtime) | Not validated, except for read lengths | Stage 4 adds validation for both |
| `pollInfo` | `c`, `i`, `s`, `ext`, `busHz`/`h`, `busHzSlots`/`hSlots`, `crc` | Same keys, passed through as JSON text. Since 2026-10-17 `busHz`/`busHzSlots` are no longer part of a record - they are DevMan `typeSettings` (record management plan 3.9, M11); a record with them gets a warning | Yes, with that exception |
| `pollInfo.s` | Present in every shipped record | Missing or 0 is **accepted** (0 to 256 allowed) | No: C5. Stage 1 defaults `s` to 1 and clamps it |
| `pollInfo.i` | Present in every shipped record | Not checked (review §7.3) | D7 clamps it |
| `devInfoJson.resp.b` | The generator checks it against the size computed from `c` | Becomes `pollDataSizeBytes`, and is checked against the runtime parser's size | Yes |
| `devInfoJson.type` | Present; differs from the key in 2 records | Passed through | Yes: kept, with the generator warning about new mismatches (D13) |
| `scanPriority` | Used (two-tier in practice) | **Accepted and ignored**; library records always go in scan list 0 | Stage 3 honours it, a change of behaviour for library records |
| `excludeFromAddrMap` | Used by all 11 RSAO records | **Ignored**, so a library RSAO record with `addresses` is address-mapped and reserved | Stage 3 honours it (as `probeOnly`, both names accepted) |
| `deviceTypeId` | Ignored by the generator; a hand-written table in Axiom holds the mapping | Validated (0–65535 or a `0x` string) and passed to the RSAO registry through a callback | Stage 3: `hwTypeId`, with `deviceTypeId` accepted as the old name |
| `disable` | **Not handled.** A `disable` entry placed in a compiled file would generate an empty record that replaces the real one | `true` means "do not identify this key"; the rest of the record is ignored | Stage 5 item 3 matches. The generator must handle it too (D23) |

**Conclusion:** the plan's type file should **be** the library format: `DeviceTypeRecords.json` plus
`disable`, with the D13 rules for `deviceType`. One grammar, one shape, accepted by both the generator and
the loader. The generator needs the `disable` handling and must take the name from the key (D13); the
loader needs the `scanPriority`, `excludeFromAddrMap`/`probeOnly` and `s` fixes.

### 10.3 Where the branch's behaviour differs from the plan

| # | Behaviour | Branch | Plan | Effect on library files already in the field |
|---|---|---|---|---|
| B1 | **Override** | The library record is tried first; the compiled record stays a fall-through candidate. The example file's `_notes` relies on this ("if these detectionValues fail to match, identification still falls through to the base definition") | D2: an override replaces the entry, with no fall-through | A library override whose detection is wrong stops matching instead of falling back. Confirm D2 (D21) and update the spec and the example |
| B2 | **Disable** | By name, kept in a separate list of up to 128 names. It also hides the record from name lookups (`typeinfo?type=`, `registerForDeviceData(name)`) and from RSAO address reservation | A policy byte on the key; identification only; lookups, decoding and reservation are unchanged | Disabled types still appear in `typeinfo`. Safer for RSAO reservation |
| B3 | **Unknown key in a `disable` entry** | Counted as suppressed (a typo reports success) | Not covered | Stage 6 reports it as "unknown key" (§10.5) - **Done (Stage 6)** |
| B4 | **Limits for untrusted records** | In the Axiom loader: file ≤ 64 KB, read ≤ 1024 bytes per command, `s` ≤ 256, `s` × slot ≤ 65,535 bytes (because of the `uint16_t` ring, I6) | Stage 2 adds a byte budget; Stage 1 makes the ring `uint32_t` | Move these limits into the RaftCore loader as limits for extended record sources. Replace the ring limit with a per-record byte cap (D22) |
| B5 | **Re-identification after loading** | `reIdentifyAllBuses()` after a load, plus an unconditional call in `RSAOSysMod::postSetup` on every boot | Stage 6 gating: no device is identified before the table is `Ready` | Both calls are removed when Stage 6 lands, ending the boot-time data gap (review §11 item 4) - **Done (Stage 6)** |
| B6 | **RSAO id mapping** | `DeviceTypeIdSink` callback into a registry that is sealed in `postSetup` | `hwTypeId` → index lookup in the catalogue index (Stage 3) | The sink, the registry and the sealing go. This also removes the dependency on SysMod order |
| B7 | **Capacity** | 20 records, shared with static devices; `devlib` reports `maxRecords` | D3: 32 / 256 records with byte budgets | Keep `maxRecords` in the status, and add the byte budget |
| B8 | **Protections** | Hard-coded: `MAX17048` cannot be disabled or replaced, and address 0x36 can't be claimed | Derived from SysTypes `role: "system"` plus `expectType` (Stage 5 item 4) | Done: Axiom018 SysTypes has `expectType: "MAX17048"` on the fuel gauge entry, and DevicePower checks the identified type before using it |
| B9 | **Status endpoint** | `devlib`: `file`, `present`, `applied`, `appliedCrc16` (CRC16-CCITT, matching `filehash`), `added`, `suppressed`, `rejected`, `maxRecords`, `maxFileBytes`, `firstError` | `devman/typefile` | Keep `devlib` as an alias with the same fields for one release; use the same CRC algorithm (D20) - **Done (Stage 6)** |
| B10 | **Config key and path** | `DeviceLibrary/devLibFile`, default `/local/devlib.json` | `DevMan/typeFile` | Default the type file to `/local/devlib.json` on Axiom, or migrate (D20) - **Done (Stage 6)** |
| B11 | **Parsing cost** | The whole file (≤ 64 KB) is read into memory, then each field is fetched with a path lookup, e.g. `getString("devTypes/<key>/pollInfo")` *(likely costly with many records; not measured)*. `pollInfo` and `devInfoJson` are stored as their raw text, including whitespace | Stage 2 packed records | The loader should walk the file once, and minify the JSON text before storing it |

### 10.4 Approach: don't merge, stay compatible, take the good ideas

Decided 2026-10-15: the branch is **not expected to be merged**. The plan's own stages deliver the
capability. Two aims follow:

1. **Stay compatible with the branch wherever there is no reason not to.** A library file written for the
   branch should load under Stage 6 and mean the same thing. The same goes for the `devlib` endpoint, the
   default path and the configuration key (D20).
2. **Take every good idea from it** (§10.7).

**Where the plan stays compatible:**
- the file shape;
- `_notes` and other unknown keys are ignored;
- `deviceType` is optional and must equal the key;
- `deviceTypeId` is accepted as the JSON name of the hardware type ID (D24);
- `disable: true` ignores the rest of the record;
- an empty `devTypes` is valid and means "no custom types";
- the per-file limits (64 KB file, 1024-byte reads, `s` ≤ 256);
- per-record rejection with the first error kept;
- the `devlib` status fields, with CRC16-CCITT matching `filehash`;
- the default path `/local/devlib.json`.

**Where the plan deliberately differs, and why:**

| Difference | Branch | Plan | Reason |
|---|---|---|---|
| Override | The compiled record stays as a fall-through candidate (B1) | It replaces the compiled record (D2) | Fall-through gives one key two indices and makes the published `_i` ambiguous (P3/P8). This is the main reason the principles review gave for change |
| Disable | Hides the record from name lookups and from RSAO reservation too (B2) | Hides it from identification only | Lookups, decoding and the UI must keep working for a disabled type, and the RSAO address must stay reserved |
| `scanPriority` and `excludeFromAddrMap` in a file | Ignored | Honoured | So that file records behave exactly like compiled ones (G7) |
| `s` missing or 0 | Accepted | Defaulted to 1 | C5. More permissive, so no branch file is refused because of it |
| Re-identification | A whole-bus clear after loading, and on every boot | Identification waits for the table to be ready | No boot-time data gap, and no hidden dependency on SysMod order |

The first two change the meaning of some existing branch files. They are listed in the Stage 6 release
notes, and Nikos is told before more units carry files that rely on fall-through (D21).

**Not taken from the branch:**
- suppression by name as the platform mechanism;
- whole-bus `reIdentifyDevices()`;
- `DeviceTypeIdSink` and registry sealing;
- the hard-coded MAX17048 / 0x36 protections;
- the `cmdraw` / `setCallbackFromBusTask` commits (superseded by deferred responses);
- shipping a reduced base catalogue in published releases (the test idea behind it *is* taken, §10.7
  item 6).

### 10.5 Issues from the review and principles documents not yet in the plan

Checked against review §5, §7 and §9 and principles P1–P15. Items already in the plan are not repeated.

| # | Issue | Source | Proposed stage |
|---|---|---|---|
| N1 | `addresses` is not validated: `parseIntList` accepts forms such as `0o51` and silently skips junk | Review §7.3; branch spec | 4 (the same strict parser) - **Done (Stages 4, 6)**: the generator and the type file loader check addresses strictly |
| N2 | Two records with the same `hwTypeId` are not detected, so a library record can silently re-map every servo | Review §7.3 | 3 (the index builder rejects or warns) - **Done (2026-10-17)**: a type file record is rejected, the generator warns, and a code-registered record is logged |
| N3 | A `disable` entry for a key that doesn't exist reports success | Review §7.3 | 6 (report as "unknown key") - **Done (Stage 6)** |
| N4 | A file record with neither `addresses` nor `hwTypeId` can never match | Branch spec | 6 (warn) - **Done (Stage 6)** |
| N5 | Records with no on-device decoder: native ROS announces an endpoint that never publishes, and DataLogger CSV skips them. Until D11 is decided, RaftROS should publish raw bytes or not announce the endpoint, and the limitation should be documented | Review §7.3, §9.2-G | 6 - **Done (2026-10-17)**: RaftROS doesn't announce a topic for a record with no on-device decoder (or one whose decoded record doesn't fit a mailbox, or when no mailbox is free); documented on the RaftROS wiki page |
| N6 | `typeinfo …&full=1` (on the branch) doesn't round-trip: it lacks `scanPriority`, `excludeFromAddrMap`/`probeOnly`, `hwTypeId`, the source and the policy. The library UI's "copy a record and add `disable`" workflow relies on it | Review §7.2 | 10: return the record in the file format, keyed by the device type key - **Done (Stage 10)** |
| N7 | The fallback hook runs wherever no record *matched*, not where no record *exists*, so the destructive RSAO probe can hit a known sensor that failed detection | Review §5.11; STCC4 notes | 5 or 9: the hook runs only at addresses with no `AllKnown` record, unless that record is `Disabled` (not `DisabledClaim`). Together with the hook address ranges (D9) - **Not done.** The hook still runs where no record *matched*; the hook address ranges (Stage 9) and Axiom's RSAO reserved and excluded addresses reduce the exposure |
| N8 | Per-device settings are lost on a short absence: OFFLINE moves straight to deletion, which erases the `BusAddrRecord` where Stage 7 keeps them | Principles P7 | 7: keep unsaved settings in DeviceManager, keyed by device ID, as well as the saved ones from Stage 10 - **Done (Stage 7)** |
| N9 | `registerForDeviceData("<busName>_<addr>@<slot>")` drops the slot, and a device name doesn't resolve in every path (`resolveDeviceIDAndBus`) | Principles P9 | 10 - **Done (Stage 10)** |
| N10 | The reserved-address set (for RSAO) is not exposed externally | Principles P10 | 10: an endpoint returning the bitmap from the catalogue index - **Done (Stage 10)**: `devman/reserved` |
| N11 | A board cannot disable or reprioritise records from its SysType ("this board never has X") | Principles P15 | 5/6: `disable` entries in a compiled catalogue or in SysTypes as another record source (D23) - **Done (Stage 5)**: `disable` entries in a compiled catalogue, and `typePolicy` in SysTypes |
| N12 | Device-specific code in the core (LSM6DS FIFO decode and timing) | Principles P4 | 1 (added as item 11) - **Done (Stage 1)** |
| N13 | `ext` records and `busHz` switching were not considered by the scheduler | Branch spec (VL53L5CX, LSM6DS) | 1 (added as items 9 and 10) - **Item 9 done (Stage 1)**; item 10 (bus speed in the tie-break) not done |
| N14 | One global ident hook per bus, rather than per record or per class | Principles P4 | Not planned. D9's address ranges are the first step; revisit if a second hook user appears |
| N15 | Hardware instance ID (RSAO serial) as a device's identity, so names and settings follow a re-addressed RSAO | Principles P9, P10 | Not planned; a candidate after Stage 10 |
| N16 | Static device IDs are `0_<position in the static device list>`, so reordering the configuration changes them | Principles §2.2 | Not planned; note in the Stage 10 documentation |
| N17 | RaftROS `MAILBOX_RECORD_SIZE` is hard-coded rather than derived from the generated struct sizes | Review §9.1 RaftROS | Outside this plan (RaftROS) |

### 10.6 Decisions raised by this section

| # | Question | Recommendation |
|---|---|---|
| D20 | How are the branch's file path, config key and `devlib` endpoint handled? | The type file defaults to `/local/devlib.json`. `DeviceLibrary/devLibFile` is read if `DevMan/typeFile` is not set. `devlib` stays as an alias of `devman/typefile`, with the same fields and CRC16-CCITT, for at least one release |
| D21 | Confirm D2 for library files: an override replaces the base record, with no fall-through, although the branch falls through? | Yes; a deliberate difference (§10.4). Tell Nikos now so the branch spec and example file can warn about it, and list it in the Stage 6 release notes |
| D22 | Where do the limits for untrusted records live? | In the RaftCore loader, applied to file and REST sources only; compiled records and code-registered records are not limited. Once I6 is fixed, replace the ring-size limit with a configurable per-record byte cap |
| D23 | Can a compiled catalogue (or a SysType) contain `disable` entries? | Yes. The generator emits them as policy settings, not records. This gives boards a way to say "never X" (N11) and removes the trap of an empty record being compiled |
| D24 | **Decided 2026-10-15: yes, `hwTypeId` (Stage 3).** Should RaftCore take over the RSAO-style "hardware type ID → record" mapping, and what is it called? | Decide in Stage 3. Today `deviceTypeId` is used only by RSAO records and Axiom's RSAO code. Recommendation: RaftCore takes it over (removing Axiom's duplicated registry and making file-loaded RSAO records work). Then the C++ name is generic (e.g. `hwTypeId`, matching principles §2) since the core holds no vendor knowledge (P4); it is documented as an ID the device reports when probed by an identification hook, in that hook's own numbering. The JSON field stays `deviceTypeId`. Not `rsaoWHOAMI`: that names the RSAO command, and WHO_AM_I is also a common register on ordinary sensors |

### 10.7 Ideas taken from the branch

All three branches were reviewed (Axiom `bbaa98b`, RaftCore `d032e24`, RaftI2C `91ce811`), including the
branch's diary and its tests.

| # | Idea | Where it is on the branch | Where it goes in the plan |
|---|---|---|---|
| 1 | **A host test that runs every shipped detection string through the real C++ parser.** This is how the branch found the binary-pattern bug in §10.8 | Axiom host harnesses that compile the pinned RaftCore parser and generator; diary 2026-10-14 | Stage 1 (with the fix in §10.8), then Stage 4's shared test vectors |
| 2 | **A descriptor spec written from the parser, plus a conformance script run in CI against every shipped catalogue.** Its rule: "a rule that refuses a profile the firmware runs is a wrong rule" | `devdocs/device-descriptor-spec.md`, `scripts/check-descriptor-spec.mjs` | Stage 4: move the spec into RaftCore (devdocs, then the wiki); run the conformance check in RaftCore CI over the RaftCore, Axiom and RaftROS catalogues |
| 3 | **Validate untrusted records where they enter**, using the runtime parser to compute the real poll size, check it against `resp.b`, and bound reads, samples and ring size. One bad record is rejected on its own, and the first error is kept | `DeviceLibraryLoader::applyJson` | Stage 6 (D22) |
| 4 | **"Present" versus "applied", with a CRC of the applied bytes** that uses the same algorithm as `filehash`, so a client can tell whether the file on disk is the one in effect | `devlib` endpoint | Stage 6 status, with the same field names (D20) |
| 5 | **An empty `devTypes` is a valid file** ("no custom types"), not an error reported on every boot | `applyJson` | Stage 6 |
| 6 | **Test additions for real:** build with a reduced base catalogue and supply the rest from a file | `DeviceTypeRecords.testsplit.json`, `check-device-library-selection.mjs` | Stage 6 verification, as a test build only: identification results from "reduced base + file" must equal those from the full base build. This is the proof that extended records are first-class (G7) - **Done (2026-10-17)**: `linux_unit_tests/test_reduced_catalogue`, built with half the RaftCore catalogue compiled and the other half loaded as a type file, gives the full catalogue's candidates, reserved addresses and scan lists (file records first among equals, as documented) |
| 7 | **A hardware acceptance gate** checking: inventory under streaming; raw-read isolation; acceptance by the real host decoder; detection of panics or unexpected reboots; file upload, CRC and persistence across a restart; PSRAM diagnostics | `tests/hardware/axiom_device_gate.py`, `test_device_gate.py` | Stage 0: build the bench script on this design and run it at the end of every stage (§7) |
| 8 | **Measure delivered rate as well as poll rate.** The branch measured a USB stream rate that fell from about 19–20 Hz to about 8 Hz with Wi-Fi enabled, an effect that predates its own changes | Diary 2026-10-15 | Stage 0: measure poll rate (at the bus) and delivered rate (at the client) separately, so G9 isn't judged on a number limited by the transport |
| 9 | **An atomic request consumed once at the worker boundary**, for work requested from another task | RaftI2C `1f2eeef` (`_reIdentifyRequested.exchange`) | Stage 8 (`requestReIdentify`) |
| 10 | **Capacity in the status** (`maxRecords`, `getMaxExtendedDeviceTypeRecords()`, `getNumExtendedDeviceTypeRecords()`) | RaftCore `8b4e63e`, `devlib` | Stage 2 API and Stage 6 status, together with the byte budget |
| 11 | **The whole record from `typeinfo`** (`full=1`) | RaftCore `53b3ff4` | Stage 10 (N6), in the file format so it round-trips |
| 12 | **DeviceManager regression tests:** `listdevs` keeps identified online devices that have no samples yet; direct devices reject bus-only `devconfig` and `cmdraw`. The fixes are on `main` but the tests are not | RaftCore `unit_tests/main/DeviceManager_regression_test.cpp` | Stage 0: port these two tests |
| 13 | **Host tests for `SpiramAwareAllocator`** across PSRAM present, missing, disabled and exhausted | Axiom `tests/spiram-allocator/` | Stage 2 (PSRAM-aware storage depends on it): port into RaftCore unit tests - **Done (2026-10-17)**: ported as `linux_unit_tests/spiram_allocator` (eight configurations, run by `make check`) |
| 14 | **Error messages that name the record and the rule**, e.g. `"<key>: deviceType field says '<x>'"`, `"<key>: reads N bytes in one command, limit M"` | `applyJson` | Stage 4 and 6 error reporting |
| 15 | **Data produced by firmware code, described by a record** (`pollInfo.ext`, the VL53L5CX ULD task) | VL53L5CX port | Stage 1 (N13), and the type file: an `ext` record can be supplied, but data only flows if the firmware contains the producer, which the loader should report - **Done (2026-10-17)**: `DeviceTypeRecords::registerExtDataSource()` (Axiom's VL53LxCX SysMod registers VL53L5CX); the loader warns for a file record with `pollInfo.ext` that no code supplies |

### 10.8 Bug found through the branch: binary detection patterns were parsed as hex (fixed 2026-10-15)

Found by the branch's host tests (diary 2026-10-14: "embedded-0x binary-expression precedence bug") and
confirmed on `main` by reading the code.

- **The cause.** `DeviceTypeRecords::extractMaskAndDataFromHexStr` checks `indexOf("0x")` before
  `indexOf("0b")`, anywhere in the value (`DeviceTypeRecords.cpp:537`). In a lower-cased binary pattern, any
  `0` followed by an `x` (don't care) matches `0x`, so the rest of the pattern is parsed as hex.
  `getBytesFromHexStr` reads `x` as the digit 8.
- **When it started.** Commit `0abf3ef` (2025-10-11, "Extended testing for linux") changed `startsWith("0x")`
  and `startsWith("0b")` to `indexOf(...)`. It has been present for a year.
- **Records affected** (every catalogue scanned):

  | Record | Detection value | What `main` checks instead |
  |---|---|---|
  | MCP9808 | `0x07=0b00000100XXXXXXXX` | 4 bytes equal to `88 88 88 00`; can never match |
  | VCNL4040 | `0x0c=0b100001100000XXXX` | Hex parsed from the middle of the pattern; can never match |
  | M5Encoder | `0x20=0b0000000X11111111` | As above |
  | AS5600 | `0x00=0b000000xx` | 1 byte equal to `00`, instead of 0x00–0x03 |
  | LTR-329 | `0x86=0b1010XXXX` | Fails; this pattern came from the 2026-10-13 C3 data fix |

  They are present in RaftCore, Axiom and the RaftI2C test records. LTR-390 (`0b1011XXXX`) is not affected.
- **Effect.** These devices fail detection and fall through to the next candidate or to the ident hook. LTR-329
  now has a stricter pattern, so it is no longer identified at all.
- **Fix** (Stage 1, urgent, small): detect the prefix at the start of the value, after any leading pause. Add
  the item 1 host test, with every shipped detection string and its expected mask and value bytes, so the
  regression cannot return. Confirm on hardware with any of the affected parts. Until the fix lands, the
  branch's workaround (explicit `0xA0,…,0xAF` alternatives for LTR-329) is an option for that record only.

## 11. Observations for future work

Things noticed along the way that are outside this plan, or not worth acting on yet. Each says what was seen and
under what conditions, so it can be checked again later.

| Date | Observation | Conditions | Status |
|---|---|---|---|
| 2026-10-16 | Two `Publish SLOW` warnings (about 50 ms per publish) | Axiom streaming `devjson` at 100 Hz over WiFi during the VL6180 work (`main`, internal-RAM optimisation branch) - a heavier subscription than the dashboard normally uses | Not acted on. Look at it if it appears at normal rates: measure where the time goes (JSON building for every device, the WiFi send) and whether `devbin` at the same rate shows it |
| 2026-10-16 | Polls at 10 Hz ran at about 107 ms and 50 Hz gave about 40 samples/s | `main` (VL6180 work) | Caused by `main` timing each poll from the previous one (`lastPollTimeUs`), so the wake-up latency adds to every interval. Already fixed on `feature/device-identification` by Stage 1 (fixed-rate schedule): measured 100.0 ms and 50.00 Hz on the FeatherS3 after merging `main` |
| 2026-10-16 | LSM6DS on Axiom slot 7, polled at 20 Hz, misses about 10 polls in 100 s with gaps of 0.3-0.5 s | Axiom018 with the VL53L5CX (slot 1) running; the same before and after Stage 9 | Not investigated. Likely the VL53L5CX driver's long transfers (it switches the bus to 400 kHz) holding the bus; `busstatus` pollTiming shows it |
| 2026-10-16 | Attribute lookup tables (`lut`) were ignored by the generated C decoders, so VL6180 and VL53L0X `valid` was inverted in DataLogger CSV, RaftROS and named values | All builds | Fixed on `feature/device-identification` (RaftCore `5982b13`), tested in `test_standard_decode`. Not yet checked with a VL6180 in a DataLogger session |

## 12. Related change: DeviceManager optional in RaftCoreApp (done 2026-10-17)

(Not part of the stages above - not to be confused with Stage 12.)

A minimal `RaftCoreApp` project (no SysMods, no devices) linked in all of the device code, because `RaftCoreApp`
constructs a `DeviceManager`. Its virtual functions, and the REST handlers registered in its setup, keep everything
reachable. This was about 35 KB on main and about 94 KB with this work. A project can now set
`set(RAFT_DEVICE_MANAGER OFF)` in `features.cmake`. RaftCore then defines `RAFT_CORE_NO_DEVICE_MANAGER` (PUBLIC,
because it changes `RaftCoreApp`'s layout), `RaftCoreApp` doesn't create the `DeviceManager`, and
`getDeviceManager()` returns nullptr. The build warns if RaftI2C or RaftMotorControl is in `RAFT_COMPONENTS`.

| Build (ESP32-S3) | Default | `RAFT_DEVICE_MANAGER OFF` |
|---|---|---|
| Minimal `RaftCoreApp` | 1,007,264 | 898,288 (-109 KB; 1.7 KB of device code left) |
| RaftROS FeatherS3 example (RaftSysMods, RaftWebServer, RaftI2C, RaftROS) | 1,436,592 | 1,375,696 (-61 KB) |
| Axiom018 | 1,885,552 (unchanged) | - |

Every `getDeviceManager()` caller in RaftCore, RaftSysMods, RaftROS, Axiom and SandBot already checked for null.
The FeatherS3 example with the option off was booted and checked:
- the web server and the RaftROS node run;
- RaftROS logs that auto-publishing is disabled;
- `devman` is absent from `caps`, and no device topics are published;
- the boot log has no errors.

A separate RaftDevices library was considered and not done: it would save no more flash than this, and it would
change every device project's `features.cmake`.

## 13. Remaining work (as of 2026-10-17)

Follow-on: device type record management - one file per record, `DeviceTypeSources.json`, git sources, type settings,
runtime type files and the raft CLI - is planned and tracked in `device-type-record-management-plan.md`. RaftCore's
records are now `devtypes/records/<Key>.json`, so the Stage 12 record changes below are edits to those files.

Everything else in this plan is done. Still open:

| Item | What | Notes |
|---|---|---|
| Stage 12 | Device type record improvements: detection for the records that have none, the shadowing cases, weak detection, the ST time-of-flight records, the record fixes listed there | Each needs the device to verify on hardware; one record at a time |
| N7 | Run the identification hook only where no address-identified record exists (unless that record is `Disabled`) | Changes behaviour at addresses with known records whose detection fails; exposure is already reduced by the hook address ranges and Axiom's own reserved addresses |
| Stage 1 items 3 and 10 (N13) | Tie-break on the selected multiplexer slot and on bus speed | Left until measurements show a need; the LSM6DS missed polls in §11 may be the first sign |
| D19 second release | The strict record grammar rejects instead of warning | After a release with the warnings has been out |
| Axiom (optional) | Replace `RSAOTypeRegistry` with `getRecordByHwTypeId()` | Not required |
