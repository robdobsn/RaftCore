# Device Status Subscriber Protocol

This document describes the wire protocol for device status messages sent from Raft firmware to client subscribers (e.g., raftjs `RaftDeviceManager`).

## Overview

Device status data is published via the subscription mechanism in two formats:
- **JSON** - Human-readable format for debugging and simple integrations
- **Binary** - Compact format for efficient transmission

Both formats support three device states:
| State | Description |
|-------|-------------|
| Online | Device is currently responding on the bus |
| Offline | Device was previously online but is no longer responding |
| Pending Deletion | Device is offline and marked for removal (will not return) |

## JSON Format

### Envelope Structure

```json
{
  "_t": <topicIndex>,
  "_v": 1,
  "<busNum>": {
    "<address>": {
      "x": "<hexData>",
      "_o": <onlineState>,
      "_i": <deviceTypeIndex>
    }
  }
}
```

### Fields

| Field | Type | Description |
|-------|------|-------------|
| `_t` | integer | Topic index for the subscription |
| `_v` | integer | Protocol version (currently `1`) |
| `<busNum>` | string | Bus number as string (e.g., `"1"` for I2C, `"0"` for direct-connected devices) |
| `<address>` | string | Device address in hex (e.g., `"48"` for I2C address 0x48) |
| `x` | string | Hex-encoded poll response data from the device |
| `_o` | integer | **Online state** (see below) |
| `_i` | integer | Device type index (for looking up device type info) |

### Online State (`_o`) Values

| Value | State | Description |
|-------|-------|-------------|
| `0` | Offline | Device is not responding (was previously online) |
| `1` | Online | Device is currently responding |
| `2` | Pending Deletion | Device is offline and will be removed from tracking |

### Client Handling for `_o`

```typescript
switch (onlineState) {
  case 0:
    // Device offline - may come back online later
    // Keep device in UI but mark as unavailable
    break;
  case 1:
    // Device online - normal operation
    // Process poll data from "x" field
    break;
  case 2:
    // Pending deletion - device will not return
    // Remove device from UI/tracking
    // This is the final message for this device
    break;
}
```

## Binary Format

### Envelope Header

| Offset | Size | Description |
|--------|------|-------------|
| 0 | 1 byte | Magic + version: `0xDB` (devbin v1) |
| 1 | 1 byte | Topic index (0-254, or 0xFF for index > 254) |

### Device Entry Structure

Each device entry follows this format:

| Offset | Size | Description |
|--------|------|-------------|
| 0-1 | 2 bytes | Message length (big-endian, excludes these 2 bytes) |
| 2 | 1 byte | Status/bus byte (see below) |
| 3-6 | 4 bytes | Device address (big-endian, 32-bit) |
| 7-8 | 2 bytes | Device type index (big-endian, 16-bit) |
| 9+ | N bytes | Poll response data |

### Status/Bus Byte (Offset 2)

```
Bit 7 (0x80): Online flag
  - 1 = Device is online
  - 0 = Device is offline or pending deletion

Bit 6 (0x40): Pending deletion flag
  - 1 = Device is pending deletion (will be removed)
  - 0 = Normal state

Bits 5-4: Reserved (should be 0)

Bits 3-0 (0x0F): Bus number (0-15)
```

### Decoding Online State from Binary

```typescript
const statusByte = data[offset + 2];
const busNumber = statusByte & 0x0F;
const isOnline = (statusByte & 0x80) !== 0;
const isPendingDeletion = (statusByte & 0x40) !== 0;

if (isPendingDeletion) {
  // Device will be removed - clean up from tracking
  onlineState = 2;
} else if (isOnline) {
  // Device responding normally
  onlineState = 1;
} else {
  // Device offline but may return
  onlineState = 0;
}
```

### Binary Parsing Example

```typescript
function parseDeviceEntry(data: Uint8Array, offset: number): DeviceEntry | null {
  if (offset + 2 > data.length) return null;
  
  // Message length (big-endian)
  const msgLen = (data[offset] << 8) | data[offset + 1];
  if (offset + 2 + msgLen > data.length) return null;
  
  // Status/bus byte
  const statusByte = data[offset + 2];
  const busNumber = statusByte & 0x0F;
  const isOnline = (statusByte & 0x80) !== 0;
  const isPendingDeletion = (statusByte & 0x40) !== 0;
  
  // Address (32-bit big-endian)
  const address = (data[offset + 3] << 24) | 
                  (data[offset + 4] << 16) | 
                  (data[offset + 5] << 8) | 
                  data[offset + 6];
  
  // Device type index (16-bit big-endian)
  const deviceTypeIndex = (data[offset + 7] << 8) | data[offset + 8];
  
  // Poll data
  const pollData = data.slice(offset + 9, offset + 2 + msgLen);
  
  return {
    busNumber,
    address,
    deviceTypeIndex,
    isOnline,
    isPendingDeletion,
    pollData,
    nextOffset: offset + 2 + msgLen
  };
}
```

## Migration Notes for raftjs

### Changes Required in `RaftDeviceManager.ts`

#### 1. Update `handleClientMsgJson`

The `_o` field now supports three values instead of two:

```typescript
// Before:
const isOnline = deviceData._o === 1;

// After:
const onlineState = deviceData._o;
if (onlineState === 2) {
  // Pending deletion - remove device from tracking
  this.removeDevice(busNum, address);
} else {
  const isOnline = onlineState === 1;
  // ... existing handling
}
```

#### 2. Update `handleClientBinaryMsg`

Add handling for the pending deletion flag:

```typescript
// Before:
const isOnline = (statusByte & 0x80) !== 0;

// After:
const isOnline = (statusByte & 0x80) !== 0;
const isPendingDeletion = (statusByte & 0x40) !== 0;

if (isPendingDeletion) {
  // Remove device from tracking
  this.removeDevice(busNumber, address);
  continue; // Skip further processing for this device
}
```

#### 3. Implement Device Removal

Add a method to cleanly remove devices when pending deletion is received:

```typescript
private removeDevice(busNum: number, address: number): void {
  const key = `${busNum}:${address}`;
  // Remove from device map
  this.deviceMap.delete(key);
  // Notify listeners of device removal
  this.emit('deviceRemoved', { busNum, address });
}
```

## State Transition Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                                                                 │
│  ┌─────────┐     responds      ┌────────┐                      │
│  │ INITIAL │ ─────────────────►│ ONLINE │◄─────────────┐       │
│  └─────────┘                   └────────┘              │       │
│       │                            │                   │       │
│       │ no response                │ no response       │       │
│       │                            │ (timeout)         │       │
│       ▼                            ▼                   │       │
│  ┌─────────────┐              ┌─────────┐              │       │
│  │ (discarded) │              │ OFFLINE │──────────────┘       │
│  └─────────────┘              └─────────┘   responds           │
│                                    │                           │
│                                    │ cleanup timer             │
│                                    ▼                           │
│                          ┌──────────────────┐                  │
│                          │ PENDING_DELETION │                  │
│                          └──────────────────┘                  │
│                                    │                           │
│                                    │ published to clients      │
│                                    │ then removed              │
│                                    ▼                           │
│                             ┌──────────┐                       │
│                             │ (deleted)│                       │
│                             └──────────┘                       │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

## Important Notes

1. **Final Message**: When `_o=2` (or `isPendingDeletion=true`), this is the **final message** for that device. The device will be removed from firmware tracking and no further messages will be sent.

2. **Guaranteed Delivery ("Last Will")**: The firmware uses a queued deletion notice mechanism to ensure deletion messages are delivered. When a device transitions to `PENDING_DELETION`:
   - A deletion notice is queued containing the device's address and type index
   - The device record is immediately removed from the active tracking list
   - On the next `getQueuedDeviceData*` call, queued deletion notices are included in the output with empty poll data and the `PENDING_DELETION` state
   - The queue is cleared after being read
   
   This ensures the deletion message is always sent, regardless of timing between device removal and publisher polling.

3. **Cleanup Timing**: The firmware sends the pending deletion message before removing the device, giving clients time to update their UI and clean up resources.

4. **Re-connection**: If a device with the same address comes back online after deletion, it will be treated as a new device and will go through the identification process again.

5. **Bus 0**: Direct-connected devices (not on a bus) use bus number `0` (`BUS_NUM_DIRECT_CONN`).

6. **Empty Poll Data**: Deletion messages have empty poll data (`x: ""` in JSON, 0 bytes in binary) since the device is no longer responding.
