# System Device Tagging — Design & Implementation Plan

## 1. Goal

Allow individual devices managed by `DeviceManager` (both *static* devices defined in `DevMan/Devices` and *bus-attached* devices discovered on an I²C / serial bus) to be marked as **system devices**. A system device is one used internally by the firmware (e.g. an on-board battery-gauge IC that monitors the Axiom unit itself) as opposed to an "application" device (e.g. an external sensor on a slot).

Specifically the user wants:

- A way to declare a static device (e.g. the `Power` device in `SysTypes.json`) as a system device.
- A way to declare a particular bus device (identified by bus name/number + address) as a system device — addresses for system devices are fixed.
- A way to override the **published name** of a bus device.
- Data from system devices still to be **published normally** through the existing `devjson` / `devbin` channels. **The on-wire publish formats are not changed.** Subscribers learn whether a given device is a system device (and any name override) via the existing *device-type-data* lookup path — i.e. an extension to `devman/typeinfo`.
- The marker is a **string** (e.g. `"role": "system"`), not a boolean, so that other roles can be introduced later (e.g. `"hidden"`, `"diagnostic"`) without another schema change. Currently defined values: `"system"` and `"normal"` (default).

This document captures the current code shape, the design constraints, the recommended approach, alternatives, and the concrete implementation steps. **No code is changed yet.**

---

## 2. Current state — what already exists

### 2.1 `DeviceManager` (RaftCore)

- File: [raftdevlibs/RaftCore/components/core/DeviceManager/DeviceManager.h](raftdevlibs/RaftCore/components/core/DeviceManager/DeviceManager.h), [raftdevlibs/RaftCore/components/core/DeviceManager/DeviceManager.cpp](raftdevlibs/RaftCore/components/core/DeviceManager/DeviceManager.cpp)
- Maintains `_staticDeviceList` (a list of `RaftDevice*` instances built from `DevMan/Devices` in SysTypes.json).
- Bus devices are **not** in that list — they are owned by `RaftBusSystem` / each bus's `RaftBusDevicesIF` and discovered at runtime.
- Already supports **friendly names** for bus devices via the `DeviceNames` config section and the `_deviceNameToID` / `_deviceIDToName` maps (loaded by `loadDeviceNames`). The friendly-name key in JSON is a packed `RaftDeviceID` string such as `"1_f8"` (bus `1`, address `0xf8`). See `DeviceManager::loadDeviceNames`, `setDeviceName`, `getDeviceNameForID`, `resolveDeviceNameToID` and the API endpoint `devman/setname`.
- Publishing: `getDevicesDataJSON(topicIndex)` / `getDevicesDataBinary(topicIndex)` walk both the bus list and the static device list. Static device JSON comes from `RaftDevice::getStatusJSON()` and is keyed under `BUS_NUM_DIRECT_CONN`; bus JSON comes from `RaftBusDevicesIF::getQueuedDeviceDataJson()` keyed by bus number.
- API: `devman/typeinfo?type=<name|index>` returns the JSON describing a device type (the `devInfoJson` field of a `DeviceTypeRecord`).

### 2.2 `RaftDevice`

- File: [raftdevlibs/RaftCore/components/core/RaftDevice/RaftDevice.h](raftdevlibs/RaftCore/components/core/RaftDevice/RaftDevice.h)
- Holds `_deviceID` (a `RaftDeviceID(busNum, address)`; for static devices the bus number is `BUS_NUM_DIRECT_CONN`).
- Holds class name, configured device type, configured device name, and a `deviceTypeIndex`.
- Each static device can optionally produce its own dynamic `DeviceTypeRecordDynamic` via `getDeviceTypeRecord(...)` — added to `deviceTypeRecords` in `setupStaticDevices` and assigned its own `deviceTypeIndex`. So **per-static-instance type records do exist**.

### 2.3 `DeviceTypeRecord` / `DeviceTypeRecords`

- Files: [raftdevlibs/RaftCore/components/core/DeviceTypes/DeviceTypeRecord.h](raftdevlibs/RaftCore/components/core/DeviceTypes/DeviceTypeRecord.h), [raftdevlibs/RaftCore/components/core/DeviceTypes/DeviceTypeRecordDynamic.h](raftdevlibs/RaftCore/components/core/DeviceTypes/DeviceTypeRecordDynamic.h), [raftdevlibs/RaftCore/components/core/DeviceTypes/DeviceTypeRecords.h](raftdevlibs/RaftCore/components/core/DeviceTypes/DeviceTypeRecords.h), [raftdevlibs/RaftCore/components/core/DeviceTypes/DeviceTypeRecords.cpp](raftdevlibs/RaftCore/components/core/DeviceTypes/DeviceTypeRecords.cpp)
- `DeviceTypeRecord` is a POD containing `deviceType`, `addresses`, `detectionValues`, `initValues`, `pollInfo`, `devInfoJson`, decode fn, etc. Sample `devInfoJson` content (from `devtypes/DeviceTypeRecords.json`):

  ```json
  { "name": "Robotical Light Sensor", "desc": "Light Sensor",
    "manu": "Robotical", "type": "RoboticalLightSensor",
    "clas": ["LGHT"], "resp": { ... }, "actions": [] }
  ```

- **Two flavours** of these records:
  - **Built-in / immutable** — generated at build time and shared by every bus device that detects as that type. There is **exactly one** record per device type, no matter how many physical devices match.
  - **Extended / dynamic** — added at runtime via `deviceTypeRecords.addExtendedDeviceTypeRecord(...)`, typically once per static device instance (see `setupStaticDevices`). These *are* effectively per-instance for static devices today.

### 2.4 Why this matters for "system" tagging

The shared `DeviceTypeRecord` for a bus device type is consulted by **every** physical device of that type on every bus, so stamping `"sys":1` onto e.g. an `LSM6DS3` record would mark every `LSM6DS3` as a system device — unusable as a per-instance flag. Even for static devices, where a dedicated `DeviceTypeRecordDynamic` exists per instance, treating the type record as the role store is fragile and asymmetric with the bus case. The role is therefore kept in a **sidecar map** in `DeviceManager` keyed by `RaftDeviceID`, used uniformly for both static and bus devices. The same store underpins the existing `_deviceNameToID` / `_deviceIDToName` maps — see §4.1.

---

## 3. JSON configuration design

All device declarations live in the existing `DevMan/Devices` array. An entry is interpreted as either a **static device** or a **bus device** based on whether the `class` field is present:

- `class` present → static device (handled by the device factory exactly as today).
- `class` absent  → bus device declaration (a hint/override for a bus-attached device at a fixed address).

### 3.1 Static device entry (existing — adds one optional field)

```json
{
    "class": "Power",
    "name": "Power",
    "role": "system",
    "type": "AxiomPowerV1",
    ...
}
```

The only new field is the optional `role` string. If omitted the device is treated as normal (`"role": "normal"`). All other static-device parsing is unchanged.

Note: the static `type` field is the existing **dynamic-record discriminator** — it selects which `DeviceTypeRecordDynamic` the static device exposes (e.g. `AxiomPowerV1`). It is unrelated to the bus-side type hint that was considered and rejected for bus entries (§6 Q3).

### 3.2 Bus device entry (new — `class` field absent)

```json
{
    "bus": "I2CA",
    "addr": "0x6a",
    "name": "BattGauge",
    "role": "system"
}
```

Fields:
- `bus` — bus *name* (matched via `raftBusSystem.getBusByName`). Required. String only — numeric bus numbers are not accepted.
- `addr` — fixed I²C/SPI address (`0x..` or decimal). Required.
- `name` — friendly publish name (optional). When present it is written into the existing `_deviceNameToID` / `_deviceIDToName` maps via `setDeviceName` — i.e. exactly as if a `DeviceNames` entry had been used. See §4.1.
- `role` — optional string, default `"normal"`. `"system"` marks the device as an internal system device. Omitted in published responses when the value is the default.
- `enable` — optional, default `true`. `enable: false` causes the entry to be ignored: no `name` / `role` registration is performed. It does **not** affect bus detection, instantiation, or publishing — if the device is present on the bus it will still be detected and published as normal (with no name override and default role).

The device type is **not** declared in the entry — normal bus detection (`detectionValues` lookup) determines the type at runtime. If detection fails the address is treated as having no device.

`class` MUST NOT be present in a bus-device entry; if it is, the entry is treated as a static-device entry and the factory looks for that class.

### 3.3 Combined example

```json
"DevMan": {
    "Buses": { "buslist": [ ... ] },
    "Devices": [
        {
            "class": "Power",
            "name": "Power",
            "role": "system",
            ...
        },
        {
            "bus": "I2CA",
            "addr": "0x6a",
            "name": "BattGauge",
            "role": "system"
        }
    ]
}
```

### 3.4 Backwards compatibility — `DeviceNames`

The existing `DeviceNames` block (a `RaftDeviceID-string -> name` map) is retained and `loadDeviceNames` continues to run, so existing configurations are not broken. A bus-device entry in `Devices` with a `name` field is the preferred mechanism going forward and supersedes any `DeviceNames` entry for the same `RaftDeviceID`.

### 3.5 Why a string and not a boolean

A boolean `"system": true` was considered but rejected for the reasons the user gave: future categories such as `"hidden"`, `"diagnostic"`, or `"developer"` are easy to anticipate. A single string field is the cheapest forward-compatible representation. The field is named `role`; currently defined values are `"normal"` (default) and `"system"`.

---

## 4. Runtime tagging mechanism

### 4.1 Storage in `DeviceManager`

**Name overrides reuse the existing maps.** The `_deviceNameToID` / `_deviceIDToName` maps already in `DeviceManager` (populated today by `loadDeviceNames` from the `DevMan/DeviceNames` config block, and mutable at runtime via `setDeviceName` and the `devman/setname` API) are reused unchanged for the new `name` field on bus-device entries. Specifically, the single-pass `setupDevices` routine (§4.2) calls `setDeviceName(deviceID, entry["name"])` for any entry that carries a `name` — for both static and bus entries — so there is exactly one source of truth for device-name lookup. The `DeviceNames` config block continues to work for backwards compatibility.

Add one new per-instance map for the role tag, alongside the existing name maps:

```cpp
// packDeviceIDKey(busNum, address) -> role string (interned)
std::unordered_map<uint64_t, std::string> _deviceRole;
```

Plus accessor helpers:

```cpp
void    setDeviceRole(RaftDeviceID id, const String& role);
String  getDeviceRole(RaftDeviceID id) const;   // returns "normal" if unset
bool    isSystemDevice(RaftDeviceID id) const;  // convenience: role == "system"
```

Static devices use their `RaftDeviceID(BUS_NUM_DIRECT_CONN, idx)`. Bus devices use `RaftDeviceID(busNum, addr)`. One map covers both. Storing the string (rather than an enum) keeps the field open-ended; `isSystemDevice` is a thin wrapper for the only currently-meaningful value. A small intern set keeps memory cost minimal even if the map grows.

**Single source of truth.** `DeviceManager` owns the role and name stores. `RaftDevice` does **not** keep its own copy of the role (no `_role` member) and — once the existing `configuredDeviceName` member is removed — does not keep its own copy of the name either. Instead, `RaftDevice::isSystemDevice()` and `RaftDevice::getConfiguredDeviceName()` are thin accessors that delegate to `DeviceManager` using the device's own `RaftDeviceID`. This eliminates the previous duplication between `RaftDevice::configuredDeviceName` and the name map (§2) and gives static and bus devices identical lookup behaviour.

### 4.2 Population — single pass over `DevMan/Devices`

**Ordering.** `loadDeviceNames(modConfig())` runs first (existing behaviour, populating the name maps from `DevMan/DeviceNames`). The new `setupDevices` runs second and overwrites map entries for any `RaftDeviceID` it processes, so a `Devices` entry's `name` field supersedes a `DeviceNames` entry for the same ID. This order is part of the contract and must not be changed.

`setupStaticDevices` (renamed conceptually to `setupDevices`, file/identifier rename optional) iterates `DevMan/Devices` once and dispatches on the presence of `class`:

1. **Entry has `class`** — existing static-device path: look up the factory, instantiate, `setDeviceID(...)`, push onto `_staticDeviceList`. Then:
   - If `name` is present, call `setDeviceName(deviceID, name)` so the existing name map also covers static devices (today it does not — see §2.1 / §6 Q5).
   - Read `role` from the entry and call `setDeviceRole(deviceID, role)`. No mirror onto the `RaftDevice` is needed — see the single-source-of-truth note in §4.1.
2. **Entry has no `class`** — bus-device path:
   - If `enable` is explicitly `false`, skip the entry entirely.
   - Resolve `bus` via `raftBusSystem.getBusByName(...)`; skip with a warning if not found.
   - Parse `addr`; build `RaftDeviceID(busNum, addr)`.
   - If `name` is present, call `setDeviceName(deviceID, name)`.
   - If `role` is present, call `setDeviceRole(deviceID, role)`.
   - No `RaftDevice*` instance is created — the device remains bus-tracked, and the bus's existing detection logic identifies its type when it appears on the bus. The name and role sidecar entries take effect only once detection confirms the device exists (§4.4).
3. **Runtime API** — a new `devman/setrole` endpoint is added so the role string can be set/cleared at runtime. It follows the existing `devman/setname` shape: `devman/setrole?deviceid=<packed>&role=<value>`. `devman/setname` keeps its existing behaviour and now applies equally well to static devices (since they are registered into the name map per step 1). **Neither endpoint persists changes** — the maps are in-memory only and are repopulated from `DevMan/DeviceNames` and `DevMan/Devices` on each boot.

### 4.3 `RaftDevice` — thin accessors, no per-instance copies

Direct callers that hold a `RaftDevice*` should still be able to ask the device for its name and role without knowing about `DeviceManager`'s internals. The plan therefore:

- Removes `String configuredDeviceName;` from `RaftDevice`.
- Re-implements `RaftDevice::getConfiguredDeviceName()` to delegate to `DeviceManager::getDeviceNameForID(_deviceID)` (returning an empty string if `DeviceManager` is unavailable, matching today's empty-default behaviour).
- Adds `RaftDevice::isSystemDevice()` (and optionally `getRole()`) implemented as `_pDeviceManager && _pDeviceManager->isSystemDevice(_deviceID)`.
- Stores no role on the `RaftDevice` instance.

The net effect: every property derived from a `RaftDeviceID` (name, role) has exactly one store — the sidecar maps in `DeviceManager` — and static and bus devices are queried the same way.

### 4.4 Exposing role to subscribers — device-type-data only

**The `devjson` and `devbin` publish formats are NOT changed.** Per-instance role and name overrides are surfaced purely through the existing device-type-data lookup path; a subscriber that already calls `devman/typeinfo` to learn how to decode a device picks the role up at the same time.

1. **`devman/typeinfo` (per-instance variant) — primary mechanism.**
   Today this endpoint takes `type=<typeName|typeIndex>` and returns the shared type record. Extend it to also accept `deviceid` (a packed `RaftDeviceID` string such as `"1_6a"`) so it can return per-instance info. The `deviceid` form is used because it matches the keys that already appear in `devjson` / `devbin` publish payloads — a subscriber receiving a publish message can call `typeinfo?deviceid=<same key>` directly, without an intermediate bus-number-to-name lookup. The response gains two new fields *outside* `devinfo` so the shared `devInfoJson` is not modified:

   ```jsonc
   {
       "rslt": "ok",
       "devinfo": { ... shared type info ... },
       "dtIdx": 42,
       "role": "system",       // omitted when "normal"
       "name": "BattGauge"     // omitted when no name override is set
   }
   ```

   This is the only mechanism subscribers need to use to discover the role of a specific device. It cleanly separates *type metadata* (shared, immutable, the existing `devinfo` payload) from *instance metadata* (per-device, mutable, the new sibling fields).

   `typeinfo` only returns a successful response for devices that **actually exist**: instantiated static devices, and bus devices that have been detected and identified by the bus. A `Devices` entry that declared a bus device which has not (yet) been detected is **not** reported — `typeinfo` returns the existing not-found error for the unknown `deviceid`. The sidecar `name` / `role` for that address sit dormant in the maps until the device appears.

2. **REST helper: `devman/listdevs`** — a new convenience endpoint returning `[ {deviceid, name, typeIndex, role}, ... ]` for all **existing** devices: instantiated static devices plus bus-detected devices. Optionally filterable by `?role=system`. Bus entries that have not been detected are not listed. This is purely for clients that want to enumerate devices without walking the publish stream and consulting `typeinfo` for each one.

No changes are required to:
- `RaftDevice::getStatusJSON()` / `RaftDevice::getStatusBinary()`
- `RaftBusDevicesIF::getQueuedDeviceDataJson()` / `RaftBusDevicesIF::getQueuedDeviceDataBinary()`
- The `devbin` envelope or per-device record headers
- `DeviceManager::getDevicesDataJSON()` / `DeviceManager::getDevicesDataBinary()`

---

## 5. Implementation steps (ordered)

1. **`RaftDevice`** —
   - Remove the `configuredDeviceName` member.
   - Re-implement `getConfiguredDeviceName()` to delegate to `DeviceManager::getDeviceNameForID(_deviceID)`.
   - Add `isSystemDevice()` (and optionally `getRole()`) as a thin delegate to `DeviceManager`.
   - No role/name members are stored on `RaftDevice` itself.
2. **`DeviceManager.h`** — add:
   - `_deviceRole` map.
   - `setDeviceRole(RaftDeviceID, const String&)` / `getDeviceRole(RaftDeviceID) const` / `isSystemDevice(RaftDeviceID) const`.
   - (No new name maps — the existing `_deviceNameToID` / `_deviceIDToName` are reused for name overrides on both static and bus entries.)
3. **`DeviceManager.cpp`** —
   - Rework `setupStaticDevices` to handle both branches in a single pass over `DevMan/Devices` (dispatch on presence of `class`), preserving the ordering guarantee that `loadDeviceNames` runs first.
     - `class` present → existing static-device flow, plus `setDeviceName` (so static devices appear in the existing name map) and `setDeviceRole`.
     - `class` absent → honour `enable: false` (skip), then resolve `bus`+`addr` and set name and role as described in §4.2.
   - In `apiDevManTypeInfo`, accept `deviceid` (packed `RaftDeviceID` string) and emit extra `role` / `name` sibling fields alongside the existing `devinfo` / `dtIdx`, omitted when at their default values. Only return per-instance info for devices that exist (instantiated static or bus-detected).
   - Add a new `apiDevManListDevs` handler exposed at `devman/listdevs`, with optional `?role=` filter. Reports only existing devices.
   - Add a new `apiDevManSetRole` handler exposed at `devman/setrole`, accepting `deviceid=<packed>&role=<value>`, mirroring the shape of `devman/setname`. In-memory only — no NVS persistence.
   - **No changes** to `getDevicesDataJSON`, `getDevicesDataBinary`, or any bus-side JSON/binary emission — the publish formats stay byte-for-byte identical.
4. **Linux unit tests** (`raftdevlibs/RaftCore/linux_unit_tests/`) — extend `DeviceNamesTest` (or add `DeviceRoleTest`) covering:
   - Mixed `Devices` array with both `class`-present and `class`-absent entries.
   - Static device with `"role": "system"`.
   - Bus device entry with `name`, `role`.
   - `enable: false` on a bus entry suppresses name/role registration but does not affect detection.
   - `RaftDevice::getConfiguredDeviceName()` and `isSystemDevice()` returning the values stored in `DeviceManager`.
   - `typeinfo?deviceid=...` returning the `role` / `name` fields (omitted when default) for an existing device, and not-found for an undetected bus address.
   - `listdevs` enumeration and `?role=` filter.
   - Ordering: `DeviceNames` entry overwritten by a later `Devices` entry for the same `RaftDeviceID`.
5. **Docs** — a short note in `raftdevlibs/RaftCore/devdocs/` describing the schema extension and the `typeinfo` / `listdevs` / `setrole` API additions. No format-doc changes required since wire formats are unchanged.
6. **Axiom side migration** — once the mechanism lands, update `systypes/Axiom009/SysTypes.json` to mark `Power` with `"role": "system"` and add a no-`class` entry to `Devices` for any battery-gauge IC on the I²C bus. (Out of scope for RaftCore but called out so the change can be done as a follow-up.)
4. **Linux unit tests** (`raftdevlibs/RaftCore/linux_unit_tests/`) — extend `DeviceNamesTest` (or add `DeviceRoleTest`) covering:
   - Mixed `Devices` array with both `class`-present and `class`-absent entries.
   - Static device with `"role": "system"`.
   - Bus device entry with `name`, `role`.
   - `typeinfo?deviceid=...` returning the `role` / `name` fields.
   - `listdevs` enumeration and filter.
5. **Docs** — a short note in `raftdevlibs/RaftCore/devdocs/` describing the schema extension and the `typeinfo` / `listdevs` API additions. No format-doc changes required since wire formats are unchanged.
6. **Axiom side migration** — once the mechanism lands, update `systypes/Axiom009/SysTypes.json` to mark `Power` with `"role": "system"` and add a no-`class` entry to `Devices` for any battery-gauge IC on the I²C bus. (Out of scope for RaftCore but called out so the change can be done as a follow-up.)

---

## 6. Open questions for the user

All previously-open questions are now resolved. Captured here for reference:

1. **Subscriber path** — resolved: role is exposed only via `devman/typeinfo` (per-instance variant) and the convenience `devman/listdevs` endpoint. The `devjson` and `devbin` publish formats are unchanged.
2. **Field name** — resolved: `role`, with `"system"` and `"normal"` as the currently defined values.
3. **No type-hint** — resolved: bus-device entries do *not* carry a `type` field. Bus detection identifies the device; if detection fails the address is assumed empty.
4. **No `RaftDevice` for bus-attached devices** — resolved: bus-device entries remain purely bus-tracked. No `RaftDevice*` is instantiated and they are not added to `_staticDeviceList`. They exist only as entries in the name and role sidecar maps.
5. **Unified name handling** — resolved: a single mechanism (the existing `_deviceNameToID` / `_deviceIDToName` maps) is used for both static and bus devices. `RaftDevice::getConfiguredDeviceName()` delegates to `DeviceManager` (the `configuredDeviceName` member is removed) so there is exactly one store of a device's name.
6. **Scope of `bus` field in SysTypes** — resolved: textual bus name only (e.g. `"I2CA"`), resolved via `raftBusSystem.getBusByName(...)`. Note: `RaftDeviceID` strings used elsewhere (`devjson` / `devbin` payloads, `devman/typeinfo?deviceid=...`, `devman/setname`, `devman/setrole`) remain in their existing packed numeric form so that recipients of publish messages can look up per-instance info directly, without first resolving a bus number to a name.
7. **Default emission** — resolved: `role` is omitted from `typeinfo` / `listdevs` responses when `"normal"`; `name` is omitted when no override is set. Consumers assume defaults when the field is absent.
8. **Reserved values** — resolved: only `"normal"` and `"system"` are defined. No other role values are documented or reserved.
9. **Undetected bus devices** — resolved: a `Devices` entry that declares a bus device is **not** reported by `typeinfo` or `listdevs` until the device is actually detected on the bus. The sidecar name/role entries sit dormant until then.

---

## 7. Files expected to change

- [raftdevlibs/RaftCore/components/core/DeviceManager/DeviceManager.h](raftdevlibs/RaftCore/components/core/DeviceManager/DeviceManager.h)
- [raftdevlibs/RaftCore/components/core/DeviceManager/DeviceManager.cpp](raftdevlibs/RaftCore/components/core/DeviceManager/DeviceManager.cpp)
- [raftdevlibs/RaftCore/components/core/RaftDevice/RaftDevice.h](raftdevlibs/RaftCore/components/core/RaftDevice/RaftDevice.h) — trivial: remove `configuredDeviceName` member, change accessors to delegate to `DeviceManager`, add `isSystemDevice()` delegate.
- [raftdevlibs/RaftCore/components/core/RaftDevice/RaftDevice.cpp](raftdevlibs/RaftCore/components/core/RaftDevice/RaftDevice.cpp) — trivial: drop the `configuredDeviceName = deviceConfig.getString("name", "")` line from the constructor; the name now lives only in `DeviceManager`'s map.
- Linux unit tests under `raftdevlibs/RaftCore/linux_unit_tests/`

No changes are required to:
- `DeviceTypeRecord` / `DeviceTypeRecordDynamic` — the role value is deliberately kept *outside* the shared type record.
- The `devjson` / `devbin` publish formats or their format docs.
- The existing `_deviceNameToID` / `_deviceIDToName` maps in `DeviceManager` (their *schema* is unchanged — they simply gain new entries via `setDeviceName` during the single-pass `setupDevices`).
