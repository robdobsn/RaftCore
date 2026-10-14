# Deferred REST responses: hardware validation

How to see deferred REST responses (devdocs/device-identification-review.md §13) working on a real
device, over HTTP (curl) and over WebSocket (raftjs).

**Coverage (slices 1 to 3).** Requests are deferred when they arrive:
- over RICSerial transports (WebSocket, BLE and serial frames, e.g. raftjs), which is slice 1, in RaftCore;
- as HTTP GET requests, which is slice 2. This needs the RaftWebServer `feature/deferred-responses` branch
  in `raftdevlibs` as well;
- as commands typed at the serial console, on the remote-control TCP socket (LogManager `RaftRemote`
  logger) or in a `.api` command file, which is slice 3. This needs the RaftSysMods
  `feature/deferred-responses` branch in `raftdevlibs` as well.

HTTP POST and MQTT are not deferred.

## 1. Build the firmware (RaftROS `examples/ExampleDiscoverable`)

The example's `raftdevlibs/RaftCore` is a symlink to this RaftCore checkout. Check out
`feature/deferred-responses` here and the build uses it.

`raftdevlibs` contains absolute symlinks, so the docker build must mount the whole `rdev` tree at the
same path (`raft build --docker` only mounts the project folder):

```sh
cd ~/Documents/rdev/raft/RaftROS/examples/ExampleDiscoverable
docker run --rm -u $(id -u):$(id -g) -e HOME=/tmp \
    -v ~/Documents/rdev:$HOME/Documents/rdev -w $PWD raftbuilder:latest \
    idf.py -B build/SysTypeFeatherS3 build
```

The build folder name selects the SysType. Look for `LOCAL (DEBUG) LIBRARY: RaftCore` in the output to
confirm the local RaftCore was used.

## 2. Flash and monitor

```sh
raft f -s SysTypeFeatherS3 -p /dev/ttyACM1 -t <path-to-working-esptool>
raft m -p /dev/ttyACM1
```

The `-t` is only needed where `~/.local/bin/esptool.py` is broken; any working `esptool` (e.g. from a
venv) will do.

Optional: to see each deferred response logged on the console, add `#define DEBUG_DEFERRED_RESPONSES` near
the top of `components/comms/ProtocolExchange/ProtocolExchange.cpp` and rebuild. This logs a
`deferred response sent channelID ... msgNum ...` line for each one.

## 3. Check with curl (HTTP: deferred path)

```sh
IP=192.168.86.52
# Registry statistics (pend = pending, def = deferred, cmpl = completed, tout = timed out,
# full = rejected as full, unk = late/unknown completions)
curl "http://$IP/api/sysmoddebug/RestAPI"
# Read the MPU6050 WHO_AM_I register -> "rdData":"68"
curl "http://$IP/api/devman/cmdraw?deviceid=1_68&hexWr=75&numToRd=1"
# Read from an empty address -> "error":"BUS_ACK_ERROR" (the bus error, not a timeout)
curl "http://$IP/api/devman/cmdraw?deviceid=1_50&hexWr=00&numToRd=1"
# Counters have increased (def/cmpl), as HTTP GET requests are deferred
curl "http://$IP/api/sysmoddebug/RestAPI"
# A deferred request which times out -> HTTP 200 with {"rslt":"fail","error":"timeout"}
curl -i "http://$IP/api/devman/cmdraw?deviceid=1_68&hexWr=75&numToRd=1&timeoutMs=1"
# Main-loop timing for the last stats period
curl "http://$IP/api/sysmoddebug/SysMan"
```

## 4. Check with raftjs (WebSocket: deferred path)

`run.sh` runs the scripts in this folder inside the `raftbuilder` image, which has Node 20. On first use
it builds a copy of the sibling `raftjs` repo into `.raftjs/`, which is git-ignored.

```sh
cd ~/Documents/rdev/raft/RaftCore/unit_tests/deferred_responses_hw

# One command (optionally repeated): shows each response, its round-trip time and the counters before/after
./run.sh try.js 192.168.86.52 "devman/cmdraw?deviceid=1_68&hexWr=75&numToRd=1" 5
#   -> 5 x {"rdData":"68","rslt":"ok"}, and def/cmpl both increase by 5

# A read with a deliberately short timeout -> {"rslt":"fail","error":"timeout"}, tout and unk increase
./run.sh try.js 192.168.86.52 "devman/cmdraw?deviceid=1_68&hexWr=75&numToRd=1&timeoutMs=1"

# Full integration test (20 checks: WebSocket and HTTP)
./run.sh deftest.js 192.168.86.52
#   reads, error paths, concurrency, capacity, forced timeouts, duplicate frames (same msgNum),
#   client disconnect while pending, unaffected commands, command-frame form,
#   HTTP: deferred reads, errors, timeout (200 + JSON), concurrency, disconnect, Content-Length

# Concurrent HTTP requests (note the server's connection limit - resets appear at about 10 concurrent connections,
# with or without deferral)
./run.sh httpconc.js 192.168.86.52 "devman/cmdraw?deviceid=1_68&hexWr=75&numToRd=1" 6 3

# Main-loop stall while handling requests (compare against a firmware built from RaftCore main)
./run.sh stall.js 192.168.86.52 mylabel

# Wider benchmark (idle, streaming, sequential and concurrent reads, error paths, HTTP and WS latency)
./run.sh bench.js 192.168.86.52 mylabel
```

`deftest.js` prints PASS/FAIL per check and ends with `ALL PASSED`. Run individual tests with
`./run.sh deftest.js <ip> T5,T6`.

## 5. Check the serial console, remote socket and command files

```sh
# Serial console: type at `raft m -p /dev/ttyACM1`
devman/cmdraw?deviceid=1_68&hexWr=75&numToRd=1
#   -> {"req":"devman/cmdraw?...","rdData":"68","rslt":"ok"} (printed when the bus read completes)

# Remote-control TCP socket (needs a LogManager logDest of type RaftRemote, e.g. port 8080, enabled)
nc 192.168.86.52 8080
devman/cmdraw?deviceid=1_68&hexWr=75&numToRd=1

# Command file (needs CommandFile enabled): each line is "<command> [delayMs] [repeats]"
printf 'devman/cmdraw?deviceid=1_68&hexWr=75&numToRd=1 100 3\n' > t.api
curl -F "file=@t.api" "http://$IP/api/fileupload"
curl "http://$IP/api/filerun/t.api"
curl "http://$IP/api/sysmoddebug/RestAPI"    # def and cmpl both +3
```

## What to expect

| Request | Via | Response | Counters |
|---|---|---|---|
| `cmdraw` read, device present | raftjs | `rdData` | `def` and `cmpl` +1 |
| `cmdraw` read, empty address | raftjs | `BUS_ACK_ERROR`, promptly | `def` and `cmpl` +1 |
| `cmdraw` read, `timeoutMs=1` | raftjs | `timeout` | `def`, `tout` and `unk` +1 |
| `cmdraw` read | curl | `rdData` / bus error | `def` and `cmpl` +1 |
| `cmdraw` read | serial console / TCP socket | `rdData` / bus error (printed when available) | `def` and `cmpl` +1 |
| `cmdraw` read in a `.api` file | `filerun/<file>.api` | discarded | `def` and `cmpl` +1 |
| `cmdraw` write only (no `numToRd`) | either | `ok` immediately | unchanged |

**Main-loop effect** (`stall.js`, compared with a RaftCore `main` build): the CommsMan worst case per 1 s
window during `cmdraw` reads falls from about 14–17 ms to 5–8 ms, close to the `v` control.

**Network noise.** WiFi latency on the bench can vary a lot. Compare builds with interleaved runs, and use
the `v` command latency as the network reference.
