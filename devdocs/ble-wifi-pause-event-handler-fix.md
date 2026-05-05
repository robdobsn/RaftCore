# BLE WiFi Pause Event Handler Fix

## Summary

On 2026-05-04, intermittent crashes during Web Bluetooth connection were
traced to the WiFi pause/resume path used when BLE connects.

The fix is in `RaftCore` `NetworkSystem`: store the ESP-IDF event handler
instance handles returned by `esp_event_handler_instance_register()` and pass
those exact handles to `esp_event_handler_instance_unregister()` when WiFi is
stopped for BLE coexistence.

No system event task stack-size change was made.

## Observed Failure

The crash occurred around BLE connect/disconnect while WiFi was paused and then
resumed. The symptoms included:

- `assert failed: xQueueGenericSend queue.c:937`
- `A stack overflow in task sys_evt`

Direct formatted logging inside the ESP system event task also reproduced a
stack overflow, confirming that `sys_evt` has very little spare stack in this
firmware configuration. Logging from that callback must stay minimal.

## Confirmed Cause

`NetworkSystem::startWifi()` registered three event handlers:

- `WIFI_EVENT, ESP_EVENT_ANY_ID`
- `IP_EVENT, IP_EVENT_STA_GOT_IP`
- `IP_EVENT, IP_EVENT_STA_LOST_IP`

`NetworkSystem::stopWifi()` attempted to unregister them with `nullptr` handler
instance arguments. In ESP-IDF 5.5.2, unregistering an instance handler requires
the instance handle returned by `esp_event_handler_instance_register()`.

The unregister calls returned `ESP_ERR_INVALID_ARG`, so each BLE pause/resume
cycle left the previous handlers registered. Subsequent WiFi restarts
accumulated duplicate callbacks and produced duplicate reconnect attempts.

Diagnostic logs before the fix showed handler count growing from 3 to 6 after
one BLE cycle and `esp_wifi_connect()` returning `ESP_ERR_WIFI_CONN` from a
duplicate `WIFI_EVENT_STA_START` callback.

## History

The relevant commit history is:

- `3bb045c8` (`Tidied up network handling`, 2023-06-30) moved network handling
  to `esp_event_handler_instance_register(..., nullptr)`. At this point WiFi
  pause still used `esp_wifi_disconnect()` / `esp_wifi_connect()`, so the
  invalid unregister path was not exercised during BLE pause/resume.
- `631be91c6991dd86f7b36a94c39cb9c094a4cddd` (`Fixed Wifi pause code`,
  2023-07-11) changed BLE WiFi pause from `esp_wifi_disconnect()` to full
  `stopWifi()` / `startWifi()` cycling and added the invalid instance
  unregister calls. This is where the handler-lifecycle bug was introduced.
- `085455d3` (`Improved logging performance over USB JTAG`, 2026-04-22)
  changed `loggerLog()` from a heap/PSRAM-backed buffer to a 1024-byte stack
  buffer. This did not create the handler bug, but it reduced stack margin for
  formatted logging from low-stack tasks such as `sys_evt`.
- RoboticalAxiom1 commit `475f7b4` (`Moved back to ESP IDF 5.5.2 until I2C
  issue fixed`, 2026-04-23) and later `d09d30c` (`Stabilize offline data
  logging firmware`, 2026-04-29) left the current firmware building on ESP-IDF
  5.5.2 with the newer RaftCore logging/event behavior.

The invalid unregister sequence from `631be91c` was:

- `esp_event_handler_instance_register(..., nullptr)` in `startWifi()`
- `esp_event_handler_instance_unregister(..., nullptr)` in `stopWifi()`

ESP-IDF 5.5.2 did not introduce the API requirement. Local ESP-IDF 4.4.4, 5.4.1,
and 5.5.2 all return `ESP_ERR_INVALID_ARG` when
`esp_event_handler_instance_unregister_with()` is called with a null instance
handle. The recent 5.5.2/6.0 migration work made this latent bug more visible,
but the handler-lifecycle bug itself is older.

Recent logging and IDF migration work changed timing and stack pressure enough that the
same invalid handler lifecycle now reproduces as `sys_evt` stack overflows,
watchdog panics, or duplicate WiFi reconnect attempts.

## Fix

`NetworkSystem` now stores the three event handler instance handles:

- `_wifiEventHandlerInstance`
- `_ipGotEventHandlerInstance`
- `_ipLostEventHandlerInstance`

On WiFi stop, each non-null handle is unregistered with its matching event
base/event ID and then cleared on `ESP_OK`.

The WiFi-disconnect warning that used to format a log line from the event
callback is now deferred and printed from `NetworkSystem::loop()`. This keeps
the system event task callback lightweight while preserving the firmware console
warning.

## Verification

After flashing Axiom009 with the fix, three real Web Bluetooth connection runs
completed successfully using `Axiom009_adcf1e`.

Serial evidence from the fixed firmware:

- BLE connect: all three handlers unregistered with `ESP_OK`
- WiFi paused: active handler count returned to `0`
- BLE disconnect / WiFi resume: exactly three handlers registered again
- Post-resume `WIFI_EVENT_STA_START`: callback delta stayed at `1`
- No panic, assert, or `sys_evt` stack overflow occurred after the flash

The remaining `sys_evt` high-water mark was still low during WiFi/IP events, so
the code should continue to avoid formatted logging or heavy work in ESP event
callbacks.

After the temporary revert sanity test, the smaller production fix was reapplied,
built, flashed, and checked again on 2026-05-04. Two real Web Bluetooth presence
runs passed and the serial capture contained no panic, assert, reboot, or
`sys_evt` stack-overflow signature. The capture did show an unrelated
post-reconnect mDNS warning (`Service already exists`) that did not reboot the
device.
