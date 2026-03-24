# Bus Device Tracking Refactor Plan

## Problem Statement

The current architecture has **two separate device tracking systems** for bus devices:

1. **`BusStatusMgr._addrStatus`** (in RaftI2C) - holds actual poll data, device type index, online state
2. **`DeviceManager._deviceList`** (in RaftCore) - holds `RaftBusDevice*` wrappers

This duplication causes issues:
- Devices that go offline persist in `_addrStatus` indefinitely (records only removed for "spurious" devices that were never online)
- `DeviceManager` creates `RaftBusDevice` objects that provide no actual data (their `getStatusJSON()` returns `"{}"`)
- Disconnected devices continue appearing in the app because `DeviceIdentMgr::getQueuedDeviceDataJson()` iterates ALL addresses in `_addrStatus`

## Design Decision: Immediate Deletion on Offline

When a bus device goes offline, it should be **immediately marked for deletion** with no recovery path.

**Rationale**: A device can be physically swapped for a different device type. If we allowed recovery when a device "comes back online," we might incorrectly associate new device data with stale device type information. Treating any reconnection as a completely new device ensures clean state.

## Proposed Architecture

### BusStatusMgr: Single Source of Truth

`BusStatusMgr._addrStatus` becomes the **authoritative** tracking for all bus devices.

**Current Behavior:**
- Records created when device first responds
- Records removed only for "spurious" devices (INITIAL → OFFLINE, never went ONLINE)
- Records for devices that were ever ONLINE persist forever

**New Behavior:**
- Records created when device first responds (unchanged)
- Records marked `pendingDeletion = true` when device goes ONLINE → OFFLINE
- Records deleted immediately after final status notification is sent
- No recovery path: if address responds again, create fresh record

### DeviceManager: Reduced Role for Bus Devices

**Current Behavior:**
- `busElemStatusCB()` creates `RaftBusDevice` objects in `_deviceList` when `isNewlyIdentified`
- `_deviceList` holds both static devices and bus device wrappers
- Cleanup logic removes entries with `DELETED` publish status

**New Behavior:**
- `busElemStatusCB()` **does NOT create** `RaftBusDevice` objects
- `_deviceList` holds **only static devices** (STATIC creation type)
- `busElemStatusCB()` continues forwarding status to application callbacks (retained for app notifications)
- Unified query interface via `RaftBusDevicesIF` remains unchanged

### Component Responsibilities After Refactor

| Component | Responsibility |
|-----------|---------------|
| `BusStatusMgr` | Owns lifecycle of bus device address records |
| `DeviceIdentMgr` | Device identification, data queueing, JSON formatting |
| `DeviceManager` | Static device management, app callback forwarding, unified query API |
| `RaftBusDevice` | **Candidate for deletion** - no longer needed |

## Implementation Plan

### Phase 1: Add Immediate Deletion to BusStatusMgr (COMPLETED)

**File: `BusAddrStatus.h`**

1. ✅ Added `pendingDeletion` flag to `BusAddrStatus` struct:
   ```cpp
   bool pendingDeletion : 1 = false;  // Marked for removal after OFFLINE notification
   ```

**File: `BusStatusMgr.cpp`**

2. ✅ In `updateBusElemState()`, when device goes ONLINE → OFFLINE:
   ```cpp
   // In handleResponding() or after it returns
   if (isNewStatusChange && pAddrStatus->onlineState == DeviceOnlineState::OFFLINE)
   {
       pAddrStatus->pendingDeletion = true;
   }
   ```

3. In `loop()`, after calling `_raftBus.callBusElemStatusCB(statusChanges)`:
   ```cpp
   // Delete records that were pending deletion and have been notified
   if (RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
   {
       _addrStatus.erase(
           std::remove_if(_addrStatus.begin(), _addrStatus.end(),
               [](const BusAddrStatus& s) { return s.pendingDeletion; }),
           _addrStatus.end());
       RaftMutex_unlock(_busElemStatusMutex);
   }
   ```

4. Modify recovery logic: If address responds again after being marked `pendingDeletion`, do NOT reuse the record. Let it be deleted, then a fresh record will be created on next response.

### Phase 2: Remove RaftBusDevice Creation from DeviceManager (COMPLETED)

**File: `DeviceManager.cpp`**

1. ✅ In `busElemStatusCB()`, removed the block that creates `RaftBusDevice`
2. ✅ Callback forwarding to application-level status handlers is retained

### Phase 3: Clean Up DeviceManager (COMPLETED)

Removed all dynamic device infrastructure from DeviceManager:

**File: `DeviceManager.h`**
1. ✅ Removed `DeviceCreationType` enum (all devices are now STATIC)
2. ✅ Removed `pendingDeletion` from `DeviceListRecord`
3. ✅ Removed `creationType` from `DeviceListRecord`
4. ✅ Removed `_deviceListReaderCount` (reader-counter pattern no longer needed)
5. ✅ Removed `releaseDeviceListFrozen()` declaration
6. ✅ Removed `cleanupOfflineDevices()` declaration

**File: `DeviceManager.cpp`**
1. ✅ Removed `cleanupOfflineDevices()` function
2. ✅ Removed `releaseDeviceListFrozen()` function
3. ✅ Removed all calls to `releaseDeviceListFrozen()` (7 call sites)
4. ✅ Removed call to `cleanupOfflineDevices()` in `loop()`
5. ✅ Simplified `getDeviceListFrozen()` - no reader count, no pendingDeletion check
6. ✅ Simplified `DeviceListRecord` constructor calls
7. ✅ Removed unused `DEBUG_DEVICE_DELETION` defines

### Phase 4: Consider Deleting RaftBusDevice Class

After refactor, `RaftBusDevice` may have no remaining use cases:
- Check all usages of `RaftBusDevice`
- If only used for bus device wrappers in `_deviceList`, delete the class
- If used elsewhere, document remaining uses

## Edge Cases

### Case 1: Device Disconnected While Data In Flight
- Device goes offline
- Record marked `pendingDeletion = true`
- Final OFFLINE notification sent via `callBusElemStatusCB()`
- Record deleted
- Any queued data for that address should be discarded or published with `_o: 0` (offline)

### Case 2: Device Reconnects Quickly
- Device goes offline → marked `pendingDeletion`
- Before `loop()` runs, device responds again
- **Current**: Would find existing record and potentially cause inconsistency
- **New**: Record is already marked for deletion; let it be deleted. Next response creates fresh record.
- This ensures no stale device type information persists

### Case 3: Device Swapped for Different Type
- Device A at address 0x50 goes offline → record deleted
- Device B (different type) connected at same address 0x50
- Device B responds → fresh record created
- Device B identified as new device type
- Clean identification with no confusion from Device A's data

### Case 4: Static Devices
- Not affected by this refactor
- Continue using `DeviceManager._deviceList` with full lifecycle management
- `DeviceCreationType::STATIC` distinguishes from bus devices

## Testing Checklist

- [ ] Device goes offline → removed from app within 1-2 poll cycles
- [ ] Device reconnects → appears as new device
- [ ] Different device at same address → correctly identified as new type
- [ ] Static devices → unchanged behavior
- [ ] No memory leaks from deleted records
- [ ] No crashes from null pointers after record deletion
- [ ] Status callbacks fire correctly for OFFLINE transition
- [ ] `getQueuedDeviceDataJson()` excludes deleted addresses

## Files to Modify

| File | Changes |
|------|---------|
| `BusStatusMgr.h` | Add `pendingDeletion` to `BusAddrStatus` |
| `BusStatusMgr.cpp` | Mark for deletion on OFFLINE, delete after notification |
| `BusAddrStatus.h` | If struct defined separately, add flag there |
| `DeviceManager.cpp` | Remove `RaftBusDevice` creation in `busElemStatusCB()` |
| `RaftBusDevice.h/cpp` | Potentially delete entirely |

## Rollback Plan

If issues arise:
1. Revert `pendingDeletion` logic in `BusStatusMgr`
2. Restore `RaftBusDevice` creation in `DeviceManager`
3. Return to previous behavior (devices persist in `_addrStatus`)

## Success Criteria

1. Disconnected bus devices disappear from app promptly
2. Device swapping works correctly (new device identified fresh)
3. No regression in static device functionality
4. Memory usage stable (no accumulating stale records)
5. Build passes with no new warnings
