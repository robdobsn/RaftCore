# Device Cleanup Implementation Plan

## Overview

This document outlines the design for implementing automatic cleanup of dynamically created devices when they go offline. When a device disconnects, it is immediately marked for deletion and cannot come back online. If activity is detected on the slot again, a full device identification and initialization cycle is required.

### Design Rationale

When a device goes offline, it is impossible to determine:
1. Whether the same device will be reattached, or a different one
2. Whether power was removed from the device (requiring reinitialization)

Therefore, the only safe approach is to treat any disconnection as permanent. If a device appears on the same address later, it must go through full identification regardless of whether it's "the same" device or not.

---

## Current Device Lifecycle

### Device Creation

Devices are created in two ways:

1. **Static devices** - Configured in the SysType configuration JSON and created during `DeviceManager::setupDevices()` at startup. These devices have `RaftDeviceID` with bus number `BUS_NUM_DIRECT_CONN` (0).

2. **Dynamic bus devices** - Created automatically when a new device is detected on a bus (e.g., I2C). Created in `DeviceManager::busElemStatusCB()` when `el.isNewlyIdentified && el.deviceStatus.isValid()`. These are `RaftBusDevice` instances with a bus number >= `BUS_NUM_FIRST_BUS` (1).

### Device Storage

Devices are stored in `DeviceManager::_deviceList` as a `std::list<DeviceListRecord>`:

```cpp
struct DeviceListRecord
{
    RaftDevice* pDevice = nullptr;
    bool isOnline = false;
};
std::list<DeviceListRecord> _deviceList;
```

### Online/Offline State Tracking

The online state is tracked in two places:

1. **DeviceManager** - `DeviceListRecord::isOnline` flag updated in `busElemStatusCB()` when status changes
2. **BusStatusMgr** (per-bus) - `BusAddrStatus::onlineState` enum (`INITIAL`, `ONLINE`, `OFFLINE`)

State transitions are managed by `BusAddrStatus::handleResponding()` with configurable thresholds:
- `ADDR_RESP_COUNT_OK_MAX_DEFAULT = 2` - responses before declaring online
- `ADDR_RESP_COUNT_FAIL_MAX_DEFAULT = 3` - failures before declaring offline

### Current Limitations

1. **No device removal mechanism** - Once created, devices persist in `_deviceList` indefinitely
2. **No cleanup implementation** - No way to remove stale devices
3. **Memory growth** - Long-running systems accumulate "ghost" device entries
4. **Reconnection ambiguity** - No way to distinguish same device reconnecting vs. different device

---

## Proposed Implementation

### 1. Data Structure Changes

#### 1.1 Add Enumerations

Add enumerations to improve code clarity and reduce magic numbers:

```cpp
/// @brief Device publish status for subscriber notifications
enum class DevicePublishStatus : uint8_t
{
    OFFLINE = 0,    // Device is offline but may come back (static devices)
    ONLINE = 1,     // Device is online and responding
    DELETED = 2     // Device has been removed and will not return
};

/// @brief How the device was created
enum class DeviceCreationType : uint8_t
{
    STATIC = 0,     // Created from SysType.json configuration
    DYNAMIC = 1     // Created dynamically when detected on bus
};
```

#### 1.2 Rename and Modify DeviceListRecord (IMPLEMENTED)

Renamed `DevicePtrAndOnline` to `DeviceListRecord` and modified the struct in [DeviceManager.h](../raftdevlibs/RaftCore/components/core/DeviceManager/DeviceManager.h#L87-L92):

```cpp
struct DeviceListRecord
{
    RaftDevice* pDevice = nullptr;
    bool isOnline = false;
    bool pendingDeletion = false;
    DeviceCreationType creationType = DeviceCreationType::DYNAMIC;

    // Constructor for clarity
    DeviceListRecord(RaftDevice* pDev, bool online, DeviceCreationType createType) :
        pDevice(pDev), isOnline(online), pendingDeletion(false), 
        creationType(createType) {}
};

// Reader count tracks how many code paths are currently iterating over device list
// Cleanup is deferred while readers are active
mutable uint32_t _deviceListReaderCount = 0;
```

**Note:** Instead of per-device `refCount`, we use a single class-level `_deviceListReaderCount` that tracks how many code paths are currently iterating over the frozen device list. This is simpler and avoids the complexity of tracking which specific devices are referenced.

#### 1.2 Add Configuration Parameters

Add to `DeviceManager` class:

```cpp
// Cleanup configuration
bool _cleanupEnabled = true;               // Enable/disable cleanup feature
uint32_t _cleanupCheckIntervalMs = 1000;   // How often to check for devices to delete
uint32_t _lastCleanupCheckMs = 0;          // Last time cleanup was performed
```

Parse from configuration in `setup()`:

```cpp
_cleanupEnabled = modConfig().getBool("cleanupEnabled", true);
_cleanupCheckIntervalMs = modConfig().getLong("cleanupCheckIntervalMs", 1000);
```

#### Configuration in SysType.json

The cleanup parameters can be added to the `DevMan` section of the SysType configuration file:

**File:** `systypes/<BoardType>/SysType.json`

```json
{
    "SysMods": {
        "DevMan": {
            "Buses": [...],
            "Devices": [...],
            "cleanupEnabled": true,
            "cleanupCheckIntervalMs": 1000
        }
    }
}
```

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `cleanupEnabled` | bool | true | Enable automatic cleanup of offline dynamic devices |
| `cleanupCheckIntervalMs` | uint32 | 1000 | How often (ms) to check for devices to clean up |

### 2. Marking Devices for Deletion

#### 2.1 Update busElemStatusCB()

Modify the status update section in [DeviceManager.cpp](../raftdevlibs/RaftCore/components/core/DeviceManager/DeviceManager.cpp#L287-L306) to mark devices for deletion immediately when they go offline:

```cpp
// Update online status in device list
if (el.isChange)
{
    if (RaftMutex_lock(_accessMutex, 5))
    {
        for (auto& devRec : _deviceList)
        {
            if (devRec.pDevice == pDevice)
            {
                bool wasOnline = devRec.isOnline;
                bool nowOnline = (el.onlineState == DeviceOnlineState::ONLINE);
                devRec.isOnline = nowOnline;
                
                // Mark dynamic devices for deletion when they go offline
                if (wasOnline && !nowOnline && 
                    devRec.creationType == DeviceCreationType::DYNAMIC && 
                    _cleanupEnabled)
                {
                    devRec.markedForDeletion = true;
                    LOG_I(MODULE_PREFIX, "busElemStatusCB device %s marked for deletion (went offline)",
                          devRec.pDevice->getDeviceID().toString().c_str());
                }
                break;
            }
        }
        RaftMutex_unlock(_accessMutex);
    }
}
```

**Key behavior:** When a dynamic device transitions from ONLINE to OFFLINE, it is immediately marked for deletion. There is no path back to ONLINE - the device will be cleaned up and any future activity on that address will trigger a new device identification cycle.

#### 2.2 Mark Device Creation Type

When creating devices in `busElemStatusCB()`, use the constructor with `DeviceCreationType::DYNAMIC`:

```cpp
_deviceList.push_back(DeviceListRecord(pDevice, el.onlineState == DeviceOnlineState::ONLINE, DeviceCreationType::DYNAMIC));
```

For static devices in `setupDevices()`:
```cpp
_deviceList.push_back(DeviceListRecord(pDevice, true, DeviceCreationType::STATIC));
```

Static devices (`DeviceCreationType::STATIC`) are never marked for deletion.

### 3. Thread Safety: Reader Counter Approach

#### 3.1 The Frozen List Problem

The existing `getDeviceListFrozen()` pattern creates a snapshot of raw pointers for iteration outside the mutex:

```cpp
uint32_t DeviceManager::getDeviceListFrozen(RaftDevice** pDevices, uint32_t maxDevices, ...) const
{
    if (!RaftMutex_lock(_accessMutex, 5))
        return 0;
    // ... copy pointers to pDevices array ...
    RaftMutex_unlock(_accessMutex);
    return numDevices;  // Caller iterates over these pointers WITHOUT holding the lock
}
```

This creates a **use-after-free hazard**: if cleanup deletes a device while another thread is iterating over the frozen list, the pointers become dangling.

#### 3.2 Reader Counter Solution (IMPLEMENTED)

Instead of per-device reference counting, we use a simple class-level reader counter:

```cpp
// In DeviceManager.h
mutable uint32_t _deviceListReaderCount = 0;
```

Modify `getDeviceListFrozen()` to increment the reader count once per call:

```cpp
uint32_t DeviceManager::getDeviceListFrozen(RaftDevice** pDevices, uint32_t maxDevices, 
                                             bool onlyOnline, bool* pDeviceOnlineArray) const
{
    if (!RaftMutex_lock(_accessMutex, 5))
        return 0;
    
    // Increment reader count to prevent cleanup while we're iterating
    _deviceListReaderCount++;
    
    uint32_t numDevices = 0;
    for (auto& devRec : _deviceList)
    {
        if (numDevices >= maxDevices)
            break;
        // Skip devices pending deletion unless we need to report them
        if (devRec.pendingDeletion && onlyOnline)
            continue;
        if (pDeviceOnlineArray)
            pDeviceOnlineArray[numDevices] = devRec.isOnline;
        if (devRec.pDevice && (!onlyOnline || devRec.isOnline))
        {
            pDevices[numDevices++] = devRec.pDevice;
        }
    }
    RaftMutex_unlock(_accessMutex);
    return numDevices;
}
```

Add `releaseDeviceListFrozen()` to decrement the reader count:

```cpp
void DeviceManager::releaseDeviceListFrozen() const
{
    if (!RaftMutex_lock(_accessMutex, 5))
        return;
    
    // Decrement reader count
    if (_deviceListReaderCount > 0)
        _deviceListReaderCount--;
    
    RaftMutex_unlock(_accessMutex);
}
```

#### 3.3 Update All Call Sites (DONE)

All existing uses of `getDeviceListFrozen()` have been updated to call `releaseDeviceListFrozen()` when done:

**Updated call sites:**
- `DeviceManager::loop()` - releases after device iteration and inside debug block
- `DeviceManager::postSetup()` - releases after device registration
- `getDevicesDataJSON()` - releases before all return paths
- `getDevicesDataBinary()` - releases before return
- `getDevicesHash()` - releases after hash computation
- `getDebugJSON()` - releases before return

Example pattern:
```cpp
RaftDevice* pDeviceListCopy[DEVICE_LIST_MAX_SIZE];
uint32_t numDevices = getDeviceListFrozen(pDeviceListCopy, DEVICE_LIST_MAX_SIZE, true);

// ... iterate over devices ...

releaseDeviceListFrozen();  // No parameters needed - just decrements the counter
```

### 4. Cleanup Implementation (IMPLEMENTED)

#### 4.1 cleanupOfflineDevices Method

The cleanup method is called from `loop()` and checks the reader counter before deleting:

```cpp
void DeviceManager::cleanupOfflineDevices()
{
    if (!RaftMutex_lock(_accessMutex, 5))
        return;
    
    // Don't cleanup if any readers are active
    if (_deviceListReaderCount > 0)
    {
#ifdef DEBUG_DEVICE_DELETION
        LOG_I(MODULE_PREFIX, "cleanupOfflineDevices SKIPPED readerCount %d", _deviceListReaderCount);
#endif
        RaftMutex_unlock(_accessMutex);
        return;
    }
    
    // Find and delete devices that are pending deletion
    auto it = _deviceList.begin();
    while (it != _deviceList.end())
    {
        if (it->pendingDeletion && it->creationType == DeviceCreationType::DYNAMIC)
        {
#ifdef DEBUG_DEVICE_DELETION
            LOG_I(MODULE_PREFIX, "cleanupOfflineDevices DELETING device %s",
                    it->pDevice ? it->pDevice->getDeviceID().toString().c_str() : "NULL");
#endif
            // Delete the device
            delete it->pDevice;
            it = _deviceList.erase(it);
        }
        else
        {
            ++it;
        }
    }
    
    RaftMutex_unlock(_accessMutex);
}
```

The cleanup is integrated into `DeviceManager::loop()` where it runs before getting the frozen device list:

```cpp
void DeviceManager::loop()
{
    // Service the buses
    raftBusSystem.loop();

    // Cleanup any devices that are pending deletion with zero reference count
    cleanupOfflineDevices();

    // Get a frozen copy of the device list for online devices
    RaftDevice* pDeviceListCopy[DEVICE_LIST_MAX_SIZE];
    uint32_t numDevices = getDeviceListFrozen(pDeviceListCopy, DEVICE_LIST_MAX_SIZE, true);

    // Loop through the devices
    for (uint32_t devIdx = 0; devIdx < numDevices; devIdx++)
    {
        pDeviceListCopy[devIdx]->loop();
    }
    
    // ... debug block if enabled ...
    
    releaseDeviceListFrozen();
}
```

### 5. Bus-Level Behavior

When a device is marked for deletion and cleaned up, the `BusStatusMgr` still maintains its `BusAddrStatus` record at that address. This is **intentional and essential** for the new device identification cycle:

1. Device at address 0x25 goes offline → marked for deletion → cleaned up
2. BusStatusMgr continues polling address 0x25
3. When a device responds at 0x25 again:
   - `BusAddrStatus` transitions back to identification pending
   - Full device identification runs (type detection, capability query, etc.)
   - New `RaftBusDevice` object created with appropriate device type
   - `busElemStatusCB()` called with `isNewlyIdentified = true`

This ensures that even if the "same" device reconnects, it goes through proper initialization, which handles cases where:
- The device lost power and needs reinitialization
- A different device was connected to the same slot
- The device firmware was updated while disconnected

### 6. Subscriber Notification

External subscribers (web clients, mobile apps) receive device data via the publish mechanism. When a device is marked for deletion, the status is embedded directly in the device's regular publication message using the `DevicePublishStatus` enumeration.

#### 6.1 DevicePublishStatus Enum

The `_o` field in JSON (and corresponding bits in binary) uses the `DevicePublishStatus` enum:

| Value | Enum | Description |
|-------|------|-------------|
| 0 | `OFFLINE` | Device is offline but may come back (static devices only) |
| 1 | `ONLINE` | Device is online and responding |
| 2 | `DELETED` | Device has been removed and will not return |

When a device is marked for deletion:
1. The device remains in the device list (with `markedForDeletion = true`)
2. On the next publication cycle, the device record includes `_o: 2` (DELETED)
3. After publication, the device can be cleaned up (when `refCount == 0`)

This approach:
- Uses the existing `_o` field without adding new fields
- Guarantees subscribers see the deletion notification (it's part of normal device data)
- Simplifies the implementation (no separate notification queue needed)

#### 6.2 Binary Publication

The `RaftDevice::genBinaryDataMsg()` method includes a `pendingDeletion` parameter:

```cpp
static bool genBinaryDataMsg(std::vector<uint8_t>& binData, 
    uint8_t busNumber, 
    BusElemAddrType address, 
    uint16_t deviceTypeIndex, 
    bool isOnline, 
    std::vector<uint8_t> deviceMsgData,
    bool pendingDeletion = false);
```

The status/bus byte encoding:
```
Status/Bus byte (byte 2 of each device record):
  bit 7: online (1) / offline (0)
  bit 6: pendingDeletion (1) / active (0)
  bits 0-5: bus number (0-63)
```

When publishing device data in `getDevicesDataBinary()`, pass the device's `markedForDeletion` flag:

```cpp
RaftDevice::genBinaryDataMsg(binData, 
    busNumber,
    address,
    deviceTypeIndex,
    devRec.isOnline,
    deviceData,
    devRec.markedForDeletion);  // pendingDeletion flag
```

**Note:** This is a breaking change for binary subscribers - they must now mask bits 0-5 for the bus number instead of bits 0-6.

#### 6.3 JSON Publication

The existing `_o` field is extended to include the deleted state. Modify `DeviceTypeRecords::deviceStatusToJson()`:

```cpp
// Determine publish status
DevicePublishStatus publishStatus = DevicePublishStatus::OFFLINE;
if (markedForDeletion)
    publishStatus = DevicePublishStatus::DELETED;
else if (isOnline)
    publishStatus = DevicePublishStatus::ONLINE;

// Use enum value in JSON
return "\"" + String(addr, 16) + "\":{\"x\":\"" + hexOut + 
       "\",\"_o\":" + String(static_cast<uint8_t>(publishStatus)) + 
       ",\"_i\":" + String(deviceTypeIndex) + "}";
```

Example JSON output:
```json
{
    "_t": 0,
    "_v": 1,
    "1": {
        "25": {"x": "0A1B2C", "_o": 1, "_i": 42},
        "30": {"x": "", "_o": 2, "_i": 15}
    }
}
```
In this example, device at address 0x25 is online (`_o: 1`), while device at 0x30 is deleted (`_o: 2`).

#### 6.4 Subscriber Handling

**Binary subscribers** should parse the status/bus byte and handle the deletion flag:

```javascript
function parseDeviceRecord(data, offset) {
    const statusBusByte = data[offset + 2];  // After 2-byte length
    const isOnline = (statusBusByte & 0x80) !== 0;
    const pendingDeletion = (statusBusByte & 0x40) !== 0;
    const busNumber = statusBusByte & 0x3F;
    
    if (pendingDeletion) {
        // Device has been permanently removed - remove from local state
        removeDeviceFromLocalState(deviceId);
        return;
    }
    // ... normal device update handling ...
}
```

**JSON subscribers** should check the `_o` field value:

```javascript
const DevicePublishStatus = {
    OFFLINE: 0,
    ONLINE: 1,
    DELETED: 2
};

function handleDeviceUpdate(deviceId, data) {
    if (data._o === DevicePublishStatus.DELETED) {
        // Device has been permanently removed - remove from local state
        removeDeviceFromLocalState(deviceId);
        return;
    }
    
    const isOnline = (data._o === DevicePublishStatus.ONLINE);
    // ... normal device update handling ...
}
```

#### 6.5 Publication Timing

The deletion status is published as part of the device's final message before cleanup:

1. Device goes offline → `markedForDeletion = true` set in `busElemStatusCB()`
2. Next publication cycle → device record includes `_o: 2` (DELETED)
3. After publication → `cleanupOfflineDevices()` removes the device (when `refCount == 0`)

This ensures subscribers always receive the deletion notification before the device is removed from the system.

---

## Device Lifecycle State Machine

```
                                    ┌─────────────────────────────────────────┐
                                    │         New activity detected           │
                                    │         (full identification cycle)     │
                                    │                                         │
                                    ▼                                         │
    ┌──────────┐  identified  ┌──────────┐  responds   ┌──────────┐         │
    │ UNKNOWN  │─────────────▶│ INITIAL  │────────────▶│  ONLINE  │         │
    └──────────┘              └──────────┘             └──────────┘         │
                                   │                        │                │
                         fails     │                        │ stops          │
                         threshold │                        │ responding     │
                                   ▼                        ▼                │
                              ┌──────────┐            ┌──────────┐          │
                              │ SPURIOUS │            │ DELETED  │──────────┘
                              │(no device │            │(immediate)│
                              │ created)  │            └──────────┘
                              └──────────┘                  │
                                                           │ refCount == 0
                                                           ▼
                                                      ┌──────────┐
                                                      │ CLEANED  │
                                                      │   UP     │
                                                      └──────────┘
```

**Key difference from previous design:** There is no OFFLINE state that can transition back to ONLINE. When a device stops responding, it immediately transitions to DELETED.

---

## Implementation Checklist

### Phase 1: Data Structures
- [x] Add `DevicePublishStatus` and `DeviceCreationType` enumerations (DONE)
- [x] Rename `DevicePtrAndOnline` to `DeviceListRecord` with new fields (`creationType`, `pendingDeletion`) (DONE)
- [x] Add constructor to `DeviceListRecord` for clarity (DONE)
- [x] Add reader counter `_deviceListReaderCount` to DeviceManager (DONE - simpler than per-device refCount)
- [ ] Add configuration member variables to `DeviceManager`
- [ ] Parse configuration from SysType.json in `setup()`

### Phase 2: Thread-Safe Device Access
- [x] Modify `getDeviceListFrozen()` to increment reader count (DONE)
- [x] Add `releaseDeviceListFrozen()` method to decrement reader count (DONE)
- [x] Update all 7 call sites to use acquire/release pattern (DONE)

### Phase 3: Deletion Marking
- [ ] Update `busElemStatusCB()` to mark devices for deletion when they go offline
- [ ] Update device creation to use `DeviceCreationType` enum appropriately

### Phase 4: Cleanup Logic
- [x] Implement `cleanupOfflineDevices()` (only deletes when `_deviceListReaderCount == 0`) (DONE)
- [ ] Implement `cleanupDeviceCallbacks()` helper
- [x] Add cleanup check to `loop()` (DONE)

### Phase 5: Subscriber Notification
- [x] Add `pendingDeletion` parameter to `RaftDevice::genBinaryDataMsg()` (DONE)
- [x] Modify `DeviceTypeRecords::deviceStatusToJson()` to use `DevicePublishStatus` enum for `_o` field (DONE)
- [ ] Modify `getDevicesDataBinary()` to pass `pendingDeletion` to `genBinaryDataMsg()`
- [ ] Update subscriber documentation for new `_o` values (0=offline, 1=online, 2=deleted)

### Phase 6: Testing
- [ ] Unit test: Device goes offline, immediately marked for deletion
- [ ] Unit test: Static devices never marked for deletion
- [ ] Unit test: Frozen list iteration during cleanup (no crash)
- [ ] Unit test: Deletion notification published before device removed (`_o: 2`)
- [ ] Unit test: Device reconnects → new device created (not reusing old)
- [ ] Integration test: Multiple buses with mixed device types

### Debug Support
- [x] Add `DEBUG_DEVICE_DELETION` flag for logging reader count and deletion events (DONE)

---

## Files Modified

| File | Changes |
|------|---------|
| [RaftDevice.h](../raftdevlibs/RaftCore/components/core/RaftDevice/RaftDevice.h) | Add `pendingDeletion` parameter to `genBinaryDataMsg()` (DONE) |
| [RaftDevice.cpp](../raftdevlibs/RaftCore/components/core/RaftDevice/RaftDevice.cpp) | Implement `pendingDeletion` bit encoding (DONE) |
| [DeviceManager.h](../raftdevlibs/RaftCore/components/core/DeviceManager/DeviceManager.h) | Add enums, rename struct to `DeviceListRecord`, add `_deviceListReaderCount` (DONE) |
| [DeviceManager.cpp](../raftdevlibs/RaftCore/components/core/DeviceManager/DeviceManager.cpp) | Implement reader counter pattern, cleanup logic, update all call sites (DONE) |
| [DeviceTypeRecords.cpp](../raftdevlibs/RaftCore/components/core/DeviceTypes/DeviceTypeRecords.cpp) | Modify `deviceStatusToJson()` to use `DevicePublishStatus` enum for `_o` field (DONE) |
| SysType.json | Add `cleanupEnabled` and `cleanupCheckIntervalMs` to DevMan config (TODO) |

---

## Risks and Mitigations

| Risk | Mitigation |
|------|------------|
| Cleanup during active device iteration | Reader counter prevents deletion until `_deviceListReaderCount == 0` |
| Callback invoked on deleted device | Clean up callback registrations before deletion |
| Memory fragmentation | Consider object pooling (future enhancement) |
| Subscriber state stale after deletion | Send deletion notification before cleanup |
| Deletion notification lost | Subscribers should defensively handle device disappearing |

---

## Summary

This design takes a conservative approach: **any device disconnection is treated as permanent**. This avoids the complexity and ambiguity of trying to determine whether a reconnecting device is "the same" and whether it needs reinitialization.

The workflow becomes:
1. Device disconnects → immediately marked for deletion
2. Deletion notification published to subscribers
3. Device cleaned up when no active references exist
4. If activity detected at that address → full identification cycle → new device object created

This ensures devices are always properly initialized and subscribers always have accurate state.
