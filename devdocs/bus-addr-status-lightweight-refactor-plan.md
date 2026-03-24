# BusAddrStatus Lightweight Refactor Plan

## Overview

This document details the plan to refactor `BusAddrStatus` into a lightweight struct for status change callbacks, while renaming the current heavy class to `BusAddrRecord` for internal bus management use.

### Problem Statement

The current `BusAddrStatus` class is used both:
1. **Internally in BusStatusMgr** — for maintaining full device state with polling info, callbacks, response counting, etc.
2. **In callbacks (BusElemStatusCB)** — to communicate simple status changes (online/offline/identified)

When status changes are reported via callback, the entire `BusAddrStatus` is copied (via `push_back`), including:
- `DeviceStatus` member containing `DevicePollingInfo` with `std::vector<BusRequestInfo>` and `std::vector<uint8_t>` poll results
- Each `BusRequestInfo` contains `std::vector<uint8_t> _writeData` and `String _elemName`
- Device data change callbacks and timing information

This results in unnecessary deep copies of vectors and strings when only a few fields are needed for status change notifications.

### Design Decisions

Based on analysis of callback receivers:
- **Flat deviceTypeIndex**: Direct field, no nested `DeviceStatus` struct
- **Keep getOnlineStateStr()**: Static helper remains in `BusAddrStatus` for API compatibility
- **Keep simplified getJson()**: Returns minimal debug JSON
- **Conversion method**: `BusAddrRecord::toStatusChange()` creates lightweight `BusAddrStatus`

---

## Current Structure Analysis

### Fields accessed by callback receivers (DeviceManager::busElemStatusCB):

| Field | Usage |
|-------|-------|
| `address` | Device identification |
| `onlineState` | Status (ONLINE/OFFLINE/PENDING_DELETION) |
| `isChange` | Flag for status transition |
| `isNewlyIdentified` | Flag for new device |
| `deviceStatus.deviceTypeIndex` | Device type lookup |

### Fields used only internally by BusStatusMgr:

| Field | Purpose |
|-------|---------|
| `count` | Response counting for online/offline determination |
| `slotResolved` | Slot resolution flag |
| `barStartMs`, `barDurationMs` | Access barring |
| `minTimeBetweenReportsMs`, `lastDataChangeReportTimeMs` | Data change rate limiting |
| `deviceStatus` (full) | Polling info, identification, poll results |
| `dataChangeCB`, `pCallbackInfo` | Device data change callbacks |
| `handleResponding()` | State machine logic |

---

## Proposed Structure

### New Lightweight BusAddrStatus (for callbacks)

```cpp
/// @brief Lightweight status change notification for bus element callbacks
/// Contains only the essential fields needed to communicate status changes
class BusAddrStatus
{
public:
    /// @brief Constructor
    BusAddrStatus(BusElemAddrType address = 0, 
                  DeviceOnlineState onlineState = DeviceOnlineState::INITIAL, 
                  bool isChange = false, 
                  bool isNewlyIdentified = false, 
                  DeviceTypeIndexType deviceTypeIndex = DEVICE_TYPE_INDEX_INVALID) :
        address(address),
        onlineState(onlineState),
        isChange(isChange),
        isNewlyIdentified(isNewlyIdentified),
        deviceTypeIndex(deviceTypeIndex)
    {}

    // Essential fields only
    BusElemAddrType address = 0;
    DeviceOnlineState onlineState : 3 = DeviceOnlineState::INITIAL;
    bool isChange : 1 = false;
    bool isNewlyIdentified : 1 = false;
    DeviceTypeIndexType deviceTypeIndex = DEVICE_TYPE_INDEX_INVALID;

    // Static helpers (kept for API compatibility)
    static const char* getOnlineStateStr(DeviceOnlineState onlineState);
    
    // Constants (kept for test compatibility)
    static const uint32_t ADDR_RESP_COUNT_FAIL_MAX_DEFAULT = 3;
    static const uint32_t ADDR_RESP_COUNT_OK_MAX_DEFAULT = 2;

    // Simplified JSON for debugging
    String getJson() const;
};
```

**Estimated size**: ~8 bytes (vs current ~100+ bytes plus vectors)

### New BusAddrRecord (internal heavy class)

```cpp
/// @brief Full address record for internal bus status management
/// Contains all state needed for device tracking, identification, and data change callbacks
class BusAddrRecord
{
public:
    /// @brief Constructor
    BusAddrRecord(BusElemAddrType address = 0, 
                  DeviceOnlineState onlineState = DeviceOnlineState::INITIAL,
                  bool isChange = false, 
                  bool isNewlyIdentified = false,
                  DeviceTypeIndexType deviceTypeIndex = DEVICE_TYPE_INDEX_INVALID);

    // Address
    BusElemAddrType address = 0;

    // Online/offline counting
    int8_t count = 0;

    // State flags
    DeviceOnlineState onlineState : 3 = DeviceOnlineState::INITIAL;
    bool isChange : 1 = false;
    bool slotResolved : 1 = false;
    bool isNewlyIdentified : 1 = false;

    // Access barring
    uint32_t barStartMs = 0;
    uint16_t barDurationMs = 0;

    // Data change timing
    uint32_t minTimeBetweenReportsMs = 0;
    uint32_t lastDataChangeReportTimeMs = 0;

    // Full device status (polling info, identification, etc.)
    DeviceStatus deviceStatus;

    // Device data change callback and info
    RaftDeviceDataChangeCB dataChangeCB = nullptr;
    const void* pCallbackInfo = nullptr;

    // State machine for response handling
    bool handleResponding(bool isResponding, bool &flagSpuriousRecord, 
            uint32_t okMax = BusAddrStatus::ADDR_RESP_COUNT_OK_MAX_DEFAULT, 
            uint32_t failMax = BusAddrStatus::ADDR_RESP_COUNT_FAIL_MAX_DEFAULT);

    // Register for data change
    void registerForDataChange(RaftDeviceDataChangeCB dataChangeCB, 
                               uint32_t minTimeBetweenReportsMs, 
                               const void* pCallbackInfo);

    // Get data change callback
    RaftDeviceDataChangeCB getDataChangeCB() const;
    const void* getCallbackInfo() const;

    /// @brief Create lightweight status change for callbacks
    /// @return BusAddrStatus with essential fields only
    BusAddrStatus toStatusChange() const
    {
        return BusAddrStatus(address, onlineState, isChange, isNewlyIdentified, 
                             deviceStatus.deviceTypeIndex);
    }

    // Debug
    String getJson() const;
};
```

---

## Implementation Steps

### Phase 1: Create new files and structures

| Step | File | Description |
|------|------|-------------|
| 1.1 | `BusAddrStatus.h` | Rewrite as lightweight struct with essential fields only |
| 1.2 | `BusAddrRecord.h` | New file - copy current `BusAddrStatus` structure |
| 1.3 | `BusAddrRecord.cpp` | New file - move `handleResponding()` and `getJson()` implementations |
| 1.4 | `BusAddrStatus.cpp` | Update to minimal implementation (simplified `getJson()` only) |
| 1.5 | `RaftCore/CMakeLists.txt` | Add `BusAddrRecord.cpp` to build |

### Phase 2: Update BusStatusMgr (RaftI2C)

| Step | File | Description |
|------|------|-------------|
| 2.1 | `BusStatusMgr.h` | Change `#include "BusAddrStatus.h"` to `#include "BusAddrRecord.h"` |
| 2.2 | `BusStatusMgr.h` | Change `std::vector<BusAddrStatus> _addrStatus` to `std::vector<BusAddrRecord>` |
| 2.3 | `BusStatusMgr.h` | Update `findAddrStatusRecord()` return type to `const BusAddrRecord*` |
| 2.4 | `BusStatusMgr.h` | Update `findAddrStatusRecordEditable()` return type to `BusAddrRecord*` |
| 2.5 | `BusStatusMgr.cpp` | Replace internal `BusAddrStatus` with `BusAddrRecord` |
| 2.6 | `BusStatusMgr.cpp` | Change `statusChanges.push_back(addrStatus)` to `statusChanges.push_back(addrStatus.toStatusChange())` |

### Phase 3: Update other internal callers

| Step | File | Description |
|------|------|-------------|
| 3.1 | `DeviceIdentMgr.cpp` | Update if it references `BusAddrStatus` fields internally |
| 3.2 | `BusScanner.h` | Update constants reference (now via `BusAddrStatus::` still works) |

### Phase 4: Verify callback consumers (no changes expected)

| File | Status |
|------|--------|
| `DeviceManager.cpp` | Should work unchanged — uses `el.address`, `el.onlineState`, `el.isChange`, `el.isNewlyIdentified`, `el.deviceTypeIndex` (new flat field) |
| `RaftDevice.h` | `handleStatusChange(const BusAddrStatus&)` — signature unchanged |
| `RaftDeviceConsts.h` | `RaftDeviceStatusChangeCB` typedef — unchanged |
| `BLEBusDeviceManager.cpp` | Creates lightweight `BusAddrStatus` directly — works as before |

### Phase 5: Update DeviceManager for new API

| Step | File | Description |
|------|------|-------------|
| 5.1 | `DeviceManager.cpp` | Change `el.deviceStatus.deviceTypeIndex` to `el.deviceTypeIndex` |

### Phase 6: Update tests

| Step | File | Description |
|------|------|-------------|
| 6.1 | `test_bus_i2c.cpp` | Verify `statusChangesList` still works with lightweight struct |
| 6.2 | `test_bus_i2c.cpp` | Update any field access if needed |

---

## Migration Path for External Code

For any external code using `BusAddrStatus`:

### Callback handlers (no change needed)
```cpp
// Before and after — works the same
void myCallback(RaftBus& bus, const std::vector<BusAddrStatus>& statusChanges) {
    for (const auto& el : statusChanges) {
        // These all work unchanged:
        el.address;
        el.onlineState;
        el.isChange;
        el.isNewlyIdentified;
        // Change: el.deviceStatus.deviceTypeIndex → el.deviceTypeIndex
        el.deviceTypeIndex;  // NEW: flat field
    }
}
```

### Creating status notifications (no change needed)
```cpp
// BLEBusDeviceManager-style direct creation still works
BusAddrStatus addrStatus(address, DeviceOnlineState::ONLINE, true, true, _deviceTypeIndex);
_raftBus.callBusElemStatusCB({addrStatus});
```

---

## Files Changed Summary

| Repository | File | Change Type |
|------------|------|-------------|
| RaftCore | `components/core/Bus/BusAddrStatus.h` | **Modified** — lightweight struct |
| RaftCore | `components/core/Bus/BusAddrStatus.cpp` | **Modified** — minimal implementation |
| RaftCore | `components/core/Bus/BusAddrRecord.h` | **New** — heavy struct (was BusAddrStatus) |
| RaftCore | `components/core/Bus/BusAddrRecord.cpp` | **New** — implementations moved from BusAddrStatus.cpp |
| RaftCore | `CMakeLists.txt` | **Modified** — add BusAddrRecord.cpp |
| RaftCore | `components/core/DeviceManager/DeviceManager.cpp` | **Modified** — `.deviceStatus.deviceTypeIndex` → `.deviceTypeIndex` |
| RaftI2C | `components/RaftI2C/BusI2C/BusStatusMgr.h` | **Modified** — use BusAddrRecord |
| RaftI2C | `components/RaftI2C/BusI2C/BusStatusMgr.cpp` | **Modified** — use BusAddrRecord, toStatusChange() |

---

## Performance Impact

### Before (copying full BusAddrStatus):
- ~100+ bytes fixed fields
- Plus `std::vector<BusRequestInfo>` (each ~40+ bytes with vector and String members)
- Plus `std::vector<uint8_t>` poll results
- Plus `std::shared_ptr` operations
- **Deep copies on every push_back**

### After (copying lightweight BusAddrStatus):
- ~8 bytes total
- No vectors, no strings, no shared_ptr
- **Trivial copy operations**

### Estimated improvement:
- **~10-20x reduction** in copy overhead for status change notifications
- No allocation/deallocation during status reporting loop

---

## Risks and Mitigations

| Risk | Mitigation |
|------|------------|
| External code accesses `deviceStatus` member | Compile error directs to use `deviceTypeIndex` directly |
| External code creates `BusAddrStatus` with full DeviceStatus | Not supported in current API anyway (constructor only takes deviceTypeIndex) |
| Missing include for BusAddrRecord in internal code | Compile error; easy fix |
| Tests reference removed fields | Update tests to use new field paths |

---

## Verification Checklist

- [ ] Build RaftCore with new structures
- [ ] Build RaftI2C with BusAddrRecord
- [ ] Build RoboticalAxiom1 project
- [ ] Run unit tests (`test_bus_i2c.cpp`)
- [ ] Test device connect/disconnect notifications
- [ ] Verify BLE device detection still works
- [ ] Verify device status change callbacks fire correctly
