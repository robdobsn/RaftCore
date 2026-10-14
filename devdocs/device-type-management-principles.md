# Device Type Management: Principles, Rationale and Current Practice

Status: draft for discussion, 2026-10-14.

## 1. Purpose and scope

This document sets out the principles behind the current mechanism for:

- managing device type records;
- scanning a bus;
- identifying devices;
- polling devices and publishing their data;
- labelling individual devices;
- allocating bus addresses for re-addressable devices (RSAO).

Each principle has four parts:

- **Principle:** what the mechanism is trying to guarantee.
- **Rationale:** why.
- **Current practice:** how the code does it today, and where it falls short of the principle.
- **To challenge:** questions to ask when we look for a better mechanism.

This document only describes and frames. It makes no proposals. For the defect-level critique and the
proposed fixes, see the companion document `device-identification-review.md`. Some findings here are
taken from that review, and those are marked *(review)*.

**Code examined:**

- RaftCore: `components/core/DeviceTypes/`, `components/core/DeviceManager/`, `components/core/Bus/`,
  `scripts/ProcessDevTypeJsonToC.py`, `devtypes/DeviceTypeRecords.json`.
- RaftI2C: `components/RaftI2C/BusI2C/`. The main classes are BusScanner, DeviceIdentMgr,
  DevicePollingMgr, BusStatusMgr and BusMultiplexers.
- RoboticalAxiom1: `components/RoboticalRSAO/`, `AxiomDevTypes/`.
- raftjs: `src/RaftDeviceManager.ts`.

The terms used throughout are defined in section 2.

---

## 2. Terminology

The code, the wire formats, the REST API and raftjs use several names for each of the core concepts, and
in a few places one name is used for two concepts. This section picks one **canonical term** for each
concept, lists the spellings in use today, and notes the problems. The rest of this document uses only
the canonical terms.

### 2.1 Type-level concepts (one per kind of device)

| Concept | Canonical term | Spellings in use today | Notes |
|---|---|---|---|
| The description of one kind of device | **device type record** | `DeviceTypeRecord`, `DeviceTypeRecordDynamic`, "devType", "type record" | **Base record**: compiled from JSON at build time. **Extended record**: added at runtime. **Catalogue**: the full set of records. |
| The record's unique machine name, e.g. `RoboticalLightSensor` | **device type name** | JSON key in `devTypes`; `deviceType` field; `DeviceTypeRecord::deviceType`; `deviceTypeName`; REST `type=`; static devices' `type` config (`RaftDevice::getConfiguredDeviceType()`); RaftROS `deviceTypeName` | The JSON key always equals `deviceType`. **`devInfoJson.type` is meant to repeat it but differs in 2 records**: AdafruitGamepad has `"Gamepad"` and RoboticalThermocoupleAmp has `"Thermocouple"`. `devman/typeinfo` returns only `devInfoJson`, so a client never sees the real device type name. |
| The human-readable name of the kind of device, e.g. "Robotical Light Sensor" | **type display name** | `devInfoJson.name`; raftjs `DeviceTypeInfo.name` | raftjs overwrites `DeviceTypeInfo.name` with the device name, so in raftjs the same field holds either the type display name or the device name. |
| The compact integer that names a record in published data | **device type index** | C++ `DeviceTypeIndexType`, `deviceTypeIndex` (most common), `deviceTypeIdx`, `devTypeIdx`, `typeIdx`; devjson `_i`; devbin header `devTypeIdx`; REST `dtIdx`; REST `type=<number>` | Positional: base records in JSON merge order, then extended records in the order added. Valid only for one firmware build and one boot. Invalid value: `DEVICE_TYPE_INDEX_INVALID` (0xFFFF). Recommended spelling: `deviceTypeIndex` in code; keep `_i` and `dtIdx` on the wire. |
| A type identifier reported by the device itself (RSAO WHOAMI) | **hardware type ID** | `deviceTypeId` (Axiom JSON field, `RSAOTypeRegistry`, `RSAOIdentHandler`) | Assigned by the vendor and persistent, unlike the device type index. **The spelling `deviceTypeId` is one letter away from `deviceTypeIdx` and means something quite different.** Renaming it, for example to `hwTypeId`, would remove the hazard. |
| Functional categories, e.g. `LGHT`, `ACCEL` | **device classes** | `devInfoJson.clas` | "Class" is also used for the C++ class that implements a static device (`RaftDevice` class name, `DeviceFactory`). Call that the **device driver class** to keep the two apart. |

### 2.2 Instance-level concepts (one per device actually present)

| Concept | Canonical term | Spellings in use today | Notes |
|---|---|---|---|
| A bus, as a number | **bus number** | `BusNumType`, `busNum`; devjson top-level key; devbin status-byte bits 0–3 | 0 means directly connected, i.e. a static device not on a bus. Buses are numbered from 1 in the order they appear in the bus configuration, so a number changes if the configuration is reordered. |
| A bus, as a name | **bus name** | `busName`, REST `bus=` | Set in configuration. REST accepts either a bus name or a bus number for `bus=`. raftjs puts the **bus number** in a field called `busName`. |
| A device's address on its bus | **element address** | `BusElemAddrType`, `address`, `addr`, `addrAndSlot`, `BusI2CAddrAndSlot` | Its form depends on the bus type. For I2C it is `(slot << 8) \| i2cAddr`. Slot 0 is the main bus and slot N is mux channel N−1 counted across all muxes. The string form is lowercase hex with no `0x`, e.g. `325` for slot 3, address 0x25. |
| The unique identifier of a device that is present | **device ID** | C++ `RaftDeviceID {busNum, address}`, `deviceID`/`deviceId`; REST `deviceid=`; raftjs `deviceKey`; string `"<busNum>_<elementAddrHex>"`, e.g. `1_325` | **The device ID is the only instance identifier.** Other forms resolve to it: REST `bus=<name>&addr=`, `registerForDeviceData("<busName>_<addr>@<slot>")` (which drops the slot), and REST `device=<device name>`. A static device's device ID is `0_<index in the static device list>`. |
| An identity reported by the device itself (e.g. an RSAO serial number) | **hardware instance ID** | RSAO `serial`, `sn`; only inside the RSAO code | **Not part of the device ID.** Raft has no way to link a device across a change of address or port. |
| A user-assigned label for a device | **device name** | DeviceManager `_deviceIDToName`/`_deviceNameToID`, `lookupDeviceName()`, `RaftDevice::getConfiguredDeviceName()`; config `DevMan/DeviceNames` and `DevMan/Devices[].name`; REST `devman/setname`; `name` at the top level of `devman/typeinfo` and `devman/listdevs` responses | Keyed by device ID. One-to-one: each device ID has at most one name and each name belongs to at most one device. Not persisted. **There is no default.** A device with no configured name has no device name. |
| The category of a device (`normal`, `system`) | **device role** | `role` | Keyed by device ID. Defaults to `normal`. |
| The label a user sees for a device | **display name** | *(not defined anywhere)* | See 2.3. |

These must not be confused with the **system friendly name** (`SysManager` `friendlyName`, raftjs
`RaftFriendlyName`). That names the whole Raft unit, not a device on it.

### 2.3 How the name a user sees is derived today

No component defines a display name. What a user actually sees comes from raftjs, as follows:

1. The firmware publishes data with a device ID and a device type index. No name is included.
2. raftjs calls `devman/typeinfo?deviceid=…`. The response holds `devinfo`, whose `name` is the type
   display name, plus a top-level `name`, which is the device name if one is configured.
3. raftjs merges the two: the device name, if present, **overwrites** `devinfo.name`. The result is
   cached as `DeviceTypeInfo` (`RaftDeviceManager.ts` ~1025).
4. raftjs copies that value into `DeviceState.deviceType` (`RaftDeviceManager.ts` ~485, ~509).
5. UIs show `deviceTypeInfo.name`, often next to the device ID (for example "Device {name} Address
   {deviceKey}").

So in practice: **display name = device name if one is configured, otherwise the type display name.**
The problems with this:

- **No instance disambiguation.** Two devices of the same type with no device name both show the same
  type display name. Only the device ID tells them apart.
- **Overloaded fields.** In raftjs, `DeviceTypeInfo.name` and `DeviceState.deviceType` each hold either a
  type-level or an instance-level value depending on configuration. `deviceType` never holds the device
  type name.
- **Stale names.** Because the merged object is cached and never invalidated, a later `devman/setname` is
  not seen until raftjs is reloaded.
- **No firmware rule.** Firmware consumers (RaftROS topics, the data logger) have to invent their own
  naming rule.

### 2.4 Consistency rules adopted in this document

1. "Type" alone is not used. Each use names the concept: device type name, type display name, device
   type index, hardware type ID or device classes.
2. "Index" always means the session-scoped device type index. "ID" always means something that is
   stable across sessions: device ID (stable while the wiring and bus configuration stay the same),
   hardware type ID or hardware instance ID.
3. "Address" alone means the element address. Where only the raw bus address is meant, it is called the
   I2C address.
4. "Name" alone is not used. Each use says device type name, type display name, device name, display
   name, bus name or system friendly name.

### 2.5 Other terms

| Term | Meaning |
|---|---|
| Slot | A multiplexer channel. Slot 0 is the main bus. |
| RSAO | Robotical Standard Add-On. Starts at a default address (0x0F), is identified by probing, and can be moved to another address. |
| Ident hook | The handler registered with `registerNewDeviceIdentHandler()`. It runs when no record matched by address. |

---

## 3. The principles at a glance

| # | Principle | Original point |
|---|---|---|
| P1 | A device type record is a complete, self-contained description of a kind of device | 1 |
| P2 | Firmware ships with a catalogue of records (possibly empty), and that catalogue drives scanning, identification, polling and publishing | 2 |
| P3 | The catalogue is cheap in memory and compute, and can be extended at runtime | 3 |
| P4 | The core is device-agnostic. Device-specific behaviour is data, with a narrow code hook for exceptions | new |
| P5 | Identification is address-scoped, non-destructive first, and first-match-wins | new |
| P6 | Scanning is prioritised, multiplexer-aware and bounded in bus time | 4 |
| P7 | A device's presence is judged with hysteresis, and polling replaces scanning once a device is known | new |
| P8 | Published data carries a device type index that is valid for the session. Indices are append-only | 5 |
| P9 | A device ID identifies a device by its location (bus number, element address). Device name, device role and settings attach to the device ID, and people see a display name | 6 |
| P10 | Re-addressable devices get addresses from a pool that avoids every address that is reserved or in use | 7 |
| P11 | Polling is scheduled per device at its own interval, as efficiently as possible | 8 |
| P12 | Data is captured raw with a timestamp, and decoded with metadata from the record, on or off the device | new |
| P13 | Data integrity comes before data availability: a sample that cannot be validated is dropped | new |
| P14 | One task owns each bus, and all bus work is serialised and time-sliced on that task | new |
| P15 | Per-board configuration can tune the catalogue's defaults without changing records | new |

---

## 4. Principles

### P1. A device type record is a complete, self-contained description of a kind of device

**Principle.** A record holds everything needed to:

- name the kind of device, with a device type name for machines and a type display name for people;
- identify it by talking to it over a bus;
- initialise it;
- poll it at a defined rate;
- decode its data;
- tell a subscriber the data format;
- change its sample and poll rate where the device supports this;
- send it further commands (actions), for example to actuators such as servos.

**Rationale.**

- Adding support for a device is a data change, not a code change.
- One artefact serves the firmware, the generated decoders, the web UI (raftjs), ROS bridges and
  MicroPython bindings, so they cannot drift apart.

**Current practice.**

- **Fields.** `DeviceTypeRecord` (`DeviceTypeRecord.h`) is a POD holding:
  - `deviceType`, the device type name;
  - `addresses`, a CSV and/or ranges;
  - `detectionValues`;
  - `initValues`;
  - `pollInfo`, a JSON object;
  - `pollDataSizeBytes`;
  - `devInfoJson`;
  - a generated `pollResultDecodeFn`;
  - `AttrFieldDesc` tables for access by field name.
- **Detection, init and poll grammar.** These use a compact string grammar of `write=read` pairs joined
  by `&`:
  - `0x..` gives write bytes;
  - `rN` reads N bytes;
  - `0x..` or `0b..x..` gives an expected value, where `x` is a don't-care bit;
  - `,` separates alternative values;
  - `pN` pauses N ms;
  - `r{expr:max}` sets a read length from an earlier result.
- **`pollInfo` fields:**
  - `c`, the poll requests;
  - `i`, the interval in ms;
  - `s`, the number of results to store;
  - optional `h`/`busHz` and `hSlots`, a bus speed for this device;
  - optional `crc`, an integrity trailer;
  - optional `ext`, for data pushed in by other code instead of polled.
- **`devInfoJson` fields:**
  - `name` (the type display name), `desc`, `manu`, `type` (meant to repeat the device type name, see
    2.1), and `clas` (the device classes);
  - `resp`, the response layout: `b` bytes, plus `a[]` attributes with type, range, format,
    divisor/addend and units;
  - `actions[]`. Actions named `_conf.*` with a `map` carry the sample-rate configuration
    (`devman/devconfig?sampleRateHz=`).
- **Coverage.** The RaftCore catalogue has 49 records, 22 of which declare actions.
- **Gaps against the principle.**
  - The record is I2C-shaped: single-byte addresses, the 0x00–0x7F index map, and register-style
    write/read pairs.
  - The record is not self-sufficient for identification in every case:
    - A record with no `addresses` (all RSAO records) can never be matched by the record alone. It
      relies on the ident hook.
    - A record with no detection values matches any device that responds at its address (P5).
    - A record with `pollInfo.ext` is never polled; other code supplies its data.
  - The hardware type ID (RSAO WHOAMI) is not in the record that Raft consumes. `deviceTypeId` exists in
    the Axiom JSON but nothing reads it. A hand-written table in `RSAOTypeRegistry.cpp` holds it instead.
  - The record does not contain its own device type index. The index is assigned from the record's
    position (P8), so the same record can have a different index in a different build.
  - The device type name is stored twice (`deviceType` and `devInfoJson.type`), and the two copies
    disagree in 2 records. Clients receive only the second copy.

**To challenge.**

- Is one string grammar the right encoding? It has four separate parsers, three in C++ and one in Python
  *(review)*.
- Should bus-type-specific parts (addressing, detection transport) be separate from bus-neutral parts
  (decode, actions, metadata)?
- Should identity data such as the hardware type ID be a first-class record field?
- Should the device type name be stored once, and returned in `devman/typeinfo`?

---

### P2. Firmware ships with a catalogue of records, and that catalogue drives everything

**Principle.**

- Firmware with auto-identification includes a standard set of records. The set may be empty.
- From that set alone, the firmware can work out how to scan the bus, identify devices, poll them,
  publish their data, and serve decode metadata to subscribers.

**Rationale.**

- Plug-and-play without network access or a companion app.
- Behaviour is fixed at build time and can be tested.
- Each product can choose which JSON files to include.

**Current practice.**

- **Build-time generation.** `ProcessDevTypeJsonToC.py` merges the JSON files listed in
  `DEV_TYPE_JSON_FILES` using `dict.update`. A duplicate key in a later file replaces the earlier record
  but keeps the earlier position. The script emits:
  - `baseDevTypeRecords[]`;
  - a per-address index (`baseDevTypeCountByAddr[]` and `baseDevTypeIndexByAddr[]`, covering
    0x00–0x77);
  - three scan-priority lists;
  - decode functions and field descriptors;
  - optionally, MicroPython bindings.
- **Build checks.** The script checks `resp.b` against the size computed from `pollInfo.c`. It does not
  validate detection or init strings *(review)*.
- **Each product picks its own JSON.** For example, Axiom018 uses `AxiomDevTypes/DeviceTypeRecords.json`
  plus `RSAOTypeRecords.json`, not the RaftCore file. The RaftCore file still holds three old Robotical
  records (Servo, WaterPump, LightSensor) that are identified by address in the old way.

**To challenge.**

- Should the catalogue be one image that can be swapped (for example a flash partition or file) instead
  of compiled C, so that it can be updated without a firmware build?
- Is merging by JSON order acceptable when that order also fixes type indices (P8) and identification
  precedence (P5)?

---

### P3. The catalogue is cheap in memory and compute, and can be extended at runtime

**Principle.**

- The built-in list is held memory-efficiently: in flash, not RAM.
- Anything derived from it (address→type map, scan priorities, addresses to avoid) is computed cheaply,
  preferably at build time.
- Records can be **added**, **overridden** and **disabled** at runtime, but not **deleted** (see P8).
- If many records may be added this way, the derived structures must still be cheap to compute.

**Rationale.**

- These are small MCUs, often without PSRAM.
- Identification lookups happen on the bus task, in the scan loop.
- Shipped firmware must still be able to support new devices.

**Current practice.**

- **Memory.**
  - String content (`R"JSON(...)JSON"`) is in flash (rodata).
  - The `baseDevTypeRecords[]` array and the per-address index arrays are emitted as non-`const` `static`
    data, so they occupy DRAM.
  - The scan-priority lists are `const`.
- **Compute for base records.** All derived data is precomputed. Looking up an address is an O(1) array
  index.
- **Compute for extended records.**
  - `getDeviceTypeIdxsForAddr()` and `getScanPriorityLists()` re-parse each extended record's
    `addresses` CSV on every call. This is a linear scan, guarded by a mutex.
  - A set-once atomic flag skips the mutex when no records have been added.
- **Add.** `addExtendedDeviceTypeRecord()`.
  - At most 20 records (`MAX_EXTENDED_DEV_TYPE_RECORDS`), reserved up front so that `c_str()` pointers
    stay valid.
  - Append-only.
  - Deduplicated by device type name: re-adding a name returns the existing device type index and
    **discards the new content**.
  - The only caller on `main` is `DeviceManager::setupStaticDevices()`. There is no REST or file loader.
- **Override.**
  - Lookup by device type name searches extended records first, so an extended record with the same
    device type name shadows the base record for lookups by name.
  - The base record is still a candidate in lookups by address.
  - An existing extended record cannot be replaced.
  - **Consistency problem:** an override gives one device type name **two** device type indices. The
    base index is still valid, still matched by address and still published. So neither the device type
    name nor the device type index identifies "the current definition of this kind of device".
- **Disable.** Not on `main`. The device-library branches add name suppression *(review §7)*.
- **Decoders.** Extended records carry no field descriptors, and have a decode function only if C++ code
  supplies one. Data-only records therefore cannot be decoded on the device.
- **`excludeFromAddrMap`.** A flag read only at build time. Extended records ignore it.

**To challenge.**

- Is "never delete" really a property of the catalogue? Or is it only forced by positional device type
  indices (P8)? With an identifier that is stable across sessions, delete and reuse would be safe.
- Should override be a single explicit operation (replace by ID) rather than shadowing based on lookup
  order?
- If many records are added at runtime, should the address→type map and the scan lists be rebuilt
  incrementally into a merged index, rather than recomputed on every call?

---

### P4. The core is device-agnostic; device-specific behaviour is data, with a narrow code hook for exceptions

**Principle.**

- RaftCore and RaftI2C contain no knowledge of particular devices.
- Where data cannot express something, an application registers a handler through a small, generic
  interface.

**Rationale.**

- Library code can be reused across products.
- Device and vendor logic (for example RSAO commissioning) lives with the product that needs it.

**Current practice.**

- **Hooks** on `RaftBusDevicesIF`:
  - `registerNewDeviceIdentHandler()` returns a verdict of Handled, Deferred or NotMine.
  - `registerBusTaskServiceHandler()` runs application bus work on the bus task. While that work is busy,
    it holds off polling but not scanning.
- **After a Handled verdict**, the handler only sets `deviceTypeIndex`. The core then runs init and sets
  up polling and data aggregation from the standard record. Everything downstream is reused unchanged.
- **Leaks.** Some device-specific code remains in the core: an LSM6DS FIFO debug decode in
  `BusStatusMgr::handlePollResult()`, and FIFO word-count timing in `DevicePollingMgr`.

**To challenge.**

- Is one global ident hook per bus enough? Would a hook per record or per class be better, for example
  "RSAO-class records are claimed by handler X"?

---

### P5. Identification is address-scoped, non-destructive first, and first-match-wins

**Principle.**

1. When a new element appears at an address, only records that declare that address are candidates.
2. Each candidate's detection sequence is a read-and-compare that should not disturb the device.
3. The first candidate that matches wins.
4. Probes that could be destructive (vendor-framed commands) run only after every non-destructive option
   has failed.

**Rationale.**

- Limiting candidates by address keeps identification fast.
- Many third-party sensors misbehave if sent unexpected bytes. For example, Sensirion parts treat probe
  bytes as commands and stop measuring.

**Current practice** (`DeviceIdentMgr::identifyDevice()`).

- **When it runs:**
  - on an offline→online edge;
  - on a retry for elements that are online but unidentified and unpolled, throttled **across the whole
    bus** to one every 3 s (`BusScanner::updateBusElemState()`).
- **Candidate order:** extended records, then base records in JSON order. The slot is ignored.
- **On a match:** init runs (its result is ignored), poll info is built, and a new `PollDataAggregator`
  is created.
- **If nothing matches:** the ident hook runs. For RSAOs the hook probes `ROBOTICAL_ID`, then WHOAMI, then
  maps the hardware type ID to a device type name through `RSAOTypeRegistry`, then finds the record by
  that name. RSAO records have no addresses, so they
  never match by address.
- **Re-identification.** A device is never re-checked once identified, except when `DevicePollingMgr`
  clears its identity after repeated CRC failures (P13).
- **Known weaknesses** *(review)*:
  - Ordering silently shadows records. VEML6040 shadows MXC400xXC, and AHT20 shadows TrillRing.
  - A bus error during detection counts as "not this type".
  - A record with no detection value matches anything.

**To challenge.**

- Should precedence be explicit, for example a specificity rank or "must be unique at this address"
  checked at build time, rather than positional?
- Should identification be scheduled per element rather than through one bus-wide throttle?
- Should identity be re-verified periodically, or on suspicious events?

---

### P6. Scanning is prioritised, multiplexer-aware and bounded in bus time

**Principle.**

- Scanning should find connected devices quickly, especially at addresses where known devices live.
- It should use only a small, bounded share of bus time, so that polling and commands are not starved.
- Multiplexers multiply the address space. The scan must cover every slot without double-counting
  devices on the main bus.

**Rationale.**

- A device that is plugged in should appear within a time a human finds acceptable.
- A full sweep of 0x04–0x77 across up to 64 slots is expensive and mostly finds nothing.

**Current practice** (`BusScanner`).

**Phases.** Each phase repeats its sweep `ADDR_RESP_COUNT_FAIL_MAX_DEFAULT + 1` (= 4) times, then moves to
the next.

1. `MAIN_BUS_MUX_ONLY`: probe the multiplexer address range (default 0x70–0x77) on the main bus.
2. `MAIN_BUS`: every address 0x04–0x77 on the main bus, ignoring priority.
3. `SCAN_FAST`: priority-ordered, all slots, up to 10 ms of scanning without yielding per worker pass
   (`fastScanMaxUnyieldMs`).
4. `SCAN_SLOW`: steady state.
   - Scans at most every `busScanPeriodMs` (default 5 ms).
   - Up to 2 ms of scanning per pass (`slowScanMaxUnyieldMs`).
   - The share of bus time is bounded by this time slice, not by an explicit bandwidth target.

Phase triggers:

- Any change in mux state (a mux appears or disappears) restarts from phase 1.
- `requestScan(…, fast)` jumps to `SCAN_FAST`.

**Priority lists** (generated at build time from each record's `scanPriority`):

- **List 0:** the *first* address of every record marked `high`, plus extended records' addresses and the
  config `scanBoost` addresses.
- **List 1:** the other addresses of `high` records, plus records marked `medium`.
- **List 2:** every remaining address in 0x04–0x77.

In practice:

- Of the 49 RaftCore records, 41 are `high` and 8 declare no priority, so the scheme works as two tiers.
- Records with `excludeFromAddrMap`, and records without addresses (all RSAO records), add nothing. Their
  addresses fall into list 2.
- Lists are visited with weights 1, 3 and 9 (`SCAN_PRIORITY_COUNTS`), so roughly 9:3:1. A list is visited
  each time its counter reaches its weight.
- Each visit sweeps the list on one slot. Each list keeps its own slot cursor. The main bus takes two
  cursor positions (0 and 1), so it is scanned twice as often as each mux slot.

**Skip rules:**

- An address already found on the main bus is not probed on slots, because main-bus devices answer on
  every slot. The "found on main bus" bits are sticky and never cleared.
- A mux address is probed only on the slot where that mux sits.
- Configured IO expander addresses are skipped.
- Devices that are polled and online are skipped. Polling detects when they go offline (P7).

**Extended records** are added to list 0, so runtime additions get the highest scan priority.

**To challenge.**

- Should the scan budget be a target share of bus time (or of probes per second), adapted to polling
  load, instead of a fixed number of ms per pass?
- Should slot coverage itself be prioritised, for example by recently-active slots or slots with known
  devices?
- With many runtime records, list 0 grows without limit. Should priority be computed rather than
  appended?
- Should the sticky "found on main bus" bits ever be cleared, for example when a device moves from the
  main bus to a slot?

---

### P7. A device's presence is judged with hysteresis, and polling replaces scanning once a device is known

**Principle.**

- An element is marked online or offline only after consistent evidence.
- Once a device is identified and being polled, the poll result itself is the presence signal.

**Rationale.**

- I2C transfers sometimes fail. Without hysteresis, devices would flap online and offline.
- Probing a device that is already being polled wastes bus time.

**Current practice.**

- **States** (`BusAddrStatus`, `DeviceStatus`): INITIAL → ONLINE after 2 OK responses → OFFLINE after 3
  failures.
- **RaftI2C behaviour.** It moves OFFLINE straight to PENDING_DELETION. The record is erased and a
  one-shot deletion notice is queued, with a maximum of 20.
- **On reappearance.** The element is a fresh record and is identified again, possibly as a different
  type.
- **Limits.** `BusStatusMgr` holds at most 50 elements (`ADDR_STATUS_MAX`). Overflow is silent
  *(review)*.

**To challenge.**

- Should identity and per-device settings survive a short absence? At present, a glitch on a cable
  discards settings made with devconfig (P9).
- Should "absent" and "deleted" be separate, as the subscriber protocol document describes, or merged,
  as the code does?

---

### P8. Published data carries a device type index that is valid for the session; indices are append-only

**Principle.**

- Each published device sample carries its device type index.
- A subscriber resolves the device type index to decode metadata once, then caches it.
- Within a session, a device type index must never be reused for a different record, so records can be
  added but not deleted.

**Rationale.**

- Bandwidth: a `uint16_t` on every devbin record instead of a device type name string.
- Consistency between devjson and devbin.

**Current practice.**

- **Assignment:**
  - base record: its position in the merged JSON;
  - extended record: `BASE_DEV_TYPE_ARRAY_SIZE` + its position in the order added at runtime.
- **Where it is published:**
  - devjson: `"_i"` in each device entry;
  - devbin: a `uint16` BE `devTypeIdx` in each record header.
- **Getting metadata:**
  - `devman/typeinfo?type=<device type index | device type name>`;
  - `devman/typeinfo?deviceid=<device ID>`, which also returns the device name and device role;
  - `devman/listdevs`, which lists `dtIdx` for each online, identified device.

  Neither form returns the device type name itself, only `devInfoJson` (see 2.1).
- **raftjs cache.**
  - Keyed by `bustype:<bus number>:<device type index>` and by device ID.
  - **Never invalidated**: not on reconnect, not when `_i` changes at a device ID, and not when a device is
    removed.
- **How long a device type index is valid.**
  - It is valid only within one firmware build and one boot.
  - It changes if the JSON order or the set of included files changes, and with the order of runtime
    additions.
  - Firmware does not publish any generation or version marker, so a client cannot tell when its cache is
    stale.
- **Consistency problems with P3:**
  - An override gives one device type name two device type indices.
  - Re-adding a device type name returns the old device type index with the old content.

  So the principle "a subscriber resolves the index once" holds only because records are never mutated
  in place.

**To challenge.**

- Would an identifier stable across sessions be better than a positional index, given P3's "no delete"
  rule exists only to protect indices? Options include a hash of the device type name, an assigned ID
  registry, or the hardware type ID where one exists.
- Alternatively, keep compact session indices but add a **type-table generation** to the publish
  envelope or the typeinfo response, so that caches can be invalidated *(review)*.
- Should a subscriber be able to fetch the whole type table, mapping device type index to device type
  name and metadata, at once?

---

### P9. A device ID identifies a device by its location; device name, role and settings attach to the device ID

**Principle.**

- Each device that is present has a device ID that is unique and stable, so that several devices of the
  same type can be told apart by APIs and subscribers.
- People see each device under a display name that is meaningful and unambiguous.
- Sample rate, poll rate and other settings can be changed per device, not only per device type record.

**Rationale.**

- On a passive bus, location is the only identity guaranteed to exist. Most I2C sensors have no hardware
  instance ID.

**Current practice.**

- **Device ID** = `RaftDeviceID(bus number, element address)`, string `<busNum>_<elementAddrHex>`. For
  example `1_325` is bus 1, slot 3, I2C address 0x25. Static devices use bus number 0 and their index in
  the static device list.
- **Stability.** A device ID changes if the device moves to another slot or bus, if the bus configuration
  is reordered (bus numbers are positional), or if an RSAO is re-addressed. It does not include any
  hardware instance ID.
- **Device names and device roles** are sidecar maps in `DeviceManager`, keyed by device ID. Device names
  are one-to-one with device IDs.
  - Sources: the `DevMan/DeviceNames` config, `DevMan/Devices[]`, and the runtime `devman/setname` and
    `devman/setrole`.
  - Not persisted.
  - Because they are keyed by device ID, they re-attach when a device reappears at the same location.
- **Display name.** Not defined by firmware. raftjs derives it as the device name if one is configured,
  otherwise the type display name (2.3). Unnamed devices of the same type look identical apart from
  their device ID.
- **Per-device settings** go through `devman/devconfig`:
  - `intervalUs`, `numSamples`, `busHz`, `busHzSlots`;
  - `sampleRateHz`, which uses the record's `_conf.*` action `map`.

  These settings are keyed by element address inside `DeviceStatus`, not by device ID, so they are
  **lost** on deletion or re-identification. Device names survive the same events.
- **Actions** are packed by the client (raftjs `sendAction`) using the record's `actions[]`, and sent as
  `devman/cmdraw`. `devman/cmdjson` exists for devices that take JSON commands.
- **Gaps:**
  - A device name for a bus device does not resolve in all paths (`resolveDeviceIDAndBus`).
  - The `<busName>_<addr>@<slot>` form in `registerForDeviceData` drops the slot.

**To challenge.**

- Where a hardware instance ID exists (an RSAO serial), should it be the primary key, with the device ID
  secondary? This would let device names and settings follow an RSAO across re-addressing or a move to
  another port.
- Should firmware define the display name, including a default that tells apart unnamed devices of the
  same type (for example type display name plus slot or ordinal)? Firmware consumers such as RaftROS and
  the data logger could then use the same rule.
- Should device names and per-device settings share one store keyed by device ID, persist, and be
  re-applied when a device is identified?

---

### P10. Re-addressable devices get addresses from a pool that avoids every address that is reserved or in use

**Principle.**

- For RSAOs, and potentially other re-addressable classes, the firmware gives each device a unique
  address that does not clash with:
  - any address a known device type could occupy;
  - infrastructure (muxes, IO expanders);
  - any device currently present.
- The list of excluded addresses should be maintained and available externally.

**Rationale.**

- In a plug-and-play system, the absence of a device proves nothing. A static sensor may be plugged in
  later at its fixed address, so every address that any record declares must be reserved, not only
  those occupied now.
- Muxes must be excluded. An RSAO at a mux address would look like a mux and could not be found again.

**Current practice** (Axiom `RSAOSysMod`, `RSAOArbitrator`).

- **Configuration:** default address 0x0F; assignable range 0x10–0x6F; `addrExclude` (Axiom018 sets
  0x70–0x77 and 0x25); and the arbitration mode (off, all-but-one, or all).
- **Reserved set.** Built **once at setup** from two sources:
  - every address in the range for which `getDeviceTypeIdxsForAddr()` returns any record, so it is
    derived from the base records plus any extended records present at that moment;
  - `addrExclude`.

  The mux `minAddr`/`maxAddr` of this board is deliberately not used, because an RSAO keeps its address
  when moved to other hardware.
- **In-use set.** A live presence scan of the range on the current slot, at the start of each pass.
- **Choosing an address:**
  1. The address saved in NVS for this serial, if it is valid.
  2. Otherwise the next free address after the last one assigned (high-water mark, wrapping), so a
     just-freed address is not reused straight away.
- **Assignment.** A state machine runs on the bus task. In order: RESET_BOARD, then isolate one device
  (DELAY_I2C back-off), then SET_I2C_ADDRESS (guarded by serial), then confirm, then START_APP. It is
  triggered by commissioning (anything seen at 0x0F) or by collision detection (bad serial check byte or
  CRC).
- **Uniqueness scope:**
  - RSAO uniqueness is per slot.
  - Static-address avoidance is global, because an enabled slot shares the wire with the upstream bus.
- **Gaps against the principle:**
  - **The exclude list is not exposed.** It appears only in setup logs. `rsao/status` reports
    configuration and counts, but not the reserved set.
  - The reserved set is not rebuilt when records are added at runtime.
  - The RSAO id→type mapping exists in four places *(review)*.

**To challenge.**

- Should the reserved set be a first-class product of the catalogue (P3), computed and published by
  RaftCore, rather than derived by the application?
- Should "re-addressable" be a property of a device *class* in the record, so that Raft handles the
  generic parts (pool, exclusion, persistence, exposure) and the RSAO code supplies only the protocol?
- Should a re-addressed device keep its identity (serial) across the change of address (see P9)?

---

### P11. Polling is scheduled per device at its own interval, as efficiently as possible

**Principle.**

- Each identified device is polled at its required interval.
- The scheduler tries to reach every device on time and wastes no bus time.

**Rationale.**

- Polling is the main load on the bus. A single VCP at default rates uses about 50% of a 100 kHz bus.

**Current practice** (`DevicePollingMgr`, `BusStatusMgr::getPendingIdentPoll()`).

- **Selection.** Each worker pass polls **one** device: the first due device in `_addrStatus` order.
  This is first-due by position, not earliest-deadline, so devices later in the list can be starved
  *(review)*.
- **Timing.**
  - "Due" means `now − lastPollTime ≥ interval`, and `lastPollTime` is then set to `now`.
  - Lateness therefore builds up as drift. There is no catch-up and no phase locking.
- **Multi-step polls.** A step with a pause (`pN`) splits the poll. It resumes as a partial poll once the
  pause has passed.
- **Per poll:**
  - the slot is selected;
  - the bus speed is switched if the record declares `busHz`;
  - dynamic read lengths are evaluated (`r{…}`);
  - the CRC trailer is validated (P13);
  - the result is stored with a 2-byte timestamp (100 µs resolution, wrapping).
- **Hold-off.** Polling pauses while a bus-task handler is busy, for example during RSAO assignment.
- **Interval sources:** the record's `pollInfo.i`, then per-device `devconfig` overrides, then
  `sampleRateHz` maps.

**To challenge.**

- Should scheduling be earliest-deadline-first, with budget accounting? Each device's estimated
  transaction time is known from its record.
- Should admission control warn when the requested poll rates exceed bus capacity, as two VCPs at 25 ms
  do on a 100 kHz bus (~102%)?
- Should several due devices on the same slot be polled together, to save mux switches?

---

### P12. Data is captured raw with a timestamp and decoded with metadata from the record, on or off the device

**Principle.**

- The bus layer stores raw poll bytes plus a timestamp, unchanged.
- Decoding is done from the record's metadata, either on the device (generated decoder) or by the
  subscriber (`devInfoJson.resp`).

**Rationale.**

- Polling stays cheap and does not depend on the device type.
- Bandwidth is minimised.
- Subscribers can decode without firmware help, and firmware consumers such as ROS and the data logger
  get typed structs.

**Current practice.**

- `PollDataAggregator` is a ring buffer of `s` results for each device.
- The devjson and devbin publishers emit hex or binary payloads with `_i`.
- `getDecodedPollResponses()` and `getLatestDecodedPollResponse()` use the generated decode function and
  carry per-device `RaftBusDeviceDecodeState`, which handles timestamp wrap.
- Records added at runtime without a C++ decoder cannot be decoded on the device (see P3).

**To challenge.**

- Should there be a runtime, table-driven decoder, so that data-only records work everywhere? One was
  tried in Axiom and then removed on 2026-10-13.

---

### P13. Data integrity comes before data availability

**Principle.**

- A poll response that cannot be validated is dropped, not decoded.
- If a device keeps producing invalid data, its identity is in doubt and it should be identified again.

**Rationale.**

- A wrong measurement is worse than a missing one, especially for actuators and control loops.
- On a shared bus, address collisions and resets look like data corruption.

**Current practice.**

- **Declaring a CRC.** `pollInfo.crc` declares a `[seq][crc16]` trailer, a re-read command, a number of
  retries, and a recovery command. An example of recovery is START_APP for an RSAO stuck in its
  bootloader.
- **Old firmware.** Devices without the trailer are detected by an all-zero trailer and accepted
  unchecked. Once a device has produced a non-zero trailer, it is always checked after that.
- **Re-identification.** After repeated drops, the device's identification is cleared and it is
  identified again. This raises **no identity-change event** *(review)*.
- **Coverage.** Only records that declare `crc` (VCP, ThermocoupleAmp) are protected.

**To challenge.**

- Should re-identification produce an event that every consumer must handle, given that consumers cache
  type-derived state?

---

### P14. One task owns each bus; all bus work is serialised and time-sliced on that task

**Principle.**

- On each bus, all scanning, identification, polling, queued requests and application bus work run on a
  single worker task that holds a bus-owner lock.

**Rationale.**

- Mux slot selection and bus speed are global state, so interleaving transactions from several tasks
  would corrupt them.
- Time slicing keeps latency bounded.

**Current practice.**

- **Worker pass.** Runs about every 5 ms (`loopYieldMs`). Each pass runs, in order:
  1. the bus-task handler;
  2. the scanner, if one is pending;
  3. one queued request;
  4. mux, power and IO-expander servicing;
  5. one device poll.
- **Identification** runs synchronously inside the scanner's slice. It can include detection pauses (for
  example `p250`), which block the bus for that time.

**To challenge.**

- Should identification be a resumable state machine, like partial polls, so that long detection pauses
  stop blocking the bus?
- Is "one poll per pass" the right granularity, or should each pass have a time budget?

---

### P15. Per-board configuration can tune the catalogue's defaults without changing records

**Principle.**

- Board configuration (SysTypes) can adjust scanning, the mux address range, IO expanders, address
  exclusions and bus timing.
- Records stay generic.

**Current practice.**

- Bus configuration keys: `scanBoost`, `busScanPeriodMs`, `fastScanMaxUnyieldMs`, `slowScanMaxUnyieldMs`,
  mux `minAddr`/`maxAddr`, and IO expander definitions.
- RSAO keys: `addrRangeLo`/`Hi`, `addrExclude` and `addrArbitration`.
- `DevMan/DeviceNames` and `DevMan/Devices[]` for names and roles.

**To challenge.**

- Should configuration also be able to disable or reprioritise individual records for a board, for
  example "this board never has X" (P3)?

---

## 5. Where practice diverges from the principles

| Principle | Divergence |
|---|---|
| P3 "in flash" | Record and index arrays are non-const and occupy DRAM. Only the string contents and scan lists are in flash. |
| P3 add/override/disable | Add exists with a cap of 20 and silently ignores content for a duplicate device type name. Override shadows only lookups by device type name, and leaves one device type name with two device type indices. Disable does not exist on `main`. There is no runtime loader. |
| P3 efficient derivation | Extended records' address strings are re-parsed on every lookup. Extended addresses are always placed in scan list 0. |
| P5 non-destructive first | The order is right, but a bus error during detection falls through to later candidates and eventually to the destructive probe. |
| P6 small share of bus | The bound is a fixed time slice per pass, not a share that adapts to polling load. |
| P8 consistent access to metadata | Device type indices are positional and carry no version. Typeinfo does not return the device type name. The raftjs cache is never invalidated, and nothing signals a change in the type table. |
| P9 per-device settings | Settings are lost whenever a device goes offline or is identified again. Device names do not persist. |
| P9 display name | Not defined by firmware. raftjs puts it into fields named `name` and `deviceType` that also hold type-level values, and caches it so renames are not seen. |
| P10 exclude list available externally | It is not exposed. It is computed once at setup and not refreshed when records are added. |
| P11 get round every device | First-due by list position, with drift and no fairness. |
| P13 / P8 | Re-identification is silent, so consumers can decode with the wrong record's decoder. |
| Terminology (section 2) | The device type index has six spellings. `deviceTypeId` (hardware type ID) and `deviceTypeIdx` (device type index) are easily confused. `devInfoJson.type` disagrees with the device type name in 2 records. |

## 6. Assumptions under the principles

These are not principles in themselves, but they shape all of the above and are worth challenging
explicitly:

1. **The bus is I2C.** Addresses are 7-bit, a zero-length write is a valid probe, and there are 0x78
   possible addresses on each segment. The address map, scan lists and the "addresses" grammar all depend
   on this.
2. **A device's address largely determines its type.** This is true for fixed-address sensors and false
   for RSAOs, which is why RSAOs need a hook.
3. **One firmware build has one catalogue, fixed at boot.** Runtime records are an add-on, not the main
   model.
4. **Subscribers are in the same session as the firmware.** Device type indices and caches assume that the
   session and the connection last the same time.
5. **Devices without a hardware instance ID are the norm.** The device ID (location) is the identity. RSAO
   serials are used only inside the RSAO code.
