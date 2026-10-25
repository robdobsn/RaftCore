# BLE Links Held by Windows After a Client Exits

## Summary

On 2026-10-25, BLE connections from a Windows client that ended without releasing the link were investigated on an
Axiom (ESP32-S3) with RaftSysMods 1.27 (`BLEConnArbiter`, two connections). The question was whether BLEMan should
detect and drop such a link itself.

- Windows doesn't unsubscribe from the response characteristic when the client process dies. The subscription stays
  until the link drops, so "connected but not subscribed" can't identify a dead client.
- Windows ends the link itself about 25 s after the process dies (3 of 3 runs).
- A new client on the same PC during that time reuses the held link (Windows hands it the same connection), and
  releases it cleanly when it finishes.
- With two connections allowed, the device keeps advertising while a link is held, so other centrals can still find
  it and take over.

**No firmware change is needed.** The only effect of a dead Windows client is that the session, and so the WiFi pause
(`pauseWiFiforBLE`), lasts about 25 s longer than it should.

## Background

A client releases the link by sending the REST command `bledisconnect`. RaftCLI does this when it finishes, on
errors and on Ctrl-C (RaftCLI `devdocs/fs-over-serial-ws-ble-plan.md`, §6.3 and §6.5). A client that is killed, or
crashes, can't.

In October 2026, with firmware that allowed one connection (and so didn't advertise while connected), a link held by
Windows was seen to last more than 3 minutes. During that time the device couldn't be found over BLE at all.
RaftSysMods commit `6d5bdec` (1.26.0, 2026-10-24) added `BLEConnArbiter`:

- the device advertises while fewer than `maxConns` (default 2) centrals are connected;
- the connection that sends a command is the session, and if the other connection sends one it takes over (the old
  one is dropped);
- while two are connected, one that has sent no command for `idleConnDropMs` (default 8000) is dropped;
- status changes (WiFi pausing) follow the session, not the connection.

The open question was whether a held link that *had* sent commands (the session) needed dropping too, for example
when it was connected but no longer subscribed.

## Test Setup

- Axiom018 firmware at RoboticalAxiom1 `e3705f3`, with RaftCore and RaftSysMods `main` of 2026-10-25, and
  `DEBUG_RESP_SUBSCRIPTION` defined in `BLEGattServer.cpp` (with the subscribe event's prev/cur notify and indicate
  added to the log line).
- Windows 11, Intel Bluetooth; RaftCLI (`raft.exe`, btleplug 0.13.5 with the WinRT service-open fix) uploading 1 MB
  with `raft fs put ble:Axiom018_2cc23e ...`.
- The process was killed with `Stop-Process -Force` about 12 s into the upload (at about 20%), so it had no chance to
  send `bledisconnect`. Ctrl-C isn't a test of this: RaftCLI handles it and releases the link.
- The device log was read on its USB serial port.

## Observed Behaviour

### A normal session (for reference)

| Device time (ms) | Event |
|-|-|
| 39393 | connected, connHandle 1 |
| 39542 | Service Changed characteristic (attr 8) subscribed, indicate (Windows) |
| 40391 | response characteristic (attr 18) subscribed, indicate (btleplug's `subscribe()`) |
| 40451 | attr 18 changed to notify (RaftCLI writes the CCCD) |
| 40758 | `bledisconnect`: the device disconnects |
| 40767 | disconnected, LocalHostTerminated (0x216), after 1.4 s |
| 41208 | Windows connects again on its own (connHandle 2) and subscribes only to attr 8 |

The connection Windows makes on its own sends no commands, so it never becomes a session: WiFi isn't paused and the
device keeps advertising. It is the connection `BLEConnArbiter` was written for.

### The client killed mid-transfer

| Run | Killed (device time) | Link ended | Reason | Time after the kill |
|-|-|-|-|-|
| 1 | about 87 s | 111.4 s, 36.2 s after connecting | RemoteUserTerminated (0x213) | about 24 s |
| 2 | about 456.4 s | 482.3 s, 35.8 s after connecting | RemoteUserTerminated (0x213) | about 26 s |
| 3 | about 516.4 s | 542.7 s, 35.2 s after connecting | RemoteUserTerminated (0x213) | about 26 s |

- No subscription change was reported when the process died. The response characteristic stayed subscribed (notify)
  until the link ended. The unsubscribes were reported only then, as part of the disconnect.
- WiFi stayed paused (`"conn":0` in the SysMan status) until the link ended, then reconnected.
- The kill times are approximate (the last log line before the kill).

### A new client while the link is held

A second `raft fs df ble:Axiom018_2cc23e` started straight after a kill completed in 0.4 s. The device log showed
no new connection: the new process subscribed again on the held link (connHandle 1), ran its command, and its
`bledisconnect` ended the link cleanly (LocalHostTerminated, after 12.9 s).

## Conclusions

1. **Dropping a connected but unsubscribed link wouldn't catch this.** Windows keeps the subscription of a dead
   client; it only goes when the link ends.
2. **A held link is short-lived and doesn't block anyone.** Windows ends it after about 25 s. Until then, a client
   on the same PC reuses it, and other centrals can connect (the device is advertising) and take over the session.
3. **The remaining cost is about 25 s of WiFi pause** after a client dies mid-session (on devices with
   `pauseWiFiforBLE`), and one of the two connection slots.

## Recommendation

Don't add a drop policy for the session connection now. The option, if longer holds are seen in practice, is a
session idle timeout: no command for N minutes while the session is active, then disconnect. It would need to be
off by default or long, because a legitimate client can sit idle with no traffic, for example an app waiting for
the user.

## Other Findings

- The subscribe event's `reason` is logged through `getHSErrorMsg()`, which reads it as a NimBLE host error code. So
  a CCCD write (`BLE_GAP_SUBSCRIBE_REASON_WRITE`, 1) appears as `TryAgain`, and the end of the connection
  (`BLE_GAP_SUBSCRIBE_REASON_TERM`, 2) as `AlreadyInProgress`. It is only visible with `DEBUG_RESP_SUBSCRIPTION`.
- btleplug's `subscribe()` on Windows enables indications only, and RaftCLI's CCCD write then switches the response
  characteristic to notifications. Firmware since RaftSysMods `0753ed0` counts either as subscribed (and sends
  indications regardless).

## Reproducing

1. Define `DEBUG_RESP_SUBSCRIPTION` in RaftSysMods `components/BLEManager/BLEGattServer.cpp` and build the app.
2. Monitor the device's serial port, start a long BLE transfer from RaftCLI on Windows, and kill it from PowerShell:
   `Get-CimInstance Win32_Process -Filter "Name='raft.exe'" | Where-Object { $_.CommandLine -like '*fs put ble:*' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }`
3. Watch for `disconnected connHandle ... RemoteUserTerminated` and the `handleSubscription` lines.
