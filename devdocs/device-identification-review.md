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
| C1 | High | **Heap overflow in `extractMaskAndDataFromHexStr` (`rN`)**: the loop runs over string positions, but the vector is sized from the parsed number | DeviceTypeRecords.cpp:511-525 | `"r1p50"` (the wiki's own example) writes 4 bytes into a 1-byte vector. *Fixed on `main` 2026-10-13*: a bounded fill (same indices as before, minus out-of-range ones), and a zero or missing length is rejected because it would be an empty check that matches anything. The branch's 1024-byte read limit was not taken. Verified with a host ASan/libstdc++-assertions harness: `r1p50`, `0x60=r1`, `r01`, `r0000001` and `r0` all wrote out of bounds before the fix and none do after; `r6` and the LTR-390 poll string give identical results |
| C2 | High | **Generated C++ decoders shift in the wrong direction** compared with raftjs (`<<=` for `s>0`) | DecodeGenerator.py:514-519 vs raftjs `RaftAttributeHandler.ts:461-467` | AHT20 humidity ×2²⁴, LPS25 pressure ×2¹⁶, AMG8833 wrong scale, in every on-device decode (DeviceManager named values, RaftROS, MicroPython). *Fixed on `main` 2026-10-13* (`283792b` applied, with its g++ regression test `scripts/tests/test_standard_decode.py`, which fails on the old generator). Affected fields: AHT20 humidity, LPS25 pressure, AMG8833 temperature. The single-bit flags (VL53 `valid`, CAP1188/CAP1203, gamepad, QwiicButton) are stored as `bool`, so they were unaffected |
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
| A8 | Low | **Stale comments and logs** claim Deferred is "retried only on offline→online", and the hook runs "before default identification" | RSAOSysMod.cpp:598; DeviceIdentMgr.cpp:247-252; RaftBusDevicesIF.h:37,226 | Misleads diagnosis *Fixed 2026-10-13: comments in RaftCore, RaftI2C and Axiom RSAO, and the RSAOSysMod log message, now describe the hook as a fallback after address-based identification, with periodic retry* |
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

- **`setCallbackFromBusTask`** fixed a real problem on the branch's base (reads could never complete), but
  `main` has since solved that differently (`36b0dc3`, servicing the bus loop while waiting). Both are
  blocking designs and are superseded by deferred REST responses (§13), so the branch's version should not
  be merged. The lost-callback paths (I12) remain, and become a prerequisite for §13.
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
   - the `listdevs` filter.

   (Done on `main` 2026-10-13: C1, C2, `appendRawField`, null-bus guards and the `listdevs` filter. The
   branch's `cmdraw` changes — `setCallbackFromBusTask` and the mutex — are **not** to be taken; see §13.)
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
   an error code. This is a prerequisite for deferred REST responses (§13). `sendCmdToDevice` should move to
   the deferred-response mechanism rather than waiting on any task.
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
   - Cherry-pick the RaftCore and RaftI2C branch fixes listed in §9.1 RaftCore item 1 (done 2026-10-13,
     except the branch's `cmdraw` changes, which are superseded by §13).
   - Fix the LTR and M5-Weight data (done 2026-10-13).
   - Pin the Raft libraries in Axiom `main`.
   - Upstream the RaftROS AutoPub type-change handling.
2. **Phase 0b: non-blocking REST responses (§13).** Deferred responses for all transports, then `cmdraw`
   and `cmdjson` moved onto them. Requires RaftI2C I12 first.
3. **Phase 1: correctness of the existing mechanism.**
   - the I1/I2 mux and slot fixes;
   - I5/I7 poll scheduling;
   - I6 aggregator width;
   - C5 defaults;
   - I4 tri-state detection;
   - the generator overlap/shadow warnings and C6;
   - turning debug logging off;
   - the documentation fixes.
4. **Phase 2: the grammar.** A single validated parser with shared test vectors, used by both the C++ and
   the Python generator.
5. **Phase 3: identification model.**
   - per-record identification state and scheduler (this replaces the global 3 s retry);
   - `IdentEvent`;
   - table generation;
   - per-record `IdentPolicy`;
   - `AllKnown` vs `IdentCandidates`;
   - runtime metadata parity (`probeOnly`, `deviceTypeId`) and the generated RSAO registry.
6. **Phase 4: re-identification.** `requestReIdentify` with `StaleGeneration` and make-before-break. Update
   the consumers (DataLogger, DevicePower, VL53LxCX, raftjs cache invalidation).
7. **Phase 5: the device library on the new primitives.**
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
7. **The branch's `cmdraw` changes conflict with `main` and with §13.** The device-library branch replaces
   `main`'s `cmdraw` wait (which services the bus loop itself) with `setCallbackFromBusTask` plus a mutex. Both
   are blocking designs and are superseded by deferred responses (§13), so those commits (RaftCore
   `53b3ff4`, `d83c6a9` (cmdraw part), `e2b3bd3`, `6d29eb3`; RaftI2C `8dc1ebf`) should be dropped from the
   branch before it merges.

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

---

## 13. Design decision: non-blocking (deferred) REST responses

Status: **agreed direction (option B), not yet implemented.** Reviewed 2026-10-13.

### 13.1 Problem

`devman/cmdraw` with `numToRd > 0` (and `DeviceIdentMgr::sendCmdToDevice`, behind `devman/cmdjson`) waits
inside the REST handler for the bus result. **Every transport runs REST handlers on the main task:**
- WebSocket, BLE and serial RICSerial via `CommsChannelManager::loop` → `ProtocolExchange`;
- HTTP via `WebServer::loop` → `RaftWebConnManager::serviceConnections`;
- console and remote-debug lines in their SysMod loops.

So any wait stalls all communications, publishing and other SysMods.

How the blocking approaches have evolved:
- **`b43d8de` (Aug 2026):** waited on a `volatile` flag. This could never complete on the main task.
- **`36b0dc3` (Sep 2026, current `main`):** services the bus loop itself while it waits. This works (20/20 reads
  measured on the FeatherS3 over WebSocket and HTTP, 24–78 ms), but it only works on the main task, it
  re-enters the bus loop from inside a REST handler, and it still blocks.
- **The device-library branch:** delivers the callback on the bus task and adds a mutex. This works from any
  task, but the callback runs under the bus-owner lock, it needs every bus implementation to support it, and
  it still blocks (up to 500 ms).

**Decision:** the handler must not wait. It returns "pending", and the response is sent when the result
arrives.

### 13.2 Options considered

- **A. Acknowledge now, deliver the result as a `msgKey` report message.** Not chosen: every client would need
  new matching code, and HTTP and the console still need separate handling.
- **B. Deferred response that keeps the request's identity.** **Chosen.** raftjs already matches responses by
  message number, out of order, with up to 255 outstanding (`RaftMsgHandler.ts`), so existing calls such as
  `sendRICRESTURL` and `cmdRawWriteRead` keep working with no client change. It is a general mechanism that
  any slow endpoint can use.
- **C. Start then poll** (like `wifiscan/start` and `wifiscan/results`). Not needed, because B covers HTTP
  (§13.4).
- **Bus-task callback or loop servicing.** Rejected because both still block.

### 13.3 Core mechanism (RaftCore)

1. **A pending return code.** Add `RAFT_RESP_DEFERRED` to `RaftRetCode`; do not reuse `RAFT_BUS_PENDING`.
2. **Request context.** Extend `APISourceInfo` (currently just `channelID`) so a handler can complete the
   request later:
   - `protocol` and `msgNum` (for RICSerial / RICFrame);
   - `canDefer` (false by default; set by transports that support completion);
   - `deferToken` (non-zero, monotonically increasing, never reused).
3. **Completion API.** A handler that has `canDefer` returns `RAFT_RESP_DEFERRED` with an empty response,
   keeps the context (or token), and later calls
   `restAPIEndpointManager.completeDeferred(ctx, respJson)`. This must be called on the main task (enforce with
   `RAFT_CHECK_MAIN_TASK`). Results from other tasks must be queued to the main task first, the same rule as
   `sendBufOnChannel`.
4. **Per-transport completion.** Each transport registers how to deliver a completed response, keyed by
   channel or token (see §13.4).
5. **Deferred timeout.** The framework sweeps pending requests on the main task and completes each one with
   `{"rslt":"fail","error":"timeout"}` after a per-endpoint limit. The default should be about 1 s, so the reply
   always arrives inside raftjs's 2 s retry window.
6. **Duplicate requests.** raftjs resends a request it hasn't had an answer to within 2 s. If the same
   channel and message number arrive while the original is still pending, ignore the repeat rather than
   running it again (`cmdraw` writes to hardware). Together with item 5 this means repeats should not occur.
7. **`handleApiRequest`** must stop discarding the handler's return code. It returns `RAFT_RESP_DEFERRED`, and
   callers that cannot defer simply never set `canDefer`.

### 13.4 Every caller and transport

| Caller / transport | How the deferred response is delivered | Change needed |
|---|---|---|
| **RICSerial over WebSocket, BLE and serial** (raftjs, WebUI, apps) | `ProtocolExchange` already sends nothing when the response is empty (`ProtocolExchange.cpp:252-266`). Completion builds `CommsChannelMsg`, calls `setAsResponse(channelID, protocol, msgNum, MSG_TYPE_RESPONSE)` (already exists, `CommsChannelMsg.h:110`), encodes as `CMDRESPJSON` and calls `outboundHandleMsg` | Pass protocol and msgNum in `APISourceInfo`; do not convert a deferred return into "API not found" (`:224`) |
| **Bridged RICSerial** (CommandSerial to a downstream device) | Bridging forwards frames with the inner message number unchanged, and raftjs matches the inner number | None |
| **HTTP** (curl, `raft fs` / `raft ota`, scripts) | **Feasible.** `RaftWebConnection` polls the responder every main-loop pass, keeps an active responder open without blocking, and refreshes its idle timer while active. The 5-minute connection cap is the backstop. Change `RaftWebResponderRestAPI` to: (a) call the handler from `loop()` once the body is complete, for **all** methods, instead of inside `getContentLength()` (GET) or after the headers have already gone (POST); (b) while pending, report "no headers or response yet" (`isStdHeaderRequired()` / `responseAvailable()` false) and keep `readyToReceiveData()` true so a client disconnect is still detected; (c) on completion, set the response string so the next pass sends the headers with the real `Content-Length`, then the body. Add `RaftWebConnManager::completeDeferredResponse(token, resp)`, which scans its 12 connection slots by token like `sendBufOnChannel`. A disconnected client's responder is deleted, so a late completion just misses | Responder rework (also fixes today's POST behaviour of sending `200` before the handler runs, and the `!=` body-length check that can stall). HTTP requests all share one channel ID, so match by token, not channel. Cap concurrent deferred HTTP requests (return 503 above the cap) so slow requests cannot use up the connection slots; check `CONFIG_LWIP_MAX_SOCKETS` against `numConnSlots` |
| **SerialConsole ASCII** (`raft m`, Axiom `request_ascii`) | Completion prints the JSON line to the console. The reply includes `"req"`, so line-oriented clients can still match it | Console registers a completion that calls `putStr` |
| **LoggerRaftRemote** (raft CLI remote debug, channel 20006) | Same as the console, sent over the remote socket if it is still connected | Register a completion |
| **CommandFile / CmdsAtStart** | The response is ignored today | Allow deferral. If a command sequence must wait for completion, add an optional completion callback to its state machine (never a blocking wait) |
| **MQTT (RICJSON)** | The response is discarded today | None (fire and forget) |
| **Internal `handleApiRequest` callers** (e.g. RaftROS `onConsoleLine`) | They discard the response | None; `canDefer` stays false |
| **FileStreamSession stream start** | Uses the handler's return code | These endpoints do not defer |
| **`SysManager::sendCmdJSON`** | Goes to SysMod `receiveCmdJSON`, not the REST API | Unaffected |

**RaftROS:**
- The firmware does not call `cmdraw` or `cmdjson`. Its own endpoints (`ros`, `rosstat`) are synchronous,
  and `onConsoleLine` discards responses. Its service registry already has a deferred outcome
  (`AutoPubServiceRegistry.h:52-56`).
- There is **no host-side RICSerial or WebSocket ROS bridge**: the host variant is Zenoh-only and its tools
  use HTTP. So RaftROS needs no change for B.
- If a host bridge is ever written, it must match responses by message number, as raftjs does.

**Other clients:**
- **raftjs:** no change.
- **Axiom `tests/hardware/serial_protocol.py` (`request_framed`):** already matches by message number. It
  should drop stale frames for a number when that request times out, so a late reply can't be paired with a
  reused number.
- **SandBot `serial_telemetry.py`:** ignores responses.
- **raft CLI (Rust `raftcli`):** uses the console, remote debug and HTTP, all covered above.

### 13.5 `cmdraw` and `cmdjson` on the new mechanism

- **`cmdraw` with `numToRd > 0`:**
  - Queue the bus request as now, carrying the deferred context (or token) as the callback data, so several
    requests can be pending at once in place of today's single `_cmdRawInFlightCmdId`.
  - Return `RAFT_RESP_DEFERRED`.
  - The result callback already runs on the main task, from `BusAccessor::loop()`. It completes the deferred
    response with `rdData`, or with the bus error once I12 is fixed.
  - No waiting, no bus-loop servicing, no cross-task shared buffer.
- **Write-only `cmdraw`:** replies immediately, as now.
- **`timeoutMs`:** becomes the deferred timeout, capped at about 1 s.
- **`DeviceIdentMgr::sendCmdToDevice` (`cmdjson`):** moves to the same pattern, removing its special case
  for waiting on the main task.
- **Prerequisite: RaftI2C I12.** Every path that consumes a queued request must invoke its callback with a
  result code: paused-drop, barred, BUSY, slot-enable failure, length mismatch and response-queue full.
  Otherwise the deferred response only completes by timeout.

### 13.6 Work items and order

1. **RaftI2C I12:** guarantee a callback for every queued request.
2. **RaftCore:**
   - the `RAFT_RESP_DEFERRED` return code;
   - the extended `APISourceInfo`;
   - the deferred registry with timeout sweep and completion API;
   - `handleApiRequest` returning the handler's code;
   - `ProtocolExchange` support (pass msgNum and protocol, send on completion, ignore repeats);
   - unit tests.
3. **RaftWebServer:** `RaftWebResponderRestAPI` rework and `completeDeferredResponse`; deferred-request cap;
   check `CONFIG_LWIP_MAX_SOCKETS`.
4. **RaftSysMods:** SerialConsole, LoggerRaftRemote and CommandFile completion handling.
5. **DeviceManager:** `cmdraw` and `cmdjson` moved to deferred responses. Remove `_cmdRawInFlightCmdId`,
   `_cmdRawResultReady` and the bus-loop servicing.
6. **Clients:** harden Axiom `serial_protocol.py`; document deferred behaviour in the wiki (REST API page and a
   "writing slow endpoints" note).
7. **Device-library branch:** drop its `cmdraw` / `setCallbackFromBusTask` commits (§11 item 7).
8. **Verification on the FeatherS3** with raftjs (WebSocket), curl (HTTP) and the console:
   - `cmdraw` reads succeed;
   - the handler returns in well under 1 ms;
   - several reads can be in flight at once;
   - a client disconnect while pending is handled cleanly;
   - a forced bus error returns its error code, not a timeout;
   - the main loop's timing statistics show no stall.

### 13.7 Decisions and open points

Decided (2026-10-13):
- **Timeout and error reporting use the Raft JSON convention** on every transport, including HTTP: the
  response is `{"req":"...","rslt":"fail","error":"timeout"}` (HTTP status 200). No HTTP status codes are
  needed, so the responder does not need a configurable status.
- **Timeouts:** the framework has a **default deferred timeout** (about 1 s, inside raftjs's 2 s retry
  window). An endpoint can **override it**, for operations with a known longer duration or a known issue. The
  value is set when the handler defers (e.g. `deferResponse(ctx, timeoutMs)`), so it can depend on the
  request. An override longer than 2 s must be paired with a longer `msgTimeoutMs` in the raftjs call, or
  raftjs will resend the request, and the framework will ignore the repeat.
- **Single final result only.** Deferral suits operations that produce one result after a delay. Operations
  with intermediate or progressive results stay on their existing patterns: `wifiscan` start-then-poll,
  publish topics, file streaming.

**Prior art:** RaftROS already implements this pattern for ROS services. `AutoPubServiceRegistry.h` has
outcomes `Replied`/`Deferred`/`Refused`, a token, `complete(token)`, a deadline sweep each loop pass, and
`svcDeferred`/`svcTimedOut` stats. The RaftCore REST version should follow the same shape.

**Important:** deferral only removes a wait from the main task if the work itself moves too. Completion has to
come from a callback, a loop()-driven state machine or a worker task. Simply returning "deferred" around an
existing blocking call achieves nothing.

**Other existing endpoints that would benefit** (survey 2026-10-13; the top entries checked in code):

| Priority | Endpoint | Today | With deferral |
|---|---|---|---|
| 1 | `espFwUpdate` (RaftSysMods ESPOTAUpdate) | After the last block, waits with `vTaskDelay(10)` for the OTA worker queue to drain, **up to 15 s on the main task** (`OTA_COMPLETION_WAIT_MAX_MS`) | Complete when the worker finishes verifying. Needs a long timeout override |
| 2 | `rsao/arb` (Axiom RSAO) | Returns `"queued":true`; the client must poll `rsao/status`. Takes tens of seconds | Complete with the arbitration outcome. Needs a long timeout override (raftjs `msgTimeoutMs` to match) |
| 3 | `devman/cmdraw`, `devman/cmdjson` | See §13.5. `cmdjson` on the main task queues the command with no callback, so the bus result and any read data are **dropped** | Complete from the bus result callback |
| 4 | `vl53/detect`, `init`, `start`, `stop` (Axiom VL53LxCX) | Queued to a worker task; the result is only visible through `vl53/status` (the init firmware upload takes seconds) | Complete when the worker finishes the command |
| 5 | `datalog?action=start` (Axiom DataLogger) | Loops up to 10 × `vTaskDelay(500)` waiting for devices to be identified: **up to 5 s on the main task** | loop()-driven wait, then complete |
| 6 | `rsao/cmd/...` (Axiom RSAO test mode) | `SCAN`, `RST`, `TPOLL` run synchronously on the main task for long or caller-chosen times | Move to a worker or bus-task service, then complete |
| 7 | `w/<ssid>/<pw>` (WiFi STA set) | Retry loop of up to 2 s on the main task, then replies before the connection outcome is known | Complete with connected/IP or failed. Useful over BLE, serial or the AP; a client on the STA link may lose its channel |
| 8 | `reformatfs`, `filehash` (FileManager) | Synchronous format or whole-file CRC; can take seconds | Worker or chunked loop(), then complete (format's reply must still go out before the restart) |
| 9 | `power/fg/*` (Axiom DevicePower) | Up to ~40 ms of delays; writes are queued with results ignored, and the reply says only "sent" | Complete with the outcome (and the new SOC for quickstart) |
| 10 | `ros/set`, `ros/clear` (RaftROS) | Replies `"applied":"next loop pass"`; the client polls `ros` | Optionally complete when applied or when the session is ready |
| 11 | MotorControl `home` (and opt-in motion completion) | Queued; the outcome is not reported | Opt-in completion (e.g. `wait=1`), since motion is normally streamed |

**Not suitable for deferral:**
- **Progressive or streaming:** `wifiscan`, `subscription`, `log/*`, `devman/demo`, `datalog simulate`.
- **The reply must go out before the channel or device goes away:** `reset`, `blerestart`, `bledisconnect`,
  `wc`, `power/off`, `console/settings?baud=`.
- **Already fast synchronous getters and setters.**

Open: confirm which of the candidates above to schedule after `cmdraw`/`cmdjson`. `espFwUpdate`, `rsao/arb`,
`vl53/*` and `datalog start` look like the most valuable next ones.

### 13.8 Preparatory work done and benchmark baseline (2026-10-13/14)

**Changes** (uncommitted on `main` at the time of writing):

- **RaftI2C (I12):** every queued (non-polling, non-scan) request now gets its callback with a result code.
  - **`BusI2C::i2cSendAsync`** reports early failures with their own codes: address barred or invalid, bus-owner
    lock busy (`BUSY`), slot-enable failure, and no I2C driver (`BUS_NOT_INIT`).
  - **`BusAccessor::processRequestQueue`** reports a request dropped because the bus is paused
    (`BUS_NOT_READY`).
  - **`BusAccessor::handleResponse`** reports a NACK or short read (`BUS_ACK_ERROR` / `BUS_INCOMPLETE`, with no
    data) instead of dropping it.
  - **Unchanged:** polling results (only complete polls are reported, with the OK/pending result as before).
  - **Not covered:** a full response queue still cannot be reported through the queue. It is counted in the
    bus stats; the deferred-response timeout will cover it.
- **RaftCore:**
  - **`BusRequestResult`** has a constructor taking the actual `RaftRetCode` (the old `bool` form is kept).
  - **`cmdraw`** returns the bus error code (e.g. `{"rslt":"fail","error":"BUS_ACK_ERROR"}`) instead of an empty
    `rdData`.
  - **`SupervisorStats`:** `sysmoddebug/SysMan` now returns the last period's loop statistics. Previously it
    always returned `{}`, because the summary was cleared straight after being logged. `minUs` is no longer
    stuck at 0.

**Benchmark method:**
- **Client:** a raftjs script (`bench.js`; Node 20 in the `raftbuilder` image) on the host, using the
  FeatherS3 (MPU6050 at 0x68, APDS9960 at 0x39) over WiFi.
- **Phases:**
  - idle;
  - `devjson` streaming at 10 Hz;
  - 100 sequential `cmdraw` reads while streaming;
  - 10 bursts of 5 concurrent `cmdraw` reads while streaming;
  - error paths: a read from an empty address (`1_50`), a bad slot (`1_168`), and a write-only command to an
    empty address;
  - HTTP `cmdraw` / `listdevs` / `v`;
  - WebSocket `listdevs` / `v`.
- **Device-side main-loop statistics** come from `sysmoddebug/SysMan` with `sysman?interval=5`.
- **WiFi latency varies a lot on this bench** (ping 8–240 ms between runs). Interleave A/B runs, check ping
  before each run, and use the WebSocket/HTTP `v` latency as the network-only reference. Device-side loop
  statistics are not affected by the network.

**Results** (quiet-network runs, ping about 9 ms):

| Metric | Before I12 | After I12 |
|---|---|---|
| WS `v` p50 (network reference) | 51 ms | 51 ms |
| `cmdraw` read p50 / p95, sequential while streaming | 50 / 65 ms | 52 / 81 ms |
| 100 sequential `cmdraw` reads | 5.07 s | 5.35 s |
| 5 concurrent `cmdraw` reads, p50 / p95 | 158 / ~260 ms | 155 / ~290 ms (requests are serialised by the blocking handler) |
| HTTP `cmdraw` / `listdevs` / `v` p50 | 50 / 40 / 39 ms | 50 / 39 / 38 ms |
| `cmdraw` read, empty address | `readTimeout` 10/10 | `BUS_ACK_ERROR` 10/10 |
| `cmdraw` read, bad slot | `readTimeout` 10/10 | `BUS_ACK_ERROR` 9/10 (the remainder are the 20 ms cap) |
| Main loop, idle avg / max | ~1.7 ms / 9–19 ms | similar |
| Main loop max during the `cmdraw` burst | 16–20 ms (CommsMan up to 14 ms: the blocking wait) | similar |

**What deferred responses (§13) should improve against this baseline:**
- **The main loop no longer blocks:**
  - CommsMan and WebServer `slowUs` during `cmdraw` bursts should drop to the cost of queuing the request;
  - the main-loop maximum during bursts should match the streaming-only value;
  - the gaps between publishes during bursts should shrink.
- **Concurrency:** concurrent `cmdraw` reads should overlap rather than serialise, so the concurrent p50 should
  approach the sequential p50.
- **Error paths:** they should never time out.

### 13.9 Slice 1 implemented and tested (2026-10-14, branch `feature/deferred-responses`)

**Implementation (RaftCore):**
- **`RAFT_RESP_DEFERRED`** return code.
- **`APISourceInfo`:** adds `msgProtocol`, `msgNum`, `canDefer` and `deferToken`.
- **`RestAPIDeferredResponses`** (header-only registry):
  - `defer` / `bindDelivery` / `complete` / `cancel` / `isPending` / `loop`;
  - default timeout 1 s, maximum 60 s, up to 16 pending;
  - a delivered response is removed from the registry before the delivery function runs, so delivery can safely call back into the registry;
  - statistics.
- **`RestAPIEndpointManager`:**
  - `deferResponse()` and `completeDeferred()`, both checked to run on the main task;
  - `loop()`, which `SysManager::loop()` calls;
  - `handleApiRequest()` passes `RAFT_RESP_DEFERRED` through and otherwise still returns `RAFT_OK`.
- **`ProtocolExchange`:**
  - numbered RICREST URL and command-frame requests are deferrable;
  - a resend of a request that is still pending is ignored;
  - the deferred response is sent as a `MSG_TYPE_RESPONSE` carrying the original message number;
  - the raw command-frame path (MQTT) is unchanged and does not defer.
- **`cmdraw` reads** defer when the transport allows it:
  - the bus result callback (on the main task) completes the request with `rdData` or the bus error;
  - if the request can't be queued it is cancelled and an immediate error is returned;
  - HTTP and the console still use the immediate (waiting) path until slices 2 and 3.
- **Stats:** `sysmoddebug/RestAPI` returns the registry statistics.

**Testing:**
- **Unit tests** (`unit_tests/main/RestAPIDeferredResponses_test.cpp`, run on the FeatherS3): 14 new tests, 106/106 pass in total. They found one real bug before hardware testing: a response that completed before the delivery function was bound was delivered from freed storage. Fixed by passing the response by value.
- **Integration** (`deftest.js`, raftjs over WebSocket plus HTTP): 14/14 checks pass.
  - sequential and concurrent reads;
  - NACK and bad-slot reads return `BUS_ACK_ERROR` with no timeouts;
  - forced 1 ms timeouts produce timeout responses, and the late completions are rejected;
  - a duplicate frame (same message number) gets exactly one response;
  - a client disconnecting with a response pending is harmless;
  - write-only `cmdraw`, `listdevs` and `v` are unaffected;
  - HTTP still works via the immediate path;
  - the JSON command-frame form defers too.
- **Soak:** 2000 mixed requests while `devjson` streams at 10 Hz. Every request got a correct response or a correct bus error, with nothing pending at the end. Heap returned to its starting level, and there were no panics or main-task check errors.
- **Not exercised on hardware:** the 16-entry capacity limit. Completions take only a few ms, so the most ever pending at once was 5; the limit is covered by unit tests.

**Main-loop effect** (1 s stats windows, 200 sequential reads, interleaved A/B runs):

| Per window | `main` (blocking) | Deferred | `v` control |
|---|---|---|---|
| CommsMan worst pass, p50 / max | 13.9–14.8 / 16.6 ms | 6.2 / 7.0 ms (a second run gave 5–8 ms) | 2–4 ms |
| Main-loop max, p50 | 17.1–17.3 ms | 9.1 ms | 6.6–8.6 ms |
| NACK reads: CommsMan max | 28–36 ms | 5.9 ms | — |
| NACK reads: main-loop max | 41–51 ms | ~27 ms (all in DevMan, see below) | — |

**Client-side latency** is unchanged (cmdraw p50 ~51 ms, dominated by the network). Concurrent-request latency can't show the overlap, because raftjs sends at most one queued message per 50 ms tick.

**Findings for follow-up:**
1. **NACK stall in DevMan.** After a NACKed read, `DeviceManager::loop()` (the RaftI2C main-task bus loop) takes ~24–26 ms. This is not caused by deferral: it was hidden inside the larger stall in the blocking build. Instrument `BusI2C::loop()` (scanner, status manager, multiplexers, power controller, accessor) to find it.
2. **WiFi drop-outs.** The FeatherS3 twice stayed "WiFi connected" (RSSI about -75) while not answering ARP, with both firmware versions. A reset recovered it both times. Long network tests need a reachability check, and the board's WiFi power-save setting is worth checking.
3. **Command-frame request strings.** These include `cmdName=...` in the query string. This is existing behaviour of the frame-to-URL conversion and only shows in the `req` echo.

### 13.10 Slice 2 implemented and tested (2026-10-14, RaftWebServer branch `feature/deferred-responses`)

**Implementation:**
- **`RaftWebServerRestEndpoint`** gains two optional hooks: `restApiDeferAllocToken` and `restApiDeferBind`.
  `WebServer::restAPIMatchEndpoint` fills them from RaftCore's deferred-response registry.
- **`RaftWebResponderRestAPI`:**
  - **GET requests:** the handler is called from `loop()` (before any headers are sent), with deferral allowed.
  - **While a response is pending:** neither the headers nor `responseAvailable()` are offered, and receiving
    continues so a client disconnect is still detected.
  - **On completion:** the headers (with the real `Content-Length`) and the body are sent.
  - **Completion lookup:** by token over a list of live responders. A responder deleted on disconnect simply
    misses, so the late completion is a no-op.
  - **POST and multipart:** unchanged (not deferred).

**Testing:**
- **Integration** (`deftest.js`): 20/20 checks pass. The six HTTP checks are:
  - reads are deferred;
  - bus errors are returned;
  - a timeout gives HTTP 200 with `{"rslt":"fail","error":"timeout"}`;
  - 6 concurrent requests all complete;
  - a client disconnecting while pending is harmless;
  - `Content-Length` matches the body.
- **Regression of other HTTP paths:**
  - multipart `fileupload` with read-back matches;
  - `filedelete` works;
  - OTA `espFwUpdate` (POST multipart, 1.3 MB) works;
  - a large GET (`filelist`) works.
- **Connection limit (not caused by deferral):** both `main` and slice 2 reset some connections at 10 or more
  concurrent HTTP connections, including for plain `v`. This is a follow-up item.

**Benchmark** (interleaved, 2 runs per build, ping 9–16 ms; p50 of per-1 s windows):

| Measure | `main` | Slice 2 | Control |
|---|---|---|---|
| HTTP `cmdraw`: WebServer worst pass | 18.6–18.9 ms | 9.0–9.9 ms | 7.7–8.8 ms (HTTP `v`) |
| HTTP `cmdraw`: main-loop max | 21.9–22.0 ms | 12.2–12.6 ms | 10.3–10.9 ms |
| WS `cmdraw`: CommsMan worst pass | 14.8–15.8 ms | 5.8–5.9 ms | 3.1–3.5 ms |
| WS `cmdraw`: main-loop max | 17.6–18.6 ms | 8.7 ms | 7.1–8.3 ms |
| NACK reads: CommsMan worst pass | 26.5–41.7 ms (max 75) | 5.8–6.0 ms | — |
| NACK reads: main-loop max | 27.5–44.5 ms (max 88) | 17–23 ms (the DevMan NACK stall, §13.9) | — |
| Error-path reads timing out | 7/40 | 0/40 | — |

Client-side latency is unchanged: it is dominated by the network and by raftjs pacing.

### 13.11 Slice 3 implemented and tested (2026-10-14, RaftCore and RaftSysMods branches `feature/deferred-responses`)

**Implementation:**
- **RaftCore helper:** `RestAPIEndpointManager::handleApiRequestDeferrable(reqStr, retStr, sourceInfo, deliveryFn)`.
  - It allocates a token, allows deferral and calls the handler.
  - If the handler defers, the helper binds `deliveryFn` and returns `RAFT_RESP_DEFERRED`. Otherwise the
    response is in `retStr` as before.
  - Callers that have no request identity of their own use this helper, so binding is not repeated at each
    call site.
- **`SerialConsole`:** a deferred response is printed (with the line ending) when it completes. The
  response includes `"req"`, so a reply printed later can still be matched to its command.
- **`LoggerRaftRemote`** (remote-control TCP socket):
  - A deferred response is sent only if the client that made the request is still connected. A connection
    counter, incremented on each accept, is captured with the request.
  - Loggers are never deleted at runtime (`LoggerCore::clearLoggers()` only resets the count), so capturing
    `this` is safe.
- **`CommandFile`:** commands may be deferred, and the response is discarded as before. A `.api` file
  therefore never blocks on a bus read.
- **Not changed:** the MQTT `RAWCMDFRAME` path in `ProtocolExchange` is still not deferrable. It falls back
  to the blocking `cmdraw` path.

**Testing** (FeatherS3, test SysType with `LogManager`/RaftRemote on port 8080 and `CommandFile` enabled):
- **Serial console** (typed commands):
  - a read returns `rdData` 68;
  - an empty address returns `BUS_ACK_ERROR`;
  - `timeoutMs=1` returns `timeout`;
  - a write-only command and `v` respond immediately;
  - two pipelined reads (MPU6050 and APDS-9960 ID `ab`) each get their own response.

  The registry counters show 5 deferred and 1 timeout.
- **Remote TCP socket:**
  - read, bus error (`BUS_SW_TIME_OUT` for the empty address) and timeout all work;
  - a client that disconnects with a request in flight, and then a fresh connection, both work;
  - the device stays healthy.
- **CommandFile:** a `.api` file with a 3-repeat read, an APDS-9960 read and a NACK read gives 5 deferred and
  5 completed, with no timeouts.
- **Regression:** `deftest.js` passes 20/20.
- **Benchmark** (`stall.js`): CommsMan worst pass during WS `cmdraw` is about 6–7 ms, in line with slice 2.
  The NACK main-loop max of about 27 ms is the known DevMan stall (§13.9). HTTP figures vary with WiFi
  (HTTP `v` control 9–12 ms in this run).

### 13.12 Slice 4: `cmdjson` on the deferral mechanism (2026-10-14, RaftCore and RaftI2C branches `feature/deferred-responses`)

**Before:** `cmdjson` for a bus device called `sendCmdToDevice()`, which on the main task only queued the
request. The response `ok` meant "queued". Data read with `numToRd` was dropped, and bus errors were never
reported.

**Implementation:**
- **RaftCore `RaftBusDevicesIF::sendCmdToDeviceAsync(deviceID, cmdJSON, resultFn)`:** a new virtual function.
  It returns `RAFT_OK` if the command is queued, and `resultFn` is then called with the `BusRequestResult` on
  the main task. The default returns `RAFT_NOT_IMPLEMENTED`, so other buses are unaffected and fall back to
  the old path.
- **RaftI2C `DeviceIdentMgr`:**
  - Implements `sendCmdToDeviceAsync()` via the bus worker queue (the same queue as `cmdraw`). It returns
    `RAFT_BUSY` if the queue is full.
  - The `hexWr`/`numToRd` parsing is shared with `sendCmdToDevice()` (`cmdJSONToHWElemReq()`).
  - `sendCmdToDevice()` itself is unchanged; Axiom `DevicePower` calls it from its own task.
- **`DeviceManager::apiDevManCmdJson`** takes the `APISourceInfo`. A body that addresses a bus device (not
  a static device) and has `numToRd` is deferred when the transport allows it. `timeoutMs` comes from the
  request, or from the body.
- **Shared response format:** `formBusCmdResponse()` builds the response (`rdData` or the bus error) for both
  `cmdraw` and `cmdjson`.
- **Unchanged:**
  - static devices (e.g. MotorControl) still handle `cmdjson` themselves;
  - non-deferring transports keep the old behaviour.
- **Writes are deferred too** (decision 2026-10-14), for both `cmdraw` and `cmdjson`:
  - `ok` now means the write completed on the bus, and a NACK reports the bus error. Before, `ok` only meant
    the write was queued.
  - The main loop still never waits; only the reply to the client comes after the bus transaction.
  - `rdData` is included only when data was read.

**Testing:**
- **HTTP, serial console and WebSocket:**
  - reads return `rdData` (MPU6050 `68`, APDS-9960 `ab`);
  - a NACK returns `BUS_ACK_ERROR`;
  - `timeoutMs=1`, in the body or in the URL, returns `timeout`;
  - a write returns `ok`, and a write to an empty address returns `BUS_ACK_ERROR` (or `BUS_SW_TIME_OUT`);
  - a device off the bus or missing returns `failNoDeviceSpecified`.
- **`deftest.js`:** T16 and T17 were added, and T8 now checks writes (`ok`, and a bus error for an empty address).
  All 23 checks pass in repeated runs.
- **Test robustness fixes:**
  - **T6** (duplicate msgNum) now sends the duplicate in the same WebSocket message. As two separate
    messages, WiFi could delay the copy until after the read completed, when it is correctly handled as a
    new request.
  - **T2** accepts any `BUS_` error for NACK reads. The I2C driver sometimes reports `BUS_SW_TIME_OUT`.

### 13.13 The "NACK stall": present devices dropping offline after a NACKed write (2026-10-14, RaftI2C)

**Symptom:**
- During `cmdraw` reads of an absent address, the main loop took about 25–40 ms per pass, with peaks to 67 ms.
  This was attributed to DevMan in §13.9.
- The present devices also briefly dropped out of `listdevs`.

**Investigation:**
- **The stall only appears when no host is reading the USB serial console.** With `raft m` attached, the NACK
  phase measured about 10 ms, the same as normal reads. Without a reader, each log line blocks the logging task.
- **The logging came from devices going offline and online.** In one capture the MPU6050 was re-identified 21
  times, with RaftROS publisher detach and attach each time. These status callbacks log on the main task.
- **Why the devices went offline:** every transaction straight after a NACKed write failed with
  `BUS_ACK_ERROR`, whatever its address (59 out of 59 in one capture). Polls of the MPU6050 and APDS-9960 were
  among them. Three failed polls in a row take a device offline.
- **Which NACKs cause it:** only a write with data bytes queued after the address, as in `hexWr`, with or
  without a read. For such a write the ESP32-S3 I2C engine stops after the address NACK. Only the START command
  is marked done, the STOP is never executed, and the data is left in the TX FIFO.
- **Which NACKs don't:** address-only probes (the scanner) and read-only accesses. Their commands, including
  the STOP, all complete.
- **What did not help:**
  - resetting the state machine (`fsm_rst`);
  - a full `reinitI2CModule()`;
  - FIFO resets, which already happen on every access (`txfifo_cnt` is 0 after them);
  - a STOP-only command sequence, which the engine does not execute.
- **What did help:** a completed START…STOP sequence on the bus restores it.
- **Earlier test runs hid the problem:** when a scanner probe happened to run between the NACK and the next
  poll, that probe took the failure without anyone noticing.

**Fix (`RaftI2CCentral::access`):**
- If an access fails without executing all of its commands, it is followed at once by an address-only probe
  of the same address. All of the probe's commands, including the STOP, are executed, and its result is ignored.
  This costs about 150 µs, and only after such a failure. A `busResync` count is added to the I2C stats.
- An access whose ISR saw a NACK, but which ended by software time-out, now returns `BUS_ACK_ERROR` instead of
  `BUS_SW_TIME_OUT`.

**Results** (FeatherS3, nothing reading the console):

| Measure | Before | After |
|---|---|---|
| Device offline/online cycles per 100 NACKed `cmdraw` reads | 7–9 | 0 (also 0 for 100 write-only NACKs) |
| Main-loop max during NACK reads (p50 per 1 s window) | 27–38 ms (peaks 38–67 ms) | 9–10 ms (normal reads: 9 ms) |
| NACKs reported as `BUS_SW_TIME_OUT` | 1–3 per 100 | 0 |

**New check:** `deftest.js` T18 sends 40 NACKed writes and checks that both present devices are listed after
each one. Without the fix it fails, with a device missing 4 and 11 times out of 40; with the fix it passes. All
24 checks pass.

**Follow-ups:**
1. **Logging can block the main task.** Logging on the main task blocks while no host reads the USB-Serial-JTAG
   console. Any burst of logging (device churn, errors) then stalls the main loop. Consider a non-blocking or
   drop-on-full console when no host is connected, or keeping per-event logs off the main task.
2. **The ESP-IDF I2C central** (`RaftI2CCentral_ESPIDF`, used when `I2C_USE_ESP_IDF` is defined) was not checked
   for the same issue.
3. **Exact line behaviour:** what the bus does after the aborted write (no STOP) has not been confirmed with a
   logic analyser. The fix is based on observed behaviour.

### 13.14 Console logging stalls the calling task when no host is reading (2026-10-14, RaftCore and RaftSysMods)

**Problem:**
- Console output goes straight to the USB-Serial-JTAG driver's TX ring buffer, with a 10 ms write timeout.
  The ring buffer is a byte buffer, and a write waits until the whole line fits.
- With no host reading the port, the buffer fills and stays full. Every log line then blocks its caller (often
  the main task) for the full 10 ms and is dropped.
- The rest of a message was already dropped after one timeout, but the next message waited again. So a burst of
  N lines cost about N × 10 ms. This is how the device churn in §13.13 turned into 25–67 ms main-loop stalls.
- The same write code was duplicated in RaftCore (`LoggerCore`, Raft `LOG_x`) and in RaftSysMods
  (SerialConsole's `esp_log` hook, `ESP_LOGx`).

**Measured on the FeatherS3**, with a temporary endpoint logging 20 lines on the main task and no reader:
- 10 lines of 80 bytes took 100 ms just after boot;
- 400-byte lines took about 10.1 ms each.

**Fix:**
- **"Not draining" state in `LoggerCore`:** when a console write times out, later writes are made with no wait
  and dropped if there isn't space. The first write that succeeds clears the state, so a host that starts
  reading again gets back-pressure as before.
- **One write path:** SerialConsole's `esp_log` hook now calls `LoggerCore::writeToConsole()`, which is public
  and static, so both paths share this handling.

**Results:**

| Case | Before | After |
|---|---|---|
| No reader, 400-byte lines | ~10.1 ms per line | ~0.25 ms per line (worst line 0.66 ms) |
| Reader closes, then a 200-line burst | ~10 ms per line | one 10.5 ms wait (when the buffer fills), then ~0.13 ms per line |
| Reader attached, 1000 × 200-byte lines | — | all 1000 received complete, 0.24 ms per line |
| Reader attached after a period without one | — | all lines received from the first one |

**Regression:** `deftest.js` passes 24/24, and the stall benchmark is unchanged.

**Notes:**
- **One wait per transition:** the switch to "not draining" still costs one 10 ms wait when the buffer first
  fills.
- **Partly-full buffer:** a short line that fits into the space left in a nearly full buffer succeeds, clears
  the state, and the next longer line can then wait once more. The free space only shrinks while nothing is
  reading, so this happens a bounded number of times.
- **SerialConsole's own writes** (`putStr` and response frames) already use a 1-tick timeout and are unchanged.
