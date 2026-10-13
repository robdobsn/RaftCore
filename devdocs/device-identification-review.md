# Device Type Handling and Identification — Review and Proposals

Status: review / proposal (no code changed)
Date: 2026-10-13
Scope: RaftCore, RaftI2C, RoboticalAxiom1 (RSAO support and device consumers), with reference to RaftROS

## 1. Purpose

This document records a critique of the existing device type and device identification mechanism across
RaftCore, RaftI2C and RoboticalAxiom1. It also reviews the `feature/device-library` work in those repos and
proposes changes in two groups:

1. **Independent improvements**: defects and design weaknesses that are worth fixing whether or not the
   device library lands.
2. **Enabling capabilities**: primitives the device library (or anything like it) needs from the platform,
   in particular:
   - **Re-identification** of devices already on a bus.
   - **Selectively stopping a specific device type record from being used for identification.** This is
     compared below with the branch's "suppression by name" mechanism, which turns out *not* to be the same
     thing.

### 1.1 What was reviewed

| Repo | `main` | `feature/device-library` tip | Notes |
|---|---|---|---|
| RaftCore | `2d8ae4d` | `283792b` (2026-10-12) | 10 commits ahead, main merged in on 2026-10-12 |
| RaftI2C | `8afcd0c` | `91ce811` (2026-10-12) | 7 commits ahead plus a merge, 0 behind |
| RoboticalAxiom1 | `0aafaec` | `de90c38` (2026-10-13) | 27 ahead, 11 behind |
| RaftROS (Robotical fork) | upstream `61318fc` | `9a508bb` (2026-10-13) | Pinned by the Axiom branch when `FEATURE_ROS2=ON` |

Evidence is given as `repo:path:line`. Line numbers refer to `main` unless a branch is named.

Confidence markers:
- **[V]** verified by reading the code, data or running a script.
- **[L]** likely, inferred from the code but not exercised on hardware.

The code was read statically; nothing was built or run on a device.

---

## 2. Executive summary

**The current identification mechanism works in the common case, but it is fragile in ways a device library
will expose immediately.**

1. **Identification order is load-bearing and invisible.**
   - Candidates are tried extended-records-first, then in generated (JSON) order, and the first match wins.
   - A record with no detection values matches anything.
   - Real examples in the shipped data:
     - VEML6040 shadows MXC400xXC at 0x10.
     - AHT20 shadows TrillRing at 0x38.
     - SHT40 shadows TrillHex.
     - QwiicLEDStick shadows TrillBar.
     - LTR-329, LTR-390 and M5-Weight have unprefixed detection strings, which parse to an empty check.
   - Nothing at build time reports any of this. **[V]**
2. **A bus error during detection counts as "not this type".** Identification then falls through to the next
   candidate, and finally to the RSAO WHOAMI probe, which the code itself describes as potentially destructive
   to third-party sensors. Once a device is identified it is never re-verified. **[V]**
3. **There is no notion of "identity changed".**
   - Clearing an identification (CRC recovery on `main`, `reIdentifyDevices` on the branch) emits no event.
   - Every consumer caches type-derived state: DeviceManager subscribers, RaftROS AutoPub, DataLogger,
     DevicePower, VL53LxCX and raftjs.
   - A device that comes back as a different type is decoded with the old type's decoder. **[V]**
4. **Unidentified devices are retried by one bus-wide 3 s throttle.** Recovering N devices after any bulk
   clear takes about 3·N seconds. The fallback RSAO probe is re-run against every unidentifiable device every
   ~3 s, forever. **[V]**
5. **The parsers for the descriptor grammar are ad hoc.** There are three hand parsers in C++ and a fourth in
   Python, and they disagree with each other and with the wiki.
   - Real defects in shipped data: the bare `p20` in LTR init, and unprefixed detection values.
   - Bugs: a heap overflow in `rN` handling, and the inverted C++ bit-shift in generated decoders (both fixed
     on the branch).
6. **Type identity is a positional index**: base records are indexed by JSON merge order, extended records
   by registration order. There is no stable ID or table generation, so caches (raftjs, subscribers) cannot
   detect that a type has changed.
7. **Several RaftI2C state-machine defects predate the device library and are independent of it.** These
   are: devices behind a lost mux become permanent ghosts; `goingOffline` never matches slotted devices;
   polling uses first-due scheduling and can starve devices; and the poll ring buffer has `uint16_t` offsets
   that can overflow. **[V]**

**On the device-library branches:**

- **The engineering is careful.** The branches fix several real bugs (heap overflow, decoder shift, null-bus
  crash, `cmdraw` race, `listdevs` flakiness), and many claims are backed by on-device measurements.
- **The two platform primitives the branches add are the right *shape* but the wrong *action*.**
  - **Name suppression** conflates "don't identify" with "don't look up by name". It cannot distinguish a
    compiled record from a same-name override. It also has side effects on RSAO address reservation and on
    exposure to the probe.
  - **Whole-bus `reIdentifyDevices()`**:
    - clears every device;
    - re-runs every init sequence;
    - recovers through the 3 s-per-device throttle;
    - tells no consumer.
  - Axiom calls it on every boot (from RSAOSysMod) as well as after a library load.
- **Library-defined or overridden types have no on-device decoder.** Native ROS, DataLogger CSV and
  DevicePower therefore cannot use them. A runtime descriptor decoder was added to Axiom and then removed on
  2026-10-13.

**Recommendation in one line:**
1. Land the branch's independent fixes on `main`.
2. Fix the grammar, ordering and RaftI2C state-machine defects.
3. Then add three platform primitives:
   - a **per-record identification policy** (by index, with a "claim" mode);
   - **scoped, make-before-break re-identification** with a proper per-record identification scheduler;
   - an **identity-change event** carried through `BusAddrStatus`.

   All three are keyed by a **type-table generation**.

Section 10 gives a phased plan.

---

## 3. How it works today (main)

### 3.1 Build time (RaftCore `scripts/ProcessDevTypeJsonToC.py`, `scripts/DecodeGenerator.py`)

- **Merging.** The JSON files listed in `DEV_TYPE_JSON_FILES` are merged with `dict.update` (py:44-50).
  - A record's base index is its position in the merged dict.
  - A duplicate key in a later file silently replaces the earlier record and keeps the earlier position.
- **Addresses.** `addresses` is expanded into the per-address index arrays `baseDevTypeIndexByAddr[128]`,
  covering 0x00-0x7F, with lookups capped at 0x77.
- **`excludeFromAddrMap`** takes a record out of the address map and the scan-priority lists (used by RSAO
  records).
- **Scan priorities.** Three scan-priority lists are emitted; the last covers every address from 0x04 to
  0x77.
- **Validation.**
  - `resp.b` is checked against the size computed from `pollInfo.c`.
  - `detectionValues` and `initValues` are not validated at all.
- **`deviceTypeId` is not consumed by the generator.** Axiom maintains a hand-written copy in
  `RSAOTypeRegistry.cpp`.
- **Decoders.** `DecodeGenerator` emits one capture-less decode lambda per record, plus `AttrFieldDesc`
  tables.
- **Storage.** The tables are emitted **non-const** (`static`), so they occupy DRAM rather than flash. **[L]**

### 3.2 Runtime type table (RaftCore `components/core/DeviceTypes/`)

- **`DeviceTypeRecord`** is a POD of `const char*` fields plus the decoder pointer and field descriptors.
- **`DeviceTypeRecordDynamic`** owns `String`s and hands out `c_str()` pointers into itself. It never sets
  the decoder or the field descriptors.
- **`DeviceTypeRecords`** (one global instance):
  - Extended records live in `_extendedDevTypeRecords`, which is `reserve(20)`d.
  - They are **append-only**, protected by a mutex, and gated by a set-once atomic flag.
  - Extended index = `BASE_DEV_TYPE_ARRAY_SIZE + position`.
  - Pointer stability depends on three invariants: the vector never reallocates (cap 20), it is
    append-only, and records are never mutated. These are stated only in comments.
- **Lookups:**
  - `getDeviceTypeIdxsForAddr(addr)` returns extended matches first (re-parsing each extended address CSV
    on every call), then the base indices.
  - `getDeviceInfo(name)` also searches extended first.
  - `getDeviceInfo(idx)` and `getPollDecodeFn` copy the record.

### 3.3 Discovery, identification, polling (RaftI2C `components/RaftI2C/BusI2C/`)

- **Worker task** (`BusI2C::i2cWorkerTask`, BusI2C.cpp:347-544): one pass per ~5 ms, holding the bus-owner
  lock. Each pass runs, in order:
  1. the bus-task service handler (RSAO arbitration);
  2. the scanner;
  3. one queued request;
  4. mux, power and IO-expander servicing;
  5. **one** device poll.
- **Scanner** (BusScanner.cpp):
  - Modes: main-bus/mux scan, then FAST, then SLOW.
  - Priority lists are visited every 1, 3 and 9 rounds.
  - Polled online devices are **not** scanned. Their offline detection comes only from poll failures.
  - Identification runs:
    - on an offline→online edge (627-644);
    - via a retry for "online, not polled, type invalid" elements, throttled by a **single bus-wide**
      `_lastUnidentRetryMs` of 3000 ms (614-626).
- **Identification** (DeviceIdentMgr.cpp:88-266):
  1. Get the candidates for the *raw* address (the slot is ignored).
  2. For each candidate, run `checkDeviceTypeMatch`. **The first match wins.**
  3. On a match:
     - run init (its result is ignored);
     - build the poll info;
     - create a new `PollDataAggregator`.
  4. **Only if nothing matched**, call the registered `RaftNewDeviceIdentFn` hook (the RSAO WHOAMI probe),
     which returns Handled, Deferred or NotMine.
- **Status** (BusStatusMgr): `_addrStatus` is a vector of `BusAddrRecord` (maximum 50), with linear lookups.
  - The online state machine is INITIAL → ONLINE (2 responses) → OFFLINE (3 failures) → PENDING_DELETION.
  - `loop()` on the main task harvests changes into `BusAddrStatus` and calls the status callback.
- **Existing re-identification on `main`.** After three CRC drops, `DevicePollingMgr` calls
  `clearDeviceIdentification(addr)`, which resets `DeviceStatus` **without raising a change**. The retry
  above then re-identifies the device.

### 3.4 Axiom RSAO (RoboticalAxiom1 `components/RoboticalRSAO/`)

- **`RSAOSysMod::setup()`**:
  - Builds the type registry (11 hard-coded id→name pairs), the ident handler, the arbitration stack, and
    the NVS address store.
  - Reserves every address that `getDeviceTypeIdxsForAddr` claims within the assignable range.
  - Applies `addrExclude`.
  - Registers the ident hook and the bus-task service **last**.
- **`identHook`:**
  - Anything at the default address 0x0F is queued for commissioning, with no probe.
  - Otherwise it probes `ROBOTICAL_ID`, then WHOAMI, then looks the id up in the registry, then resolves
    the name with `getDeviceInfo(name)`, then runs a serial check.
- **The id→name mapping exists in four places:**
  1. `RSAOTypeRegistry.cpp`
  2. `RSAOTypeRecords.json` `deviceTypeId`
  3. the WHOAMI suffix embedded in each RSAO `detectionValues` (dead data, since RSAO records have no
     addresses)
  4. `AxiomDevTypes/split_rsao_records.py`, which is stale (8 of 11 entries)

### 3.5 Consumers of type identity

| Consumer | Depends on |
|---|---|
| RaftCore DeviceManager | `getDeviceTypeIndex(addr)` live; per-callback `deviceTypeIdx` for type-keyed subscribers; `registerForDeviceData(name)` resolves name→index once |
| raftjs | Caches typeinfo per device key, fetched only when `deviceTypeInfo === undefined`; never invalidated on an `_i` change |
| RaftROS AutoPub | Caches `decodeFn` at attach; skips an already-attached device; type-name overrides in `AutoPubClassMap.h` |
| Axiom DataLogger | Snapshots `deviceTypeIndex` and `decodeFn` at session start; skips types without a compiled decoder in CSV |
| Axiom DevicePower | Accepts *any* identified type at the fuel-gauge address; decodes by field names; raw MAX17048 register writes |
| Axiom VL53LxCXSysMod | `strcmp(deviceType, "VL53L5CX")` at 0x29 |

---

## 4. Critique of the existing mechanism (main)

Severity is from the point of view of a shipping product. Items marked "fixed on branch" are included because
they are not yet on `main`.

### 4.1 RaftCore: type table, grammar, generator

| # | Sev | Defect | Evidence | Scenario |
|---|---|---|---|---|
| C1 | High | **Heap overflow in `extractMaskAndDataFromHexStr` (`rN`)**: the loop runs over string positions, but the vector is sized from the parsed number | DeviceTypeRecords.cpp:511-525 | `"r1p50"` (the wiki's own example) writes 4 bytes into a 1-byte vector. *Fixed on branch* (`06827e6`) |
| C2 | High | **Generated C++ decoders shift in the wrong direction** compared with raftjs (`<<=` for `s>0`) | DecodeGenerator.py:514-519 vs raftjs `RaftAttributeHandler.ts:461-467` | AHT20 humidity ×2²⁴, LPS25 pressure ×2¹⁶, AMG8833 wrong scale, in every on-device decode (DeviceManager named values, RaftROS, MicroPython). *Fixed on branch* (`283792b`) and worth cherry-picking alone |
| C3 | Med | **Unprefixed detection values parse to an empty check, which then "matches"** | DeviceTypeRecords.cpp:~583 returns true with empty vectors; DeviceIdentMgr.cpp:279-352 | LTR-329 `0x86=A005`, LTR-390 `0x06=B2` and M5-Weight `0xFF=26262626` become catch-alls at their addresses **[V]**. See §12.1 |
| C4 | Med | **A bare `pNN` in `initValues` is written as data, not paused**: `=` splits on the first occurrence, so `p20` becomes write bytes `{0x02,0x00}` | RaftJson.h:880-886; DeviceTypeRecords.cpp:474-489 | LTR-329 and LTR-390 write a spurious register and skip the 20 ms settle. *Data fixed (`=p20`) on RaftCore and Axiom `main`, 2026-10-13*. The parser still accepts a bare `pNN` as write data; see §9.1 RaftCore item 2 |
| C5 | Med | **`getPollInfo` defaults `s` (samples to store) to 0**, while the `ext` path defaults to 1 | DeviceTypeRecords.cpp:342 vs :219 | Any runtime record that omits `s` corrupts the heap in `PollDataAggregator::put`. RaftI2C guard on branch; the RaftCore default is still 0 |
| C6 | Med | **Generator crash** (`UnboundLocalError`) when the first processed record has no `addresses` | ProcessDevTypeJsonToC.py:97 **[V by running]** | Listing `RSAOTypeRecords.json` first in `DEV_TYPE_JSON_FILES` breaks the build |
| C7 | Med | **A failed `addExtendedDeviceTypeRecord` silently yields index 0** for static devices | DeviceManager.cpp:378-381 | The 21st static device with a record is reported as base record 0 |
| C8 | Med | **Static instances with the same type name share the first instance's record** | DeviceTypeRecords.cpp:796-812; DemoDevice per-instance `sampleBytes` | The second instance's data size does not match its `devInfoJson` |
| C9 | Low-Med | **Hex values are not validated**; odd-length hex is padded with a trailing zero byte | DeviceTypeRecords.cpp:484-487, 532-536 | `0x123` → `{0x12,0x00}`; trailing junk becomes extra check bytes |
| C10 | Low | **`rN` in detection has undefined semantics** (it constrains byte 0 only); in poll strings it is a no-op | DeviceTypeRecords.cpp:519-523 | The wiki says "any value accepted"; no production record uses it |
| C11 | Low | **The generator's `0x` poll length includes the prefix** (off by one compared with C++) | DecodeGenerator.py:99-100 | Latent |
| C12 | Low | **Addresses ≥ 0x78 are silently dropped** | py:83-93, 228-229 | Latent |
| C13 | Low | **A bad `r{…}` expression is skipped with `continue`**, so later `$N` references point at the wrong result | DeviceTypeRecords.cpp:258-275 | Wrong FIFO read lengths with no error |
| C14 | Low | **`getJson(true)` emits invalid JSON** for empty `pollInfo` | DeviceTypeRecord.h:224-242 | *Fixed on branch* via `appendRawField` (see §7.1) |
| C15 | Low | **`pollDataSizeBytes` has two meanings**: with the timestamp for MotorControl, without it for base records, and "1" for DemoDevice | MotorControl.cpp:580; DemoDevice.cpp | Inconsistent `typeinfo` |

### 4.2 RaftI2C: scanning, identification, status, polling

| # | Sev | Defect | Evidence | Scenario |
|---|---|---|---|---|
| I1 | High | **Devices behind a lost mux, or a slot with unstable power, become permanent online ghosts** | DevicePollingMgr.cpp:73-75 returns on a slot-enable failure without `updateBusElemState(false)`; scanner skips polled devices | Unplug a mux carrying a polled sensor: it stays ONLINE and identified indefinitely |
| I2 | High | **`goingOffline` never matches slotted devices**: the tracker returns *raw* addresses, which are compared with slot-encoded records | BusMultiplexers.cpp:660-663; BusI2CElemTracker.h:70-79; BusStatusMgr.cpp:546-567 **[V]** | A power-cycled slot does not report its devices offline, so they are not re-initialised and their data is garbage |
| I3 | High | **A device swapped or power-glitched within 3 polls is never re-identified or re-initialised** | Polled devices are never scanned; success never re-checks identity | A same-type replacement never receives its init sequence; a different type is decoded as the old one |
| I4 | High | **A bus error during detection equals a mismatch**: it falls through to weaker candidates, then to the RSAO probe, and becomes permanent | DeviceIdentMgr.cpp:317-318, 115-174, 192-265 | One NACK while reading a WHOAMI register lets a weak or empty-detection record claim the device |
| I5 | High | **Polling can starve devices**: `getPendingIdentPoll` returns the first due record from index 0, one poll per pass, and `i` defaults to 0 (always due) | BusStatusMgr.cpp:598-622 **[V]**; DeviceTypeRecords.cpp:345 | One record without `i` (easy in a library), or a few fast devices, means later records are never polled |
| I6 | High | **The `PollDataAggregator` ring uses `uint16_t` offsets and sizes** | PollDataAggregator.h:336-339 **[V]** | `devman/devconfig?numSamples=N` with N × resultSize > 65535 wraps the head offset, causing an out-of-bounds write. The branch's ceiling was later dropped; the device library bounds only its own records |
| I7 | Med | **OFFLINE records are polled forever** (`goingOffline` and `informBusStuck` never move them to PENDING_DELETION; polling has no state filter) | BusAddrRecord.cpp:56; BusStatusMgr.cpp:546-594 | Wasted bus time, NACKs or timeouts |
| I8 | Med | **The main-bus tracker bits are sticky** and hide slot devices | BusI2CElemTracker.h:60-67; BusScanner.cpp:180 | A device moved from the main bus to a mux slot is never found; stale INITIAL records can accumulate **[L]** |
| I9 | Med | **There is a single bus-wide 3 s retry for unidentified devices** | BusScanner.h:173; .cpp:614-626 **[V]** | N unidentified devices take about 3·N s; this is the recovery path every re-identify scheme depends on |
| I10 | Med | **The destructive RSAO probe is re-run every ~3 s** against permanently unidentifiable devices | I9 path → hook | Sensirion-class parts are repeatedly probed (the code itself warns about this, DeviceIdentMgr.cpp:183-189) |
| I11 | Med | **Init failures are ignored**; there is no re-initialise primitive | DeviceIdentMgr.cpp:384-409 | A browned-out sensor that still ACKs is never re-initialised |
| I12 | Med | **Some requests never get a callback** (paused drop, barred, BUSY, slot failure, length mismatch, queue full) | BusAccessor.cpp:163-178, 274-288, 316-336; BusI2C.cpp:687-701 | `cmdraw` and `sendCmdToDevice` only ever see a timeout, never the error |
| I13 | Med | **Deletion notices and poll data are single-consumer** | DeviceIdentMgr.cpp:500-512, 571-579 | With two publishers only one sees a deletion; this caused the `listdevs` flakiness fixed on the branch |
| I14 | Med | **The DevicePollingMgr CRC recovery table has 8 entries and they are never freed**; `trailerCap` sticks to the address | DevicePollingMgr.cpp:435-455 | RSAO commissioning across many addresses exhausts it; a new device at an old address inherits stale CRC capability |
| I15 | Low | **Production debug logging is on**: `DEBUG_DEVICE_IDENT_MGR` and `DEBUG_ELEM_STATUS_DEVICE_IDENT` | DeviceIdentMgr.cpp:13; BusScanner.cpp:7 | String formatting and logging on the bus task while holding the lock; log spam every 3 s |
| I16 | Low | **`ADDR_STATUS_MAX` (50) overflow is silent** | BusStatusMgr.cpp:261 | The 51st element is never tracked |
| I17 | Low | **Detection pause is applied *after* the read**, inside the bus lock; sync-path barring is ignored | DeviceIdentMgr.cpp:372-373 | Unclear whether this is intended; it extends the scan budget **[L]** |

### 4.3 Axiom: RSAO and consumers

| # | Sev | Defect | Evidence | Scenario |
|---|---|---|---|---|
| A1 | High | **A change of identity is invisible to consumers** (no event on clear; RaftROS on `main` skips an already-attached device and ignores the per-callback type index; DataLogger caches `decodeFn` for the whole session) | BusStatusMgr.cpp:1311-1325; RaftROS AutoPubDeviceSource.hpp:262-267 | A CRC-triggered re-identify that resolves to a different type is decoded with the old decoder |
| A2 | Med | **The commissioning debounce can be starved**: each retry refreshes `requestedMs` | RSAOSysMod.cpp:259, 299 vs RaftI2C 3 s retry | With `arbDebounceMs` ≥ ~3000 a device at 0x0F is never commissioned; correctness depends on a constant in another repo |
| A3 | Med | **Anything at 0x0F is commissioned** without checking that it is an RSAO | RSAOSysMod.cpp:544-551 | A third-party part at 0x0F triggers arbitration every cooldown, holding off polling for up to 120 s |
| A4 | Med | **DevicePower does not check the fuel gauge's type** | DevicePower.cpp:1157-1196 | 0x36 is shared by MAX17048, AS5600 (`0x00=0b000000xx`, very weak) and AdafruitSoilSensor, and the precedence is JSON order **[V]**. If it is ever mis-identified, telemetry silently stops and the MAX17048 register writes go to the wrong device |
| A5 | Low | **`_knownSerials` is write-only and never pruned** | RSAOSysMod.cpp:317 | Memory creep; overstated `knownRSAOs` |
| A6 | Low | **Unknown RSAO ids are re-probed and logged every ~3 s** | RSAOIdentHandler.cpp:58-65 | Log noise and bus traffic |
| A7 | Low | **An RSAO moved onto a static-record address could be claimed by a weak record** (e.g. AS5600 matches any WHOAMI high byte of 0x00/0x01) | Order in DeviceIdentMgr.cpp:104-176 | **[L]** |
| A8 | Low | **Stale comments and logs** claim Deferred is "retried only on offline→online", and the hook runs "before default identification" | RSAOSysMod.cpp:598; DeviceIdentMgr.cpp:247-252; RaftBusDevicesIF.h:37,226 | Misleads diagnosis |
| A9 | Low | **Raft libraries float on `main`** (`RaftCore@main`, `RaftI2C@main`) | features.cmake on Axiom `main` | Identification semantics can change without an Axiom commit (the branch fixes this by pinning) |

---

## 5. Cross-cutting design weaknesses

1. **Identification is an edge-triggered side effect of scanning with no per-record identification state.**
   "Never tried", "deferred", "no match", "bus error", "init failed" and "suppressed" all collapse into
   `deviceTypeIndex == INVALID`. There are no attempt counts, no backoff and no reason, so nothing can be
   diagnosed through the API.
2. **First match wins, in an implicit order.**
   - The order is extended records, then JSON order, then the hook.
   - Overlap between records is resolved silently by position.
   - An empty detection string shadows everything after it.
   - There is no priority field, no specificity ordering and no build-time overlap report.
   - Combined with C3 and I4, a large share of mis-identification risk sits in ordering.
3. **Identity is assumed fixed until the device goes offline.** Successful polls are treated as proof of
   identity; they only prove an ACK.
4. **Clearing is break-before-make and silent.** `clearDeviceIdentification` drops polling, aggregator data
   and per-device overrides immediately. It then waits for the throttled retry, and tells nobody.
5. **The notification model is lossy.**
   - `BusAddrStatus` has `isChange` and `isNewlyIdentified` only.
   - It has no previous type index and no "identity revoked" signal.
   - Listeners have to infer a type change from a second `isNewlyIdentified` without an intervening OFFLINE.
6. **Per-device runtime configuration is lost on re-identification and replug.** The `devconfig` settings
   for interval, numSamples and busHz live in `DeviceStatus` and are replaced wholesale.
7. **Type identity is positional** (base: JSON merge order; extended: registration order). There is no
   stable ID or table generation. Clients and subscribers cannot detect that an index now means something
   else.
8. **Names are not unique keys.**
   - Base and extended records can share a name.
   - Address lookup returns both, while `getDeviceInfo(name)`, `typeinfo?type=` and
     `registerForDeviceData(name)` resolve to the extended one.
   - So a device that falls through to the base record is invisible to a subscriber registered by name.
9. **An override does not replace.** The base record stays a fall-through candidate behind its override. An
   extended record cannot be removed or replaced: that is append-only, and append-only is also what keeps
   the `c_str()` pointers safe.
10. **One query has two meanings.** `getDeviceTypeIdxsForAddr` serves both as "identification candidates"
    and (in RSAOSysMod) as "address is physically claimed by a known device". Any change to candidacy
    therefore changes address reservation.
11. **The fallback probe runs when no record *matches*, not when no record *exists*.** A disabled or failing
    record at 0x10-0x6F therefore exposes the device to the RSAO probe. The STCC4 notes describe the
    narrower behaviour; the code does the broader one.
12. **Record metadata does not survive to runtime.** `scanPriority`, `excludeFromAddrMap` and `deviceTypeId`
    exist only at build time, so a runtime record can never behave exactly like a compiled one. In
    particular, a runtime record is always address-mapped and is forced into scan priority 0.
13. **Records defined only as data are second-class.** They have no decoder and no `AttrFieldDesc`, so
    `getBusDeviceNamedValue`, `getDecodedPollResponses`, RaftROS, DataLogger and DevicePower all get nothing.
    An override of a compiled type cannot inherit the compiled decoder, even when the wire format is
    identical.
14. **System devices are found by address, not by role.** SysTypes already declares
    `FuelGauge {addr, role:"system"}`, but nothing uses this to protect, prioritise or validate the type at
    that address.
15. **The bus-owner lock is held across identification delays.** Detection and init pauses run inside the
    pass lock, so one identification can exceed the scan budget by an unbounded amount.
16. **The scan-priority lists are snapshotted once in `BusScanner::setup`.** Extended records added later
    get no priority boost.

### 5.1 Performance and memory notes

- **Identification latency for a newly plugged device** is two positive scans of its address/slot plus the
  detection transactions. A low-priority address on a multi-slot bus can take several seconds **[L]**.
- **Poll throughput** is at most one device poll per worker pass, roughly ≤150 polls/s across all devices
  **[L]**. Each poll copies the full `DevicePollingInfo`, including a vector of `BusRequestInfo` with
  `String`s and `std::function`s, which causes heap churn.
- **Lock and lookup cost per scanned address:** 4-5 lock/linear-search operations on the status mutex.
- **`getDeviceTypeIdxsForAddr`** allocates and re-parses the extended address CSVs on every identification.
- **Base tables** sit in DRAM, not flash, because they are non-const. The extended reserve is about 2.2 KB of
  internal RAM, always allocated.
- **Worker stack and reads:** the worker stack is 5000 B, with a VLA read buffer in `i2cSendAsync`, so large
  reads through the request path risk overflowing it **[L]**.

---

## 6. Documentation drift

The wiki and the code-level docs disagree with the code in several places. The descriptor spec on the Axiom
branch (`devdocs/device-descriptor-spec.md`) was written from the parser for exactly this reason. It ships
with a check script, and it is the most reliable description of the grammar today.

| Doc | Claim | Code |
|---|---|---|
| wiki `I2CDeviceIdentificationAndPolling.md` | Max 64 extended records | 20 |
| same | Dynamic poll example `"c=01=r6&i=100&s=10"` | Poll info must be a JSON object; the example device is never polled |
| same | Detection `rN` means "any value accepted" | Byte 0 must be 0 (and the form overflows on `main`) |
| same | `0x10=0x60` writes 0x60 to register 0x10 | Only `0x10` is written; the right-hand side is a read spec |
| same | Pause applies before reading | Applied after the transaction |
| same | "3-5 consecutive successful detections" threshold | No such logic |
| same | Init failure marks the device "not fully initialised" | Init results are ignored |
| same; `RaftBusDevicesIF.h:37,226` | Ident hook runs **before** default identification | Runs **after**, as a fallback |
| same | Base records "stored in flash (const)" | Non-const |
| wiki `DeviceTypeRecordFormat.md` | `scanPriority` takes "high" only; `0b` in init is write data | int/high/medium/low; `0b` in init is a read length. `excludeFromAddrMap` and `deviceTypeId` are undocumented |
| `DeviceTypeRecordDynamic.h:271` | Address ranges are not accepted | `parseIntList` accepts `a-b` |
| `system-device-tagging-plan.md:35` | Per-static-instance type records | Deduplicated by name (C8) |
| `stcc4-identification-notes.md:30-35` | The hook runs only where no standard record exists | It runs where no record *matches* |
| RaftI2C `DeviceIdentMgr.cpp:236-252` | Identification only on offline→online | The 3 s retry also calls it |

---

## 7. The device-library work

### 7.1 What the three branches do

**RaftCore `feature/device-library`** (10 commits, NikTheGeek1, 2026-09-16 → 10-12):

- **New platform hooks:**
  - **Suppression by name**: `addSuppressedDeviceType`, `clearSuppressedDeviceTypes`,
    `isDeviceTypeSuppressed`. Suppressed names are kept in a separate list (max 128), honoured in
    `getDeviceTypeIdxsForAddr` and `getDeviceInfo(name)`, but not in lookups by index.
  - **`RaftBusDevicesIF::reIdentifyDevices()`**, a no-op by default.
  - **`getMaxExtendedDeviceTypeRecords()` / `getNumExtendedDeviceTypeRecords()`**. The extended-record
    capacity stays at 20.
  - **`devman/typeinfo?…&full=1`**, which returns the whole record under `devrec`.
- **Fixes that are independent of the library** (all worth landing on `main`):
  - the `rN` heap overflow (C1);
  - `appendRawField` empty-value handling (C14);
  - null-bus guards for directly-connected devices in `devconfig` and `cmdraw`;
  - `listdevs` filtering on identified and online instead of "has unread samples";
  - the DecodeGenerator shift direction (C2).
- **`cmdraw` reads now work:**
  - `BusRequestInfo::setCallbackFromBusTask()` lets the result be delivered on the bus worker task, so a
    handler blocking the main loop can wait for it.
  - A follow-up cross-task race, which crashed a board in the field, is fixed with `_cmdRawMutex`.
  - The timeout defaults to 20 ms, with a maximum of 500 ms.
- **Tests:** `DeviceManager_regression_test.cpp` and `scripts/tests/test_standard_decode.py`.

**RaftI2C `feature/device-library`** (7 commits plus a merge):

- **`reIdentifyDevices()`.** It sets an atomic flag. The worker services the flag at the start of a pass by
  calling `BusStatusMgr::clearAllDeviceIdentifications()`, which clears `deviceStatus` on every record while
  leaving them online.
- **`BusAccessor`** honours `isCallbackFromBusTask()`.
- **`PollDataAggregator::put`** gets a guard against zero slots (the symptom of C5). The ring-size ceilings
  were added and later removed again, which leaves I6 open.
- **Unit tests** are re-enabled.

**RoboticalAxiom1 `feature/device-library`** (27 commits):

- **The `DeviceLibrary` component and SysMod:**
  - **Loading:** in `postSetup()` it loads `/local/devlib.json` (max 64 KB) and applies each record as an
    extended record, or as a suppression when it has `"disable": true`.
  - **Validation:**
    - per-record rejection that keeps the first error;
    - read-length, retained-sample and ring-size bounds, checked with RaftCore's own parser;
    - the record key must agree with `deviceType`;
    - `resp.b` must agree with the parsed poll size;
    - `deviceTypeId` must be a strict 16-bit value.
  - **Protections:** it refuses to suppress or replace MAX17048, or to claim 0x36.
  - **Re-identification:** it asks every bus to re-identify if anything was applied.
  - **Reporting:** a `devlib` endpoint gives the counts, the first error and `appliedCrc16`, which can be
    compared with `filehash`.
- **RSAO lifecycle:**
  - The registry is created in `setup()`, and a `DeviceTypeIdSink` callback is registered with
    DeviceLibrary.
  - Everything else moves to `postSetup()`: seal the registry, reserve addresses from the final table,
    publish the hook, then **call `reIdentifyDevices()` unconditionally**.
  - The registry returns owned `std::string`s. The REST status is read through an atomic.
  - Arbitration moves onto the bus task.
- **Data and spec:**
  - The LTR `p20` init data is fixed (C4).
  - A descriptor spec is added, plus a CI check that runs it against all shipped profiles.
  - A Python hardware test gate is added.
- **Build:**
  - Raft libraries are pinned to exact commits. They now point at the RaftCore and RaftI2C branch tips
    (`283792b`, `91ce811`).
  - **The device type list selects `DeviceTypeRecords.testsplit.json`**, an 18-profile subset, so that
    catalogue installs exercise real additions. CI enforces that only `-devlib` versions may build with it.
- **ROS** (2026-10-12/13):
  - `181a6db` added `components/AxiomROS`, with a descriptor-driven runtime decoder and a "keep the
    compiled decoder for response-compatible overrides" sink.
  - `08c5e2a` **removed all of it** the next day.
  - `FEATURE_ROS2` is now a CMake `option`, **OFF by default**. CI publishes a "host" variant (ROS on the
    host via USB/WebSocket) and an opt-in "zenoh" variant.
  - The zenoh variant pins **`robotical/RaftROS@9a508bb`**. Its net change against upstream is small:
    - AutoPub detaches and re-attaches a device when re-identification changes its type index without an
      offline transition;
    - data callbacks whose `deviceTypeIdx` no longer matches are dropped;
    - `MAILBOX_RECORD_SIZE` goes from 256 to 260, so a full AMG8833 frame fits.

  These AutoPub changes are good, independent fixes that belong upstream in RaftROS.

### 7.2 Assessment of the platform primitives

#### Name suppression vs "stop this record being used for identification"

These are **not the same**, and the difference matters:

| Property | Branch name suppression | What "disable a record for identification" needs |
|---|---|---|
| Granularity | A name. It hides the base record **and** any same-name override | A record index. "Disable compiled VCNL4040, keep my VCNL4040 override" has to be expressible |
| Scope | Identification **and** lookup by name (`typeinfo?type=`, `registerForDeviceData(name)`, `getDevTypeInfoJsonByTypeName`) | Identification only. Lookups, typeinfo and decode should keep working, so a UI can still show what it is offering to re-enable |
| Address claim | Removes the address from RSAO reservation, so arbitration may place an RSAO on a real sensor's address | A choice: *disable* (fall through to the next candidate or the hook) or *disable-and-claim* (not identified, but the address is still physically claimed, no hook probe, still reserved) |
| Fallback | The device falls through to the destructive RSAO probe, retried every 3 s | The claim mode prevents this |
| Reversibility | `clearSuppressedDeviceTypes` clears everything, with no owner and no per-name removal | Per-record enable and disable, optionally owner-tagged |
| Change signal | None; the caller must remember to re-identify | Bumps a table generation, which the bus observes |
| Visibility | Counts only, in `devlib` | `typeinfo` reports `src` (base or ext) and the identification state |

The branch's suppression is a reasonable *user-facing* concept ("don't detect VCNL4040"). It should be built
**on top of** a per-record identification policy, not used as the platform primitive.

#### `reIdentifyDevices()`

**What is right:**
- The request is consumed at a worker-task boundary, after the bus lock is taken. No in-flight
  identification can publish after the clear.
- The atomic exchange makes the request idempotent.
- The clear is a single locked pass.

**What is wrong is the action attached to it:**
- **All or nothing.** Adding one record resets every device on every bus.
  - Every init sequence re-runs, including FIFO configuration, start-measurement commands and calibration.
  - Every RSAO is re-probed.
  - Any device that now matches nothing gets the destructive probe every 3 s.
- **Break-before-make.** Polling stops immediately, and recovery runs through the 3 s-per-device retry (I9).
  For about 12 devices that is about 36 s of missing data.
- **Silent.** No status event is raised, so consumers keep the old type. Only the RaftROS fork copes with
  this.
- **Lossy.**
  - The per-device `devconfig` overrides revert to defaults.
  - A pending `isNewlyIdentified` is discarded.
  - The aggregator sequence restarts.
  - DevicePollingMgr's CRC recovery state is not reset.
- **Called on every boot in Axiom.**
  - `RSAOSysMod::postSetup` calls it unconditionally, in addition to DeviceLibrary calling it.
  - The RSAO call is unnecessary: devices missed before the hook existed are already unidentified and get
    retried anyway.
  - This puts a boot-time data gap on every unit, including the fuel gauge, whose `FUEL_GAUGE_STALE_DATA_MS`
    is 10 s **[L]**.
- **Inaccurate return value.** It returns `getAddrStatusCount()`, which over-reports, because it includes
  offline and unidentified records.

#### Other branch items

- **`setCallbackFromBusTask`** is correct and necessary. Three gaps remain:
  - the lost-callback paths (I12) are unchanged;
  - the contract for callbacks that run under the bus-owner lock should be documented;
  - `DeviceIdentMgr::sendCmdToDevice` still special-cases the main task instead of using the flag.
- **The `extractMaskAndDataFromHexStr` fix** bounds the writes correctly. Its justification for preserving
  the fill pattern ("all forty compiled profiles were detected and polled against this behaviour") does not
  hold up:
  - for poll strings the loop is a no-op;
  - no production detection string uses `rN`.

  The semantics should be *defined*, not preserved.
- **`appendRawField`** treats the symptom. `DeviceTypeRecord::getJson` should decide which fields are
  present.
- **`typeinfo full=1`** is useful, but it does not round-trip: `scanPriority`, `excludeFromAddrMap` and
  `deviceTypeId` are missing, as are the source (base or ext) and the identification state.

### 7.3 Assessment of the Axiom loader and lifecycle

**Good:**
- Validation sits at the point where untrusted data enters.
- The bounds are computed with the runtime parser.
- Per-record rejection with the first error kept.
- The CRC of the applied file.
- The dependency direction: RoboticalRSAO knows about DeviceLibrary, not the reverse.

**Gaps:**
- **Values that are not validated:**
  - The poll interval `i` is unbounded. With I5, a record with `i` of 0 or a very small value starves every
    other device.
  - The syntax of `addresses` and `detectionValues` is not checked.
- **Records that misbehave:**
  - An **empty `detectionValues` claims its addresses unconditionally, ahead of compiled records.**
  - **`deviceTypeId` collisions are not checked.** A library record reusing an existing servo id silently
    re-maps every servo to a type with no decoder.
- **Feedback to the user:**
  - Suppressing an unknown name still counts as "suppressed", so a typo reports success.
  - The `DeviceTypeIdSink` returns nothing, so the loader reports a record as added even when the registry
    refused it (sealed, or RSAO disabled).
- **Library RSAO records differ from compiled ones.** A library RSAO record with `addresses` is
  address-mapped, identified by address first, and reserved, because `excludeFromAddrMap` has no runtime
  equivalent.
- **Load ordering is implicit.** "DeviceLibrary `postSetup` before RSAO `postSetup`" holds only because of
  the `registerSysMod` order in `main.cpp`.
  - That file conflicts when merged with `main`.
  - If the order is swapped, the registry is sealed first. Every library `deviceTypeId` is then silently
    dropped while `devlib` reports `applied: true`.
- **Sealing means boot-once only.** A library change needs a reboot by design. That is consistent with
  append-only extended records, but it should be stated as a product constraint.
- **The Axiom protections are hard-coded** (the MAX17048 name and address 0x36):
  - **Too broad:** any record claiming 0x36 on any slot is rejected, so AS5600 and AdafruitSoilSensor can
    never be overridden.
  - **Too narrow:** VL53L5CX, which VL53LxCXSysMod needs by name, is not protected. Neither is the IO
    expander.
  - They should be derived from the SysTypes `role: "system"` entries.
- **`addrExclude: "0x36"` in SysTypes is redundant.** 0x36 is already reserved by the MAX17048 record, which
  cannot be suppressed. It is harmless.
- **Library and overridden types get no on-device decoding** (§5 item 13). Native ROS announces an endpoint
  for them that never publishes. DataLogger CSV skips them. Only the host-side (raftjs / host ROS) path works.
  This should be a documented limitation or a planned capability (§9.2-G).
- **The host test harness** (`tests/ros/CMakeLists.txt`) builds against sibling checkouts, not the pinned
  commits.

---

## 8. Answer to the specific questions

**Re-identification.** The platform needs re-identification, but not as "clear everything".

The right primitive:
- is a scoped request (all, address, type, or *stale against the current table generation*);
- is serviced at the worker boundary, as the branch already does;
- is executed by a per-record identification scheduler, not the 3 s global retry;
- is **make-before-break**: keep polling the current type until a different result is known;
- skips init when the same type re-matches;
- emits an explicit identity-change event.

**Stopping a specific record being used for identification.** This needs a **per-record (per-index)
identification policy**, separate from lookup, with a "claim" variant. The branch's suppression by name
should become a convenience layered over it (`setIdentPolicyByName(name, Disabled)`). A table-generation bump
on every change lets the bus re-identify exactly the affected addresses without the caller having to ask.

---

## 9. Proposed changes

### 9.1 Independent improvements (no device library required)

**RaftCore**

1. **Cherry-pick the branch fixes to `main`:**
   - the DecodeGenerator shift direction (C2);
   - the `rN` overflow bound (C1);
   - `appendRawField`;
   - the null-bus guards;
   - the `cmdraw` mutex and `setCallbackFromBusTask`, together with the RaftI2C counterpart;
   - the `listdevs` filter.
2. **One grammar, one parser, validated.** Replace `extractBufferDataFromHexStr`,
   `extractMaskAndDataFromHexStr` and `extractReadDataSize` with a tokenizer that produces typed operations:
   `Write{bytes}`, `Read{n|expr}`, `Check{alts:[{mask,value}]}` and `Pause{ms}`.
   - **Parsing rules:**
     - validate characters strictly;
     - reject odd-length hex;
     - a bare `pNN` is a pause;
     - an unprefixed value is an **error**, not an empty check;
     - `rN` in detection means "read N bytes, compare nothing";
     - reject more than one `=`.
   - **Shared behaviour:** return errors with their position. Share the test vectors (as JSON) between a C++
     test and a Python test, and run the same parser in the generator.
   - **Pre-parse at registration**, so identification doesn't re-parse strings and validation comes for
     free.

   Use the Axiom `device-descriptor-spec.md` as the starting point for the grammar definition.
3. **Fix the data**: the LTR-329, LTR-390 and M5-Weight detection prefixes (C3) and the LTR `=p20` (C4),
   in RaftCore's own `devtypes/DeviceTypeRecords.json` as well as Axiom's.
4. **Generator hygiene.**
   - **Fixes:** the py:97 crash (C6), the `0x` length (C11) and the address range (C12).
   - **Warn or fail on:**
     - duplicate keys across files;
     - a key that differs from `deviceType`;
     - **empty-detection shadowing at shared addresses** (VEML6040/MXC400xXC, AHT20/TrillRing, SHT40/TrillHex,
       QwiicLEDStick/TrillBar).
   - **Output:**
     - emit the tables `const`;
     - emit `scanPriority`, `excludeFromAddrMap` and `deviceTypeId` into the runtime record (new
       `flags`/`typeId` fields).
5. **`getPollInfo`.** Default `s` to 1 and clamp it to ≥1 (C5). Default `i` to a sensible minimum. Fail
   rather than `continue` on a bad `r{}` (C13).
6. **A registration result.** `addExtendedDeviceTypeRecord` returns
   `enum class AddResult { Added, AlreadyPresent, Full, Invalid }`.
   - DeviceManager must not assign index 0 on failure (C7).
   - Decide a policy for same-name records with different content (C8).
7. **Explicit candidate ordering.**
   - Add an optional `identPriority`.
   - Sort by priority, then by detection strength (records with more checks first).
   - Rank records with no detection last, as "address-only, used only if no other candidate".
8. **Separate queries by purpose.**
   `getDeviceTypeIdxsForAddr(addr, AddrQuery::{AllKnown, IdentCandidates}, out)`. RSAO reservation uses
   `AllKnown`; identification uses `IdentCandidates`.
9. **Lock-free index reads.**
   - Publish an atomic extended-record count with release semantics, so that `getDeviceInfo(idx)` and
     `getPollDecodeFn` don't need the mutex.
   - Add `const DeviceTypeRecord* getBaseRecord(idx)` to avoid copies.
   - Enforce the append-only and capacity invariants in code, not just in comments.
10. **Fix the docs** in §6 and the `RaftBusDevicesIF.h` comments on hook ordering.

**RaftI2C**

1. **Tri-state detection** (I4). `checkDeviceTypeMatch` → `{Match, NoMatch, BusError}`. On `BusError`, abort
   the identification attempt with no fall-through and no hook, and back off briefly.
2. **Per-record identification state and scheduler** (I9, I10, I15). Add to `BusAddrRecord`:
   `identState {None, Pending, Identified, Deferred, NoMatch, BusError, InitFailed}`, `identAttempts`,
   `nextIdentMs` and `identGen`.
   - An identification scheduler, called from the worker with its own time budget, picks due records round
     robin.
   - Each record backs off exponentially on its own.
   - **Never re-run the fallback hook for `NoMatch`** until the table generation changes or the device
     reappears.
   - Expose the state through `devman/identinfo`.
3. **Identity re-verification for polled devices** (I3). After a poll glitch (a failure followed by
   success), or every `verifyIntervalMs` per type, re-run *that type's* detection.
   - On a mismatch, re-identify.
   - On a match after a glitch, re-run init.
4. **Mux and slot correctness** (I1, I2, I8):
   - when a mux goes offline, mark all its slot records offline;
   - count slot-enable failures as poll failures;
   - make `goingOffline` work on composite addresses taken from `_addrStatus`;
   - clear the tracker bits when a record is deleted;
   - age out stale INITIAL records.
5. **Polling** (I5, I7, I14):
   - pick the most overdue record, or go round robin, instead of first-due;
   - enforce a minimum interval;
   - skip records that are not ONLINE;
   - move OFFLINE to PENDING_DELETION consistently;
   - free the CRC recovery entries when a record is deleted or its identification changes;
   - avoid copying `DevicePollingInfo` per poll (store a `shared_ptr<const PollPlan>`).
6. **Aggregator** (I6): `uint32_t` offsets and sizes, a configurable byte cap applied in `resize()`, and
   `_latestValue` reset in `clear()`.
7. **Init outcome** (I11). `processDeviceInit` reports NACKs. Retry it, then set `InitFailed` with backoff
   and do not publish the type. Add `reinitDevice(addr)`.
8. **Callback guarantee** (I12). Every consumed request with a callback gets a `BusRequestResult` carrying
   an error code. Let `sendCmdToDevice` use `setCallbackFromBusTask`.
9. **Multi-consumer deletions and data** (I13). Use per-consumer cursors, or make deletions purely a status
   event.
10. **Keep per-device overrides** (`devconfig` settings) on `BusAddrRecord`, separately from `DeviceStatus`,
    and re-apply them after identification when the type is unchanged.
11. **Housekeeping.**
    - Turn the identification debug defines off.
    - Log when `ADDR_STATUS_MAX` is reached.
    - Fix the stale comments.
    - Move identification delays out of the bus-owner lock, or count them against the scan budget.

**Axiom**

1. **Pin the Raft libraries on `main`**, as the branch does (A9).
2. **Make the type table the single source of RSAO ids.**
   - Generate `RSAOTypeRegistry` from the record `deviceTypeId` (after RaftCore item 4).
   - Delete `kDefaultTypes`, the dead `detectionValues` id suffixes and `split_rsao_records.py`.
   - Until then, move the branch's registry-vs-JSON CI check to `main`.
3. **Fuel gauge by role.** Have the SysTypes `Devices` entry declare `expectType: "MAX17048"`. DevicePower
   checks the type and its fields at registration and logs loudly on a mismatch (A4). Do the same for
   VL53LxCX.
4. **Commissioning.**
   - Probe `ROBOTICAL_ID` at 0x0F before queueing (A3).
   - Keep the first-seen time for the debounce and track last-seen separately (A2).
5. **Back off the RSAO hook per address** (A6, I10), and allow a configured "never RSAO-probe" list.
6. **`_knownSerials`.** Implement the collision comparison the plan describes, or delete it (A5).
7. **Consumers must handle identity changes** (A1), using the event in §9.2-C:
   - DataLogger must re-bind its decoder;
   - DevicePower must re-validate the fuel gauge;
   - VL53LxCX must restart.

**RaftROS (upstream)**

- Adopt the fork's AutoPub changes:
  - re-attach on a type change;
  - drop data callbacks that carry a stale `deviceTypeIdx`.
- Revisit `MAILBOX_RECORD_SIZE` against the largest generated record. Ideally derive it at build time from
  the generated `pollStructSize` values rather than hard-coding 256 or 260.

### 9.2 Enabling capabilities

These are API sketches to guide design, not final signatures.

**A. Type-table generation (RaftCore).**
```cpp
uint32_t DeviceTypeRecords::getGeneration() const;   // bumped on add / override / ident-policy change
```
- Every identification records the generation it was made against (`DeviceStatus::identGen`).
- `typeinfo` and `listdevs` report the generation, so clients key their caches on
  `(deviceid, dtIdx, generation)`.

**B. Per-record identification policy (RaftCore).** Store the policy in a small atomic byte array over
`BASE + MAX_EXTENDED` indices. It does not touch record storage, so pointer stability is preserved.
```cpp
enum class IdentPolicy : uint8_t {
    Enabled       = 0,  // normal candidate
    Disabled      = 1,  // not a candidate; fall through to next record / hook
    DisabledClaim = 2   // not a candidate, but the address counts as claimed: no fallback hook,
                        // still reserved for RSAO arbitration
};
enum class RecSource : uint8_t { Any, Base, Extended };

bool        setIdentPolicy(DeviceTypeIndexType idx, IdentPolicy p, uint32_t ownerTag = 0);
uint32_t    setIdentPolicyByName(const char* name, IdentPolicy p,
                                 RecSource src = RecSource::Any, uint32_t ownerTag = 0);
IdentPolicy getIdentPolicy(DeviceTypeIndexType idx) const;    // lock-free
void        resetIdentPolicies(uint32_t ownerTag);            // a component resets only its own
```
- `getDeviceTypeIdxsForAddr(…, IdentCandidates)` skips records that are not enabled. `AllKnown` includes
  them.
- The RSAO hook resolves through an identification-aware call (`findTypeForIdent(name or typeId)`) that
  honours the policy.
- `getDeviceInfo(idx/name)`, `typeinfo`, decoding and subscriptions are **not** affected. `typeinfo`
  reports `"ident":"enabled|disabled|claim"` and `"src":"base|ext"`.
- `DeviceIdentMgr` skips the fallback hook whenever an address has a `DisabledClaim` record.
- **Name rules:** a by-name rule also applies to extended records registered *later*. This keeps the
  branch's useful "suppress before load" behaviour.
- **Replacement** becomes: "add the extended record, then `setIdentPolicyByName(name, Disabled,
  RecSource::Base)`". The compiled record no longer acts as a fall-through, and the name lookup still
  resolves to the override.
- Every change bumps the generation.

**C. Identity-change event (RaftCore `BusAddrStatus`, RaftI2C `BusStatusMgr`).**
```cpp
enum class IdentEvent : uint8_t { None, Identified, TypeChanged, Lost };
IdentEvent          identEvent = IdentEvent::None;
DeviceTypeIndexType prevDeviceTypeIndex = DEVICE_TYPE_INDEX_INVALID;
```
- Set it under the mutex wherever `deviceStatus` changes, including `clearDeviceIdentification`, and raise
  the change flag.
- Keep `isNewlyIdentified` for compatibility (`Identified || TypeChanged`).
- DeviceManager forwards the event. RaftROS, DataLogger, DevicePower, VL53LxCX and raftjs (through
  `_i`/generation in the published data) detach on `Lost` and re-bind on `TypeChanged`.

**D. Scoped, make-before-break re-identification (RaftCore interface, RaftI2C implementation).**
```cpp
struct ReIdentRequest {
    enum class Scope : uint8_t { All, Unidentified, Address, TypeIndex, StaleGeneration } scope;
    BusElemAddrType     addr    = 0;
    DeviceTypeIndexType typeIdx = DEVICE_TYPE_INDEX_INVALID;
    bool verifyCurrentFirst = true;  // same type still enabled and still first match → keep, no re-init
    bool reinit             = false; // force init even if unchanged
    bool allowFallbackHook  = false; // only if the device was hook-identified originally
};
virtual uint32_t requestReIdentify(const ReIdentRequest& req);       // async; worker boundary
virtual uint32_t reIdentifyDevices() { return requestReIdentify({ReIdentRequest::Scope::All}); }
virtual DeviceTypeIndexType getDeviceTypeIndex(BusElemAddrType addr) const;  // promote from RaftBus
virtual bool getIdentInfo(BusElemAddrType addr, String& json) const;         // state, attempts, candidates
```
- **Servicing** marks the matching records `Pending`. It does **not** clear `deviceStatus`.
- **The identification scheduler** (§9.1 RaftI2C item 2) works through them promptly, within its budget.
- **Outcomes:**
  - same type → keep everything, including the aggregator, the overrides and the sequence counter;
  - a different type → swap, and emit `TypeChanged`;
  - no match → clear, and emit `Lost`.
- **`StaleGeneration`** gives automatic, targeted behaviour after any record or policy change. It covers
  addresses whose candidate list changed, plus devices identified as a type whose record changed or was
  disabled. This removes the need for callers to remember to re-identify.
- **Axiom RSAO** would use `Unidentified` (or nothing at all, once the scheduler is prompt) instead of `All`.
- **Directly connected devices** are explicitly out of scope: their type is bound at setup.

**E. Runtime parity for record metadata (RaftCore).** Add `probeOnly` (the runtime equivalent of
`excludeFromAddrMap`), `scanPriority` and `deviceTypeId` to `DeviceTypeRecordDynamic` and `DeviceTypeRecord`.
- Add `findByDeviceTypeId(uint16_t)`, so RSAO resolution goes id → index directly.
- This makes library RSAO records behave exactly like compiled ones, and makes `DeviceTypeIdSink`, the
  RSAO registry and the registry sealing unnecessary.
- When records are added after `BusScanner::setup`, rebuild the scanner's priority lists on a generation
  change.

**F. Protected system types driven by SysTypes.** Derive the protected set from `DevMan/Devices[].role ==
"system"` plus `expectType`. Expose `isProtectedType(name)` and `isProtectedAddr(addrAndSlot)`, which are
slot-aware. The loader, and any policy change, consult these instead of hard-coded names and addresses.

**G. A decoder for records defined only as data.** Provide a RaftCore facility that interprets
`devInfoJson` field descriptors at runtime.
- **Interface:** `DeviceTypeRecords::getDecoder(idx)` returns either the compiled function or a
  descriptor-interpreting adapter that exposes the same `AttrFieldDesc` view.
- **Inheritance:** allow an override to inherit the compiled decoder when its `resp` schema is
  byte-identical.
- **Starting point:** the removed `DescriptorPollDecoder` (Axiom `181a6db`) is a starting point. It must
  follow raftjs numeric conventions; see C2.
- **Without it:** native ROS should publish raw bytes rather than announce a silent endpoint, and the
  limitation should be documented.

**H. Explicit readiness instead of `postSetup` ordering.** Either have consumers react to generation
changes, or provide `deviceTypeRecords.markTableReady()` and have RSAO return Deferred until the table is
ready. Both remove the hidden dependency on `registerSysMod` order in `main.cpp`.

**I. Prerequisite for live reload, if it is ever wanted.** Stop handing out raw `c_str()` pointers that
outlive the lock. Use immutable per-record snapshots (refcounted, swapped atomically) or never-freed
superseded strings with a slot generation. Until then, document "library changes need a reboot" as a
product constraint.

---

## 10. Suggested sequencing

1. **Phase 0: land the safe fixes.**
   - Cherry-pick the RaftCore and RaftI2C branch fixes listed in §9.1 RaftCore item 1. Land the RaftCore
     and RaftI2C `cmdraw` changes together: RaftCore `cmdraw` no longer pumps the bus loop, so it depends on
     RaftI2C honouring `setCallbackFromBusTask`.
   - Fix the LTR and M5-Weight data.
   - Pin the Raft libraries in Axiom `main`.
   - Upstream the RaftROS AutoPub type-change handling.
2. **Phase 1: correctness of the existing mechanism.**
   - the I1/I2 mux and slot fixes;
   - I5/I7 poll scheduling;
   - I6 aggregator width;
   - C5 defaults;
   - I4 tri-state detection;
   - the generator overlap/shadow warnings and C6;
   - turning debug logging off;
   - the documentation fixes.
3. **Phase 2: the grammar.** A single validated parser with shared test vectors, used by both the C++ and
   the Python generator.
4. **Phase 3: identification model.**
   - per-record identification state and scheduler (this replaces the global 3 s retry);
   - `IdentEvent`;
   - table generation;
   - per-record `IdentPolicy`;
   - `AllKnown` vs `IdentCandidates`;
   - runtime metadata parity (`probeOnly`, `deviceTypeId`) and the generated RSAO registry.
5. **Phase 4: re-identification.** `requestReIdentify` with `StaleGeneration` and make-before-break. Update
   the consumers (DataLogger, DevicePower, VL53LxCX, raftjs cache invalidation).
6. **Phase 5: the device library on the new primitives.**
   - suppression becomes `setIdentPolicyByName`;
   - replacement uses `RecSource::Base`;
   - no explicit `reIdentifyDevices()` calls;
   - protection is driven by SysTypes;
   - the decoder is either available for data-only records or the limitation is documented.

---

## 11. Integration risks for the current branches

1. **Merge conflicts in Axiom.**
   - **`main/main.cpp`:** `main` has since added VL53LxCX, DemoDevice and RaftROS registration. The SysMod
     registration order in this file is load-bearing (§7.3).
   - **`systypes/Axiom018/features.cmake`:** `main` has `FEATURE_ROS2` on and `RaftROS@main`; the branch has
     an option that defaults to off and the Robotical fork pin.
2. **Behaviour changes on a naive merge:**
   - native ROS goes from on to off by default;
   - RaftROS moves to the fork;
   - RaftCore and RaftI2C are pinned to unreleased feature commits;
   - the device catalogue switches to the 18-profile `testsplit.json`.
3. **CI will fail after merging `main`.**
   - `testsplit.json`'s SCD40 no longer matches `main`'s fixed SCD40 (`033ad56`), and
     `check-device-library-selection.mjs` requires an exact match.
   - The testsplit selection also blocks every non-`-devlib` release until it is reverted.
4. **Boot-time data gap.** Every boot clears every identification (RSAO `postSetup`), with about 3 s per
   device to recover. Watch fuel-gauge staleness, VL53L5CX re-initialisation and ROS endpoint churn.
5. **Only the RaftROS fork handles type changes.** DataLogger, DevicePower and VL53LxCX on `main` do not
   (A1).
6. **Library capacity varies by build.** The 20 extended slots are shared with static devices (e.g. Axiom's
   direct-connected devices). `devlib` reports `maxRecords`, but users will not expect the limit to vary.
7. **Other buses ignore `setCallbackFromBusTask`.** RaftCore's `cmdraw` read path depends on the bus
   honouring it, and any bus implementation other than RaftI2C ignores it, so `cmdraw` reads will time out
   on those buses.

---

## 12. Open questions and unverified items

- **Hardware confirmation needed:**
  - **C3:** resolved; reproduced and fix verified on hardware (§12.1).
  - **I8:** stale INITIAL slot records after a main-bus hot-plug (inferred from the scan order).
  - **I17:** whether detection `pauseAfterSendMs` is intended to apply between the write and the read.
- **Figures still to be measured:** the latency, throughput and memory figures in §5.1 are estimates.
- **Product decisions:**
  - Is live library reload (without a reboot) a goal? If so, §9.2-I is a prerequisite for anything that
    replaces records.
  - Should on-device decoding of data-only records (§9.2-G) be a platform capability, or should data-only
    types remain host-decoded only?
  - Should name-level suppression be user-facing at all once per-record policy exists, or should the
    library format express "disable compiled record X" explicitly?

### 12.1 C3 resolved: unprefixed detection values match any responding device

Status: **confirmed by code trace and on hardware; fix verified on hardware** (2026-10-13). See "Hardware test" below.

**The path, end to end:**

1. **Splitting.** `DeviceTypeRecords::getDetectionRecs` (RaftCore DeviceTypeRecords.cpp:660-688) splits
   `0x86=A005` into write `0x86` and value `A005`.
2. **Parsing the value.** `extractCheckInfoFromHexStr` (:591-616) passes `a005` to
   `extractMaskAndDataFromHexStr` (:499-584).
   - The value contains no `r`, no `0x` and no `0b`, so all three branches are skipped.
   - The function reaches its final `return true` with **empty** mask and check vectors.
   - `extractCheckInfoFromHexStr` pushes that empty pair, so the record has one check value of length 0.
3. **Building the transaction.** `DeviceIdentMgr::checkDeviceTypeMatch` (RaftI2C DeviceIdentMgr.cpp:272-378)
   sees `checkValues.size() == 1`, so it does not `continue`. It sets
   `readDataCheckBytes = checkValues[0].second.size() = 0` and issues a **write-only** transaction (`0x86`,
   read length 0).
4. **The bus result.** `BusI2C::i2cSendSync` (BusI2C.cpp:555-615) passes a read length of 0.
   - `RaftI2CCentral::access` (the default driver on ESP32, S3 and C3) classifies this as
     `ACCESS_WRITE_ONLY` and returns `RAFT_OK` if the address and register byte are ACKed.
   - The ESP-IDF driver (C5/C6) does the same through its write-only path.
   - The device has already been seen responding at that address by the scanner, so it will almost always
     ACK a one-byte register-pointer write.
5. **The comparison.** `readData.size() (0) == checkValue.second.size() (0)` is true, and the byte-compare
   loop does not execute, so `checkValueMatch = true`. The record is reported as a **match**.

**Effect.**
- Each affected record is the **last** candidate at its address in both RaftCore's and Axiom's
  `DeviceTypeRecords.json`.
- So it claims any device at that address that failed every earlier record. That includes a known device
  whose own detection failed because of a transient bus error (see I4).
- It also stops the RSAO fallback hook from ever running at those addresses.

| Record | Address | Earlier candidates (RaftCore / Axiom) | Devices that would be mis-identified (examples) |
|---|---|---|---|
| LTR-329 `0x86=A005` | 0x29 | VL6180, VL53L0X, VL53L1X, VL53L4CD, CAP1188, TrillSquare / VL6180, VL53L0X, VL53L1X, VL53L4CD, VL53L5CX | TSL2591, TCS34725, any 0x29 ToF variant not listed, or a listed VL53 whose detection NACKed once |
| LTR-390 `0x06=B2` | 0x53 | ADXL313 | ADXL345/ADXL343 (common at 0x53), or an ADXL313 that NACKed once |
| M5-Weight `0xFF=26262626` | 0x26 | TrillBar / none | Any device at 0x26 |

**Why it has gone unnoticed.**
- A genuine LTR-329, LTR-390 or M5-Weight *is* identified correctly, because it is the device present and
  the check is vacuous.
- The listed VL53 parts normally match their own, correctly-prefixed records first.
- The failure only shows with an uncatalogued part at one of these addresses, or after a detection glitch.
  Because identification is never re-verified (I3), the wrong type then persists until the device goes
  offline.

**Intended values** (checked against the datasheets on 2026-10-13).

- **LTR-329ALS-01** (Lite-On DS86-2014-0006):
  - **Register set table:** PART_ID `0x86` has a reset value of **0xA0**. Bits 7:4 are the Part Number ID
    `1010`; bits 3:0 are the Revision ID `0000`.
  - **Datasheet typo:** the per-register heading says "default = 0x92". This contradicts both the register
    table and the bit-field table, so treat it as a datasheet error.
  - **MANUFAC_ID:** `0x87` reads **0x05**.
  - **Suggested detection:** `0x86=0b1010XXXX&0x87=0x05`. This is two independent checks, so it does not
    rely on register auto-increment, and it tolerates a future silicon revision.
  - **Exact alternative:** `0x86=0xA005` is also valid if a two-byte read auto-increments into 0x87.
- **LTR-390UV-01** (Lite-On DS86-2015-0004, V1.2):
  - **PART_ID:** `0x06` has a default of **0xB2**. Bits 7:4 are the Part Number ID `1011`. Bits 3:0 are the
    Revision ID `0010`, which "increases by one each time a new silicon revision is manufactured".
  - **Suggested detection:** `0x06=0b1011XXXX`. This matches the Linux IIO driver, which checks only the
    part-number nibble (`LTR390_PART_NUMBER_ID 0xb`).
  - **Exact alternative:** `0x06=0xB2` would reject a later silicon revision.
- **M5-Weight:** `0xFF=26262626` appears to mean "register 0xFF returns the device's own address 0x26".
  This still needs confirming against the M5Stack unit's register map before choosing between `0xFF=0x26`
  and a 4-byte read.

Address 0x53 is also shared with ADXL313, and ADXL345/ADXL343 report DEVID 0xE5 at register 0x00. With the
corrected LTR-390 value, an ADXL345 at 0x53 would correctly match nothing.

**Fix (two parts, both small).**
1. **Data:** prefix the three detection values, in both RaftCore `devtypes/DeviceTypeRecords.json` and
   Axiom `AxiomDevTypes/DeviceTypeRecords.json`, together with the related LTR `=p20` init fix (C4).
2. **Parser:** in `extractMaskAndDataFromHexStr`, a value that is not empty and is not `0x`, `0b`, `r` or a
   pure pause (`pNN`) must return **false**. That makes `getDetectionRecs` drop the record entry, so the
   mistake surfaces as "no detection" rather than a silent match.
   - Better still, `ProcessDevTypeJsonToC.py` should reject such values at build time.
   - The Axiom `check-descriptor-spec.mjs` should reject them for library records.
   - A detection entry that ends up with no usable check should be treated as a **record error**, not
     "matches anything".

**Confirming on hardware (optional).**
- Put any non-LTR device that ACKs at 0x29 (e.g. a TSL2591 or TCS34725 breakout) or at 0x53 (e.g. an
  ADXL345) on a board running the current firmware.
- With `DEBUG_DEVICE_IDENT_MGR` on, which is the default on `main`, the log shows
  `checkDeviceTypeMatch OK … readSize 0` followed by identification as LTR-329 or LTR-390, and
  `devman/typeinfo?deviceid=…` reports the wrong type.

**Hardware test** (2026-10-13, UM FeatherS3 with an MPU6050 at 0x68 and an APDS9960 at 0x39).

*Firmware.* RaftROS `examples/ExampleDiscoverable` built with a test-only SysType,
`SysTypeFeatherS3C3Test`. It is a copy of `SysTypeFeatherS3` with two records placed ahead of the real
ones:
- `C3Unprefixed` at 0x68, with detection `0x75=FF`. The MPU6050 WHO_AM_I register reads 0x68, so this
  record must never match.
- `C3Prefixed` at 0x39, with detection `0x92=0xFF`, as a control. The APDS9960 ID register reads 0xAB, so
  this record must not match either.

| Build | 0x68 identified as | 0x39 identified as |
|---|---|---|
| RaftCore/RaftI2C `main` | **C3Unprefixed** (dtIdx 49): bug reproduced | APDS9960 (control correctly rejected: `readData ab` vs `ff`, NO MATCH) |
| `fix/detection-value-validation` | **MPU6050** (dtIdx 52). Log: `W DevTypeRecs: getDetectionRecs C3Unprefixed invalid detection value 0x75=FF`, then `identifyDevice new device MPU6050` | APDS9960 |

*The fix* (branch `fix/detection-value-validation` in RaftCore and RaftI2C; not yet committed):

- **RaftCore `extractMaskAndDataFromHexStr`:** a value that is not `0x…`, `0b…` or `r…` may now contain
  only a pause (`pNN`), as in the write-then-pause step `0x00FF=p25`. Anything else returns false.
- **RaftCore `getDetectionRecs`:** now returns `bool`. An invalid entry invalidates the whole record
  (with `LOG_W`) instead of being skipped, because skipping an entry would make the record *easier* to
  match.
- **RaftI2C `DeviceIdentMgr::checkDeviceTypeMatch`:** returns no-match when `getDetectionRecs` fails.
- **RaftCore `devtypes/DeviceTypeRecords.json`:** LTR-329 `0x86=0b1010XXXX&0x87=0x05`; LTR-390
  `0x06=0b1011XXXX`; M5-Weight `0xFF=0x26`. The M5-Weight value comes from M5Stack's
  `M5UnitWeightI2C.h`, which defines `I2C_ADDRESS_REG 0xFF` and `DEVICE_DEFAULT_ADDR 0x26`. The stale
  LTR-329 note ("matches any device at 0x29") is replaced.

*Impact check.* Every shipped device type file was scanned with the new rule: RaftCore, Axiom, the
RaftROS example SysTypes and the RaftI2C test records. Only the three records above, plus the test record,
are affected. No poll strings are affected.

**Required alongside the fix: Axiom's `AxiomDevTypes/DeviceTypeRecords.json`** carries the same three
bad values. With the parser fix, and without the matching data fix, genuine LTR-329, LTR-390 and
M5-Weight devices would stop being identified on Axiom.
