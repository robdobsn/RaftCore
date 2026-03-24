# `devman/devconfig` — Device Configuration API

Configure polling parameters for a bus device at runtime.

## Endpoint

```
GET devman/devconfig?<device>&<params>
```

## Device Addressing

A device can be identified in two ways:

| Style | Example |
|-------|---------|
| Device ID | `deviceid=1_25` (busNumber_hexAddress) |
| Bus + Address | `bus=I2CA&addr=25` |

## Parameters

All configuration parameters are optional. At least one must be provided.

| Parameter | Type | Description |
|-----------|------|-------------|
| `intervalUs` | integer | Polling interval in microseconds. Must be > 0. |
| `numSamples` | integer | Number of poll result samples the device buffers. Must be > 0. Changing this clears any previously buffered samples. |

Parameters may be combined in a single request.

## Examples

Set polling interval only:
```
devman/devconfig?deviceid=1_25&intervalUs=50000
```

Set number of buffered samples only:
```
devman/devconfig?deviceid=1_25&numSamples=10
```

Set both at once:
```
devman/devconfig?bus=I2CA&addr=25&intervalUs=50000&numSamples=5
```

## Response

Success:
```json
{
  "rslt": "ok",
  "deviceID": "1_25",
  "pollIntervalUs": 50000,
  "numSamples": 5
}
```

The response always includes the current values of both `pollIntervalUs` and `numSamples` for the device, regardless of which parameters were changed.

## Errors

| Error | Cause |
|-------|-------|
| `failAddrMissing` | No `deviceid` or `addr` parameter provided |
| `failBusMissing` | Using `addr` style but no `bus` parameter provided |
| `failBusNotFound` | Bus name or number does not exist |
| `failInvalidDeviceID` | Device ID could not be parsed |
| `failInvalidInterval` | `intervalUs` is 0 |
| `failInvalidNumSamples` | `numSamples` is 0 |
| `failUnsupportedBus` | The bus does not support the requested operation, or the device was not found on the bus |

## Notes

- `numSamples` corresponds to the `"s"` field in the device's `pollInfo` configuration. Changing it at runtime overrides the value from the `DeviceTypeRecord`.
- Changing `numSamples` clears all previously buffered poll data for the device.
- Both parameters are backward-compatible additions; existing clients that omit them are unaffected.
