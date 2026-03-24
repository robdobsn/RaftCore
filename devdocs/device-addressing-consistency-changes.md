# Device Addressing Consistency Changes

## Background

Device addresses in this codebase follow the canonical `RaftDeviceID` format:

- **String form:** `<busNum>_<hexAddr>` where `busNum` is decimal and `hexAddr` is lowercase hex with no `0x` prefix and no leading zeros. For example, `1_25` means bus 1, I2C address 0x25.
- **Direct-connected devices** (not on a bus) use bus number `0`, e.g. `0_0`.
- `ANY` is a special string meaning any device on any bus.

This format is defined in `RaftDeviceID::toString()` / `RaftDeviceID::fromString()` in `raftdevlibs/RaftCore/components/core/RaftDevice/RaftDeviceConsts.h`.

The I2C bus uses a composite `BusElemAddrType` (`uint32_t`) internally to encode both the slot number and I2C address: bits 0–7 are the I2C address, bits 8–13 are the slot number (0 = main bus, 1–64 = mux slot). Helper methods for this are in `BusI2CAddrAndSlot` (`raftdevlibs/RaftI2C/components/RaftI2C/BusI2C/BusI2CAddrAndSlot.h`).

---

## Changes Made

### 1. `components/DeviceSlotControl` — use `BusI2CAddrAndSlot` helpers

**Files changed:**
- `components/DeviceSlotControl/CMakeLists.txt`
- `components/DeviceSlotControl/DeviceSlotControl.cpp`

**What changed:**

`DeviceSlotControl.cpp` was manually unpacking the composite I2C address with raw bit operations:

```cpp
// Before
uint16_t slotOnAddr = (uint16_t)((addr >> 8) & 0x3F);
uint16_t i2cAddr    = (uint16_t)(addr & 0xFF);
```

This has been replaced with the `BusI2CAddrAndSlot` static helpers:

```cpp
// After
uint16_t slotOnAddr = BusI2CAddrAndSlot::getSlotNum(addr);
uint16_t i2cAddr    = BusI2CAddrAndSlot::getI2CAddr(addr);
```

Additionally, the `addr` query parameter in the `setrate` REST endpoint was being parsed with `strtoul(..., nullptr, 0)` (auto-detect base), which would misinterpret a canonical plain-hex value like `"25"` as decimal 25. It now uses base 16 explicitly:

```cpp
// Before
filterI2C = strtoul(addrStr.c_str(), nullptr, 0);

// After
filterI2C = strtoul(addrStr.c_str(), nullptr, 16);
```

`RaftI2C` has been added as a component dependency in `CMakeLists.txt` to allow `BusI2CAddrAndSlot.h` to be included.

**REST API impact:** The `addr` query parameter in `slotcontrol/setrate/<slot>/<hz>?addr=<addr>` must now be plain lowercase hex without a `0x` prefix (e.g. `addr=25` not `addr=0x25`), consistent with the canonical address format. Previously `0x`-prefixed values also worked due to the auto-detect base; they will still work because `strtoul` with base 16 still accepts a leading `0x`.

---

### 2. `components/DeviceLEDPixels` — use `BusI2CAddrAndSlot` helpers

**Files changed:**
- `components/DeviceLEDPixels/CMakeLists.txt`
- `components/DeviceLEDPixels/LEDPatternSlotsStatus.h`

**What changed:**

`LEDPatternSlotsStatus.h` was manually extracting the slot number with a raw bit shift:

```cpp
// Before
// Slot is bits 8..13 on address (6 bits)
uint32_t slotNum = (uint32_t)((addr >> 8) & 0x3F);
```

Replaced with:

```cpp
// After
uint32_t slotNum = BusI2CAddrAndSlot::getSlotNum(addr);
```

`RaftI2C` has been added as a component dependency in `CMakeLists.txt`.

---

### 3. `raftdevlibs/RaftCore` — `DeviceManager` API address resolution refactor

**Files changed:**
- `raftdevlibs/RaftCore/components/core/DeviceManager/DeviceManager.h`
- `raftdevlibs/RaftCore/components/core/DeviceManager/DeviceManager.cpp`

#### 3a. New helper: `resolveDeviceIDAndBus()`

A new private helper method has been added to `DeviceManager`:

```cpp
RaftRetCode resolveDeviceIDAndBus(const String& reqStr, String& respStr,
                                  const RaftJson& jsonParams,
                                  RaftDeviceID& deviceID, RaftBus*& pBus);
```

This centralises the pattern of resolving a device from API parameters. It accepts **either**:

- A `deviceid` field — a canonical `RaftDeviceID` string (e.g. `"1_25"`), **or**
- A `bus` field (bus name or numeric index) **and** an `addr` field (plain hex address, e.g. `"25"`).

Either way it returns a valid `RaftDeviceID` (with bus number and address) and a looked-up `RaftBus*` pointer, or fills `respStr` with a JSON error response and returns a non-`RAFT_OK` code.

Error strings returned:
| Condition | Error string |
|---|---|
| `addr` field missing | `failAddrMissing` |
| `bus` field missing | `failBusMissing` |
| Bus name not found | `failBusNotFound` |
| Resolved `deviceID` is not valid | `failInvalidDeviceID` |
| Bus number from `deviceid` not found | `failBusNotFound` |

#### 3b. `apiDevManCmdRaw` — bugs fixed and simplified

Previously this function had:
1. A `DeviceID::fromString(...)` call (wrong class name — should be `RaftDeviceID::fromString`).
2. `pBus` declared inside the `else` block but used after it (scope bug — would not compile or would be undefined when the `deviceid` path was taken).
3. No `deviceid` support in `apiDevManDevConfig` at all.

All of these are resolved by the move to `resolveDeviceIDAndBus()`. The function body is now:

```cpp
RaftDeviceID deviceID;
RaftBus* pBus = nullptr;
RaftRetCode retc = resolveDeviceIDAndBus(reqStr, respStr, jsonParams, deviceID, pBus);
if (retc != RAFT_OK)
    return retc;
// ... remainder of function unchanged
```

#### 3c. `apiDevManDevConfig` — bugs fixed and `deviceid` support added

Previously `apiDevManDevConfig` only accepted `bus` + `addr` with no `deviceid` alternative. It also had a type confusion bug:

```cpp
// Before (buggy)
RaftDeviceID deviceID = RaftDeviceID::fromString(addrStr.c_str()).getAddress();
// .getAddress() returns BusElemAddrType (uint32_t), which was then used to construct
// a RaftDeviceID — putting the address value into the busNum field, leaving address=0.
// addr was therefore always 0 regardless of input.
BusElemAddrType addr = deviceID.getAddress();  // always 0
```

This is now corrected. `apiDevManDevConfig` uses `resolveDeviceIDAndBus()` identically to `apiDevManCmdRaw`, and the response JSON `deviceID` field now correctly includes the bus number.

---

## Documentation Implications

### REST API — `devman/cmdraw`

Both of these parameter combinations are now consistently supported:

```
# Option 1: canonical deviceID string
devman/cmdraw?deviceid=1_25&hexWr=0a0b&numToRd=2

# Option 2: separate bus name and address
devman/cmdraw?bus=I2CA&addr=25&hexWr=0a0b&numToRd=2
```

### REST API — `devman/devconfig`

Same two-option addressing now applies (previously only `bus`+`addr` was accepted, and was broken):

```
# Option 1
devman/devconfig?deviceid=1_25&intervalUs=50000

# Option 2
devman/devconfig?bus=I2CA&addr=25&intervalUs=50000
```

### REST API — `slotcontrol/setrate`

The optional `addr` filter parameter should be plain hex without `0x` prefix:

```
# Correct
slotcontrol/setrate/2/10?addr=25

# Also accepted (0x prefix tolerated by strtoul base-16)
slotcontrol/setrate/2/10?addr=0x25
```

### Address format convention

All API endpoints that accept an address field now expect the canonical format: **plain lowercase hex, no `0x` prefix, no leading zeros**. This matches `RaftDeviceID::toString()` output and the format documented at https://github.com/robdobsn/RaftCore/wiki/DeviceDataPublishing#device-address-notation.
