# Dynamic I2C Bus Speed Per Device

## Status

Implemented.

This document reflects the current implementation, including the later fixes that were needed after the first working version.

## Goal

Allow individual devices to override the I2C bus speed for polling, while keeping the configured bus default speed for normal operation and for devices that do not request an override.

The implementation supports:

- device-type JSON poll-time bus speed override
- optional slot filtering for that override
- runtime override through `/devman/devconfig`
- restoring the previous bus speed after the poll finishes
- using the currently applied bus speed for software timeout calculations

## Current Behaviour

For a device poll:

1. The device's poll definition is read from `pollInfo`.
2. If a poll-specific bus speed is configured, and the current slot matches the optional slot mask, the I2C controller is switched to that frequency before the poll starts.
3. All poll requests for that device are executed.
4. The bus speed is restored to the previously applied speed.

If no poll-specific speed is configured, or if the device is on a slot outside the configured slot mask, polling runs at the normal bus speed.

## JSON Format

### Device Type JSON `pollInfo`

Supported keys:

| Key | Meaning | Default |
|-----|---------|---------|
| `c` | Poll command(s) | required |
| `i` | Poll interval in ms | 0 |
| `s` | Number of poll results to store | 0 |
| `h` | Poll-time bus speed in Hz | 0 |
| `busHz` | Verbose equivalent of `h` | 0 |
| `hSlots` | Array of slots where `h` applies | all slots |
| `busHzSlots` | Verbose equivalent of `hSlots` | all slots |

Compact and verbose forms are both accepted.

Example:

```json
"pollInfo": {
    "c": "=r7",
    "i": 100,
    "s": 1,
    "h": 400000,
    "hSlots": [4, 5, 6]
}
```

Meaning:

- poll at 100 ms interval
- store one result
- switch to 400 kHz only when the device instance is on slot 4, 5, or 6
- use the bus default speed on any other slot

## REST API

The runtime override is available through:

```text
GET /devman/devconfig?deviceid=<deviceId>&busHz=<hz>&busHzSlots=<csv>
```

Examples:

```text
GET /devman/devconfig?deviceid=1_6a&busHz=400000
GET /devman/devconfig?deviceid=1_6a&busHz=400000&busHzSlots=4,5,6
```

Behaviour:

- `busHz` sets the poll-time bus speed override
- `busHzSlots` limits where that override applies
- if `busHzSlots` is omitted, the override applies on all slots

## Data Flow

```text
Device type JSON / REST API
        |
        v
DevicePollingInfo.pollBusHz
DevicePollingInfo.pollBusHzSlotMask
        |
        v
BusStatusMgr stores per-device poll settings
        |
        v
DevicePollingMgr::taskService()
        |
        +--> enableOneSlot(slot)
        +--> if slot matches mask: setBusFrequency(pollBusHz)
        +--> run poll requests
        +--> restore previous bus speed
        +--> disableAllSlots(false)
```

## Implementation Details

### `DevicePollingInfo`

Added fields:

- `uint32_t pollBusHz = 0;`
- `uint64_t pollBusHzSlotMask = 0;`

Mask semantics:

- `0` means all slots
- otherwise bit `N` means slot `N`

This is used because I2C slots are represented in a compact 0-63 range, so a `uint64_t` is the simplest storage format.

### `DeviceTypeRecords.cpp`

`getPollInfo()` now parses:

- `h`, falling back to `busHz`
- `hSlots`, falling back to `busHzSlots`

The slot array is converted to a `uint64_t` bitmask.

### `RaftBus`, `BusI2C`, `BusStatusMgr`

The bus abstraction now supports getting and setting:

- per-device poll bus speed
- per-device poll bus speed slot mask

`BusStatusMgr` stores both values in `deviceIdentPolling` under its mutex.

### `DevicePollingMgr`

`DevicePollingMgr` now has access to `RaftI2CCentralIF` and applies the override in the polling task.

Current logic:

```cpp
if (pollInfo.pollBusHz != 0 && _pI2CCentral &&
    (pollInfo.pollBusHzSlotMask == 0 ||
     (pollInfo.pollBusHzSlotMask & (1ULL << addrAndSlot.getSlotNum()))))
{
    savedBusFreqHz = _pI2CCentral->getBusFrequency();
    if (pollInfo.pollBusHz != savedBusFreqHz)
        speedChanged = _pI2CCentral->setBusFrequency(pollInfo.pollBusHz);
}
...
if (speedChanged && _pI2CCentral)
    _pI2CCentral->setBusFrequency(savedBusFreqHz);
```

So the override is only applied when:

- the device requested a non-zero poll bus speed
- there is an I2C central object available
- the current slot is allowed by the mask
- the requested speed differs from the currently applied speed

### `RaftI2CCentral`

`RaftI2CCentral` now distinguishes between:

- `_busFrequency`: configured default bus speed
- `_appliedBusFrequency`: speed currently programmed into the hardware

This distinction is required because:

- the bus may temporarily switch to a per-device poll speed
- bus recovery should still restore the configured default speed
- timeout calculations should use the currently applied hardware speed, not just the configured default

Current behaviour:

- `init()` sets `_busFrequency` from configuration and resets `_appliedBusFrequency` to `0`
- `reinitI2CModule()` restores the controller to `_busFrequency`
- `setBusFrequency()` returns early only if the requested speed already matches `_appliedBusFrequency`
- after programming the registers, `setBusFrequency()` updates `_appliedBusFrequency`
- `getBusFrequency()` returns `_appliedBusFrequency`
- `access()` uses `_appliedBusFrequency` for timeout calculation, falling back to `_busFrequency` before the first apply

## Important Implementation Lesson

An earlier attempt initialized `_appliedBusFrequency` to `100000`. That caused I2C startup failure.

Reason:

1. `init()` called `reinitI2CModule()`
2. `reinitI2CModule()` called `setBusFrequency(_busFrequency)`
3. The early-return logic saw the requested frequency already matched `_appliedBusFrequency`
4. Register programming was skipped on first init
5. The controller was left unconfigured

The fix is the current implementation:

- initialize `_appliedBusFrequency` to `0`
- let the first `setBusFrequency()` always program the hardware

## Why The Timeout Change Was Needed

Before the `RaftI2CCentral` follow-up fix, the polling code could switch the hardware to 400 kHz, but the software timeout calculation in `access()` still used `_busFrequency`, which remained at the default 100 kHz.

That meant the software timeout estimate could be based on the wrong speed.

The current implementation fixes that by basing timeout estimation on the applied speed.

## Files Updated

### RaftCore

- `components/core/DeviceTypes/DevicePollingInfo.h`
- `components/core/DeviceTypes/DeviceTypeRecords.cpp`
- `components/core/Bus/RaftBus.h`
- `components/core/DeviceManager/DeviceManager.cpp`

### RaftI2C

- `components/RaftI2C/I2CCentral/RaftI2CCentralIF.h`
- `components/RaftI2C/I2CCentral/RaftI2CCentral.h`
- `components/RaftI2C/I2CCentral/RaftI2CCentral.cpp`
- `components/RaftI2C/BusI2C/BusStatusMgr.h`
- `components/RaftI2C/BusI2C/BusStatusMgr.cpp`
- `components/RaftI2C/BusI2C/BusI2C.h`
- `components/RaftI2C/BusI2C/BusI2C.cpp`
- `components/RaftI2C/BusI2C/DevicePollingMgr.h`
- `components/RaftI2C/BusI2C/DevicePollingMgr.cpp`

## Verification Notes

Observed during development:

- poll-time speed override works when configured in device JSON
- slot filtering works as expected
- runtime override through `/devman/devconfig` works
- the first `_appliedBusFrequency` implementation broke bus init and was reverted
- the corrected implementation uses `_appliedBusFrequency = 0` at init and restores bus functionality

## Remaining Areas To Watch

- whether changing bus speed affects devices that are sensitive to timeout register changes at higher bus speeds
- whether main-loop latency warnings are caused by task scheduling side effects rather than direct I2C functional problems
- whether the current `vTaskDelay(0)` polling wait in `RaftI2CCentral::access()` should eventually be replaced with a blocking notification or semaphore design
