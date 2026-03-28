# Device Sampling Rate Control — Design Document

## Executive Summary

This document describes a mechanism to control the **sampling rate** of devices in the Raft framework. The sampling rate (how fast a device internally acquires data from its sensors) is distinct from the **polling rate** (how often the host retrieves data from the device). Both rates need to be configurable by client systems for optimal performance.

---

## Concepts: Polling Rate vs. Sampling Rate

| Concept | Definition | Where Configured | Who Controls |
|---------|------------|------------------|--------------|
| **Polling Rate** | How often the host reads data from a device via I2C | `pollInfo.i` in DeviceTypeRecord, overridable via `/devman/devconfig` | Host firmware |
| **Sampling Rate** | How often the device's internal sensor/ADC acquires new data | Device registers (device-specific) | Device firmware |

### Why Both Matter

- **High polling rate, low sample rate**: Host wastes bus bandwidth re-reading stale data
- **Low polling rate, high sample rate**: Device may lose data between polls (FIFO overflow, or only latest value retained)
- **Optimal**: Polling rate matched to sample rate, with buffering (`numSamples`) to handle timing jitter

### Current State

The Raft framework already supports runtime configuration of:
- **Polling interval**: `/devman/devconfig?deviceid=<id>&intervalUs=<us>`
- **Buffer size**: `/devman/devconfig?deviceid=<id>&numSamples=<n>`

What's missing is a generic mechanism to configure the device's **internal sample rate**.

---

## Architecture Overview

### Device Categories

| Category | Description | Sample Rate Mechanism |
|----------|-------------|----------------------|
| **Static Devices** | Configured in SysType, created at startup, always present | Override virtual methods in custom `RaftDevice` subclass |
| **Dynamic I2C Devices** | Auto-discovered on the bus, use DeviceTypeRecords | Use reserved "actions" in `devInfoJson` to send configuration commands |

### Proposed Solution

1. **Static Devices**: Add `setSampleRate()` / `getSampleRate()` virtual methods to `RaftDevice` base class
2. **Dynamic I2C Devices**: Define a reserved action name (`"_conf.rate"`) in DeviceTypeRecords that specifies how to configure sample rate via I2C writes
3. **Unified API**: Extend or create API endpoints that abstract away the differences between static and dynamic device configuration

---

## Detailed Design

### 1. Static Device Support

Static devices are custom `RaftDevice` subclasses registered with the `DeviceFactory`. These have full C++ implementation control.

#### Proposed Virtual Methods

Add to `RaftDevice.h`:

```cpp
/// @brief Set the device's internal sample rate
/// @param sampleRateHz Desired sample rate in Hz
/// @return RaftRetCode indicating success or failure
virtual RaftRetCode setSampleRate(double sampleRateHz)
{
    return RAFT_NOT_IMPLEMENTED;
}

/// @brief Get the device's current sample rate
/// @return Sample rate in Hz, or 0 if not supported/unknown
virtual double getSampleRate() const
{
    return 0.0;
}

/// @brief Get supported sample rate range
/// @param minHz (out) Minimum supported rate
/// @param maxHz (out) Maximum supported rate
/// @param stepHz (out) Step size (0 if continuous)
/// @return true if sample rate is configurable
virtual bool getSampleRateRange(double& minHz, double& maxHz, double& stepHz) const
{
    return false;
}
```

#### Implementation Example

A custom accelerometer device might implement:

```cpp
class MyAccelerometer : public RaftDevice
{
public:
    RaftRetCode setSampleRate(double sampleRateHz) override
    {
        // Map Hz to device register value
        uint8_t odrReg = mapHzToODR(sampleRateHz);
        
        // Write to device register
        uint8_t cmd[] = {CTRL_REG1, odrReg};
        return i2cWrite(cmd, sizeof(cmd));
    }
    
    double getSampleRate() const override
    {
        return _currentSampleRateHz;
    }
};
```

### 2. Dynamic I2C Device Support

Dynamic devices are discovered on the I2C bus and configured via `DeviceTypeRecords.json`. They don't have custom C++ code, so configuration must be data-driven.

#### Reserved Action Name: `_conf.rate`

Add a reserved action in the `actions` array that the framework recognizes as a sample rate configuration command.

##### Action Schema

```json
{
    "n": "_conf.rate",
    "t": "B",
    "w": "10",
    "f": "d",
    "r": [1, 200],
    "d": 100,
    "map": {
        "1": "00",
        "10": "01",
        "25": "02",
        "50": "03",
        "100": "04",
        "200": "05"
    }
}
```

| Field | Description |
|-------|-------------|
| `n` | Reserved name `"_conf.rate"` — framework recognizes this as sample rate control |
| `t` | Data type (e.g., `"B"` for unsigned byte) |
| `w` | Write prefix (register address in hex) |
| `r` | Valid range of sample rates in Hz |
| `d` | Default sample rate in Hz |
| `map` | (Optional) Mapping from Hz values to register values for discrete ODR devices |

##### Alternative: Continuous Scaling

For devices with continuous sample rate control:

```json
{
    "n": "_conf.rate",
    "t": ">H",
    "w": "40",
    "f": "d",
    "r": [1, 1000],
    "d": 100,
    "mul": 1,
    "u": "Hz"
}
```

Here `mul` (multiplier) and optional divisor allow linear transformation from Hz to register value.

#### Example: LSM6DS IMU

The LSM6DS has discrete ODR settings. A complete DeviceTypeRecord might look like:

```json
{
    "LSM6DS": {
        "addresses": "0x6a",
        "deviceType": "LSM6DS",
        "detectionValues": "0x0f=0x69,0x6a,0x6c",
        "initValues": "0x0a00&0x1048&0x114c&0x1640",
        "pollInfo": {
            "c": "0x22=r12",
            "i": 100,
            "s": 3
        },
        "devInfoJson": {
            "name": "LSM6DS",
            "desc": "6-Axis IMU",
            "manu": "ST",
            "type": "LSM6DS",
            "clas": ["ACC", "GYRO"],
            "resp": { /* ... existing response schema ... */ },
            "actions": [
                {
                    "n": "_conf.rate",
                    "w": "10",
                    "t": "B",
                    "r": [12.5, 6660],
                    "d": 104,
                    "desc": "Accelerometer ODR",
                    "map": {
                        "12.5": "10",
                        "26": "20",
                        "52": "30",
                        "104": "40",
                        "208": "50",
                        "416": "60",
                        "833": "70",
                        "1660": "80",
                        "3330": "90",
                        "6660": "A0"
                    }
                }
            ]
        }
    }
}
```

### 3. API Design

#### Option A: Extend Existing `/devman/devconfig`

Add a `sampleRateHz` parameter to the existing devconfig endpoint:

```
GET /devman/devconfig?deviceid=1_6a&sampleRateHz=104
```

**Response:**

```json
{
    "rslt": "ok",
    "deviceID": "1_6a",
    "pollIntervalUs": 10000,
    "numSamples": 3,
    "sampleRateHz": 104
}
```

**Pros:**
- Consistent with existing API
- Single endpoint for all device timing configuration
- Clients can set polling rate and sample rate together

**Cons:**
- Requires framework changes to recognize and process `_conf.rate` actions

#### Option B: Use Existing `/devman/cmdjson`

Use the existing JSON command mechanism with a standardized command format:

```
GET /devman/cmdjson?body={"device":"1_6a","action":"_conf.rate","value":104}
```

**Pros:**
- No new API endpoint required
- Leverages existing action processing

**Cons:**
- Requires clients to know the action name
- Less intuitive than a dedicated parameter

#### Option C: New `/devman/samplerate` Endpoint

Create a dedicated endpoint for sample rate configuration:

```
GET /devman/samplerate?deviceid=1_6a&hz=104
```

**Response:**

```json
{
    "rslt": "ok",
    "deviceID": "1_6a",
    "sampleRateHz": 104,
    "pollIntervalUs": 10000,
    "recommended": {
        "pollIntervalUs": 9615,
        "numSamples": 3
    }
}
```

**Pros:**
- Clear, dedicated endpoint
- Can return recommended polling settings based on new sample rate

**Cons:**
- Yet another API endpoint
- Fragmenting configuration across multiple endpoints

#### Recommendation: Option A (Extend `/devman/devconfig`)

Option A provides the best user experience by consolidating timing configuration in one place. The implementation would:

1. Parse `sampleRateHz` parameter
2. Look up the device's `_conf.rate` action (for dynamic devices) or call `setSampleRate()` (for static devices)
3. Execute the configuration
4. Return current values for all timing parameters

---

## Implementation Plan

### Phase 1: Foundation (Static Devices)

1. Add `setSampleRate()` / `getSampleRate()` virtual methods to `RaftDevice`
2. Update `DeviceManager::apiDevConfig()` to handle `sampleRateHz` parameter
3. For static devices: delegate to the device's `setSampleRate()` method
4. Add `sampleRateHz` to the response JSON

### Phase 2: Dynamic Device Support

1. Define `_conf.rate` action schema and parsing in `DeviceTypeRecords`
2. Add `DeviceTypeRecords::getSampleConfigAction()` method
3. Implement sample rate command generation from action definition
4. Update `DeviceManager` to process `_conf.rate` actions via I2C writes

### Phase 3: Action Processing Infrastructure

1. Create `ActionProcessor` class to handle action execution:
   - Parse action definition
   - Apply value transformations (`mul`, `sub`, `map`)
   - Generate I2C write command
   - Execute via `BusI2C::i2cSendAsync()`

2. Wire action processing into the command flow:
   ```
   DeviceManager::apiDevConfig()
     └─ ActionProcessor::executeAction("_conf.rate", value)
          └─ BusI2C::i2cSendAsync(writeCmd)
   ```

### Phase 4: Enhanced Features

1. **Auto-adjust polling rate**: When sample rate changes, optionally adjust polling rate to match
2. **Validation**: Reject sample rates outside the device's supported range
3. **Query support**: Add `/devman/devconfig` query-only mode to read current configuration
4. **Coordinated multi-device configuration**: Set sample rate for all devices in a slot

---

## Action Field Reference

Below is a comprehensive reference for action fields that would be used in `_conf.rate` and other actions:

| Field | Type | Description | Example |
|-------|------|-------------|---------|
| `n` | string | Action name (use `_conf.rate` for sample rate) | `"_conf.rate"` |
| `t` | string | Struct pack type code | `"B"`, `">H"`, `"<h"` |
| `w` | string | Write prefix (hex bytes sent before value) | `"10"` |
| `wz` | string | Write prefix when value is zero (optional) | `"0064"` |
| `r` | array | Valid range `[min, max]` | `[1, 200]` |
| `d` | number | Default value | `100` |
| `mul` | number | Multiply input value by this before encoding | `10` |
| `sub` | number | Subtract this from input value before encoding | `0` |
| `map` | object | Discrete value mapping (input → hex output) | `{"100":"04"}` |
| `f` | string | Format specifier for display | `"d"`, `".1f"` |
| `u` | string | Unit string | `"Hz"` |
| `desc` | string | Human-readable description | `"Accelerometer ODR"` |

### Value Encoding Process

For a `_conf.rate` action with `{"n":"_conf.rate", "w":"10", "t":"B", "mul":1, "map":{"100":"04"}}`:

1. **Input**: `sampleRateHz = 100`
2. **Map lookup**: `100` → `0x04` (if `map` exists)
3. **Or calculation**: `value = (input * mul) - sub` → `value = 100`
4. **Encoding**: Pack `value` according to type `t` → `[0x64]` for `"B"`
5. **Command**: Prepend write prefix `w` → `[0x10, 0x04]` (mapped) or `[0x10, 0x64]` (calculated)
6. **I2C Write**: Send `[0x10, 0x04]` to device

---

## Full Example: Robotical Servo with Sample Rate Control

The Robotical Servo currently has actions for `angle` and `enable`. Adding sample rate control:

```json
{
    "RoboticalServo": {
        "addresses": "0x10-0x18",
        "deviceType": "RoboticalServo",
        "detectionValues": "=p250&0x6004ff9c=p250&0x9904ff63=0x526f626f746963616c&0x00=0x008F",
        "initValues": "=p250&0x6004ff9c=p250&0x2001",
        "pollInfo": {
            "c": "=r6",
            "i": 100,
            "s": 1
        },
        "devInfoJson": {
            "name": "Robotical Servo",
            "desc": "Servo",
            "manu": "Robotical",
            "type": "RoboticalServo",
            "clas": ["SRVO"],
            "resp": { /* ... */ },
            "actions": [
                {
                    "n": "angle",
                    "t": ">h",
                    "w": "0001",
                    "wz": "0064",
                    "f": ".1f",
                    "mul": 10,
                    "sub": 0,
                    "r": [-180.0, 180.0],
                    "d": 0
                },
                {
                    "n": "enable",
                    "t": "B",
                    "w": "20",
                    "f": "b",
                    "r": [0, 1],
                    "d": 1
                },
                {
                    "n": "_conf.rate",
                    "t": "B",
                    "w": "30",
                    "f": "d",
                    "r": [10, 100],
                    "d": 100,
                    "u": "Hz",
                    "desc": "Internal position sampling rate"
                }
            ]
        }
    }
}
```

**Usage:**

```
GET /devman/devconfig?deviceid=1_10&sampleRateHz=50&intervalUs=20000
```

This would:
1. Send I2C write `[0x30, 0x32]` (0x32 = 50) to configure device sample rate
2. Set host polling interval to 20ms

---

## Coordinating Polling and Sampling

For optimal performance, polling rate and sample rate should be coordinated. The framework could provide:

### Auto-Coordination Mode

Add a `coordinated` parameter:

```
GET /devman/devconfig?deviceid=1_6a&sampleRateHz=100&coordinated=true
```

When `coordinated=true`:
- Set the device sample rate to 100 Hz
- Automatically set `intervalUs` to 10000 (10ms = 100 Hz)
- Adjust `numSamples` if needed for FIFO-based devices

### Recommended Settings Response

Return recommended polling configuration in responses:

```json
{
    "rslt": "ok",
    "deviceID": "1_6a",
    "sampleRateHz": 100,
    "pollIntervalUs": 50000,
    "numSamples": 1,
    "recommended": {
        "pollIntervalUs": 10000,
        "numSamples": 1,
        "reason": "Poll interval > sample interval may cause data loss"
    }
}
```

---

## Multiple Sample Rates per Device

Some devices (like IMUs) have separate sample rates for different subsystems (accelerometer vs. gyroscope). This can be handled with multiple actions:

```json
"actions": [
    {
        "n": "_conf.accel",
        "w": "10",
        "t": "B",
        "r": [12.5, 6660],
        "d": 104,
        "desc": "Accelerometer ODR"
    },
    {
        "n": "_conf.gyro",
        "w": "11",
        "t": "B",
        "r": [12.5, 6660],
        "d": 104,
        "desc": "Gyroscope ODR"
    }
]
```

**API extension:**

```
GET /devman/devconfig?deviceid=1_6a&sampleRateHz.accel=416&sampleRateHz.gyro=104
```

Or with JSON body via cmdjson:

```json
{
    "device": "1_6a",
    "sampleConfig": {
        "accel": 416,
        "gyro": 104
    }
}
```

---

## Error Handling

### Invalid Sample Rate

If requested sample rate is outside valid range:

```json
{
    "rslt": "fail",
    "error": "invalidSampleRate",
    "requested": 1000,
    "validRange": [12.5, 6660]
}
```

### Device Doesn't Support Sample Rate Configuration

```json
{
    "rslt": "fail",
    "error": "sampleRateNotSupported",
    "deviceType": "VL53L4CD"
}
```

### I2C Communication Failure

```json
{
    "rslt": "fail",
    "error": "i2cWriteFailed",
    "deviceID": "1_6a"
}
```

---

## Query Current Configuration

To query without changing settings:

```
GET /devman/devconfig?deviceid=1_6a
```

**Response:**

```json
{
    "rslt": "ok",
    "deviceID": "1_6a",
    "pollIntervalUs": 10000,
    "numSamples": 3,
    "sampleRateHz": 104,
    "sampleRateSupported": true,
    "sampleRateRange": [12.5, 6660]
}
```

---

## Summary

| Device Type | Sample Rate Mechanism | Configuration Method |
|-------------|----------------------|---------------------|
| Static (custom class) | Override `setSampleRate()` | API calls virtual method |
| Dynamic I2C | `_conf.rate` action in DeviceTypeRecords | API executes action via I2C write |
| Both | `/devman/devconfig?sampleRateHz=<hz>` | Unified API endpoint |

### Key Design Decisions

1. **Reserved action name** `_conf.rate` distinguishes sample rate configuration from other device actions
2. **Extend `/devman/devconfig`** rather than creating a new endpoint for API consistency
3. **Support discrete and continuous** sample rates via `map` field or linear scaling
4. **Auto-coordination option** helps users set optimal polling parameters

### Next Steps

1. Review this design with stakeholders
2. Define `_conf.rate` schemas for priority device types
3. Implement Phase 1 (static device support)
4. Implement Phase 2–4 incrementally
5. Update wiki documentation
