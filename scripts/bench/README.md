# Device identification and polling bench

`device-bench.js` measures, on a Raft device with I2C devices attached:

- how closely each device is polled at its configured interval (from the firmware's poll timing statistics);
- the sample rate a client actually receives (decoded by raftjs from the `devbin` stream);
- where the I2C worker task spends its time (scanning, polling, requests), in about 1 s windows;
- how long each device took to be seen, come online and be identified after boot.

It runs a set of scenarios, setting each device's poll interval with `devman/devconfig` (and a sample buffer large
enough that publishing doesn't drop samples), and restores the default intervals at the end. Results are written
as JSON so that runs - for example before and after a change - can be compared.

The firmware must report `activity`, `pollTiming` and `identTiming` in `devman/busstatus` (RaftCore and RaftI2C
from October 2026).

## Setup

Requires Node 22 or later (it uses the built-in `fetch` and `WebSocket`) and a built copy of raftjs:

```bash
cd <raftjs>
npm ci
npm run build
```

## Running

```bash
node device-bench.js --ip 192.168.86.52 --raftjs <raftjs> --reboot --out results.json --label baseline
```

| Option | Default | Meaning |
|---|---|---|
| `--ip` | (required) | Device IP address (HTTP and WebSocket) |
| `--raftjs` | (required) | Path to a built raftjs |
| `--devices` | `1_68:50000,1_39:100000` | Devices to exercise, as `<deviceId>:<default poll interval in us>` |
| `--duration` | `20` | Seconds per scenario |
| `--reboot` | off | Reset the device first, so identification timing is from a fresh boot |
| `--out` | (none) | File to write the results to (they are also printed) |
| `--label` | `run` | Name stored in the results |

The default devices are the FeatherS3 bench: an MPU6050 at 0x68 and an APDS9960 at 0x39 on I2C bus 1.

## Scenarios

| Name | Poll intervals |
|---|---|
| `defaults` | Each device's default interval |
| `all_100ms`, `all_50ms`, `all_25ms`, `all_10ms` | Every device at the same interval |
| `mixed_10ms_<n>ms` | The first device at 10 ms, the others at their defaults |

## Results

For each scenario and device: the configured rate, the firmware's measured poll timing (`firmware`: mean, min and
max interval, a histogram of the difference from the configured interval and its 95th percentile, missed periods,
achieved rate, and mean bus time per poll), the poll rate error, and the rate delivered to the client. For each scenario: the firmware's estimate of the bus time polling needs (`pollLoadPct`), the I2C worker's
busy percentage, passes per second, longest pass, scan probes and poll steps per second, and time per second in
each part of a pass. Plus identification timing at boot, the main loop statistics, and health checks (the
WebSocket connection stayed up, so the device didn't reboot; the device list didn't change).
