# WiFi fast connect and on-demand start

For battery devices that wake from deep sleep and use WiFi on some wakes - and for any device that should join
the network quickly after a restart. Everything here is off unless selected, so existing apps are unchanged.

## Measured (ESP32-C5, ESP-IDF 6.1, WPA2 home router, deep-sleep wake)

| | IP address (from boot) | MQTT connected |
|---|---|---|
| Defaults (scan all channels on both bands, DHCP) | 15.3 s | 20.4 s |
| 2.4 GHz only, first connect (scan) | 0.85 - 3.1 s | 1.3 - 5.2 s |
| Remembered AP + static IP | 0.26 - 0.31 s (20 wakes) | 0.65 - 4.9 s, median 1.2 s (laptop broker over WiFi: steps are TCP retries) |

Boot to WiFi start is about 0.38 s of that. MQTT was also held up 5 s after the IP address by the client's
retry interval and DNSResolver's failed-lookup hold-off - both fixed (MQTT now connects within 250 ms of the
network coming up).

## NetMan settings (SysTypes JSON)

| Key | Default | |
|---|---|---|
| `startMode` | `auto` | `onDemand`: WiFi isn't started at setup. Start and stop it with the NetMan command `{"cmd":"wifiStart"}` / `{"cmd":"wifiStop"}` (`sysModSendCmdJSON("NetMan", ...)`) or the `wifistart` / `wifistop` API |
| `wifiSTAFastReconnect` | 0 | Remember the AP connected to (channel and BSSID) in RTC memory and connect straight to it next time without scanning. Survives deep sleep and software restarts, not power on |
| `wifiSTAFastReconnectMinRSSI` | -80 | Don't remember an AP weaker than this (dBm, 0 for no minimum) so that a better one is looked for |
| `wifiSTAFastReconnectMaxUses` | 0 | Scan again after the remembered AP has been used this many times (0 for no limit) - finds a better AP on a mesh network |
| `wifiBand` | unset | `2g` or `5g` on chips with 5 GHz. A scan of one band is much shorter, especially avoiding 5 GHz DFS channels (passive scans). Unset (or `auto`): the driver's band setting is left alone |
| `wifiStaticIP`, `wifiStaticGateway`, `wifiStaticNetmask`, `wifiStaticDNS` | blank (DHCP) | Static IP. Usually set per device with the `wifiip` API rather than in the SysType |

## Behaviour

- **First connect / after power on**: no remembered AP, so a normal scan (as configured). The AP connected to is
  then remembered.
- **Remembered AP fails** (or the connection to it is lost): it is forgotten and the same WiFi start falls back to a
  normal scan (NetMan named value `fastFallbacks` counts these).
- **New credentials** (`w/SSID/password`): the remembered AP is forgotten.
- **Full scan** (`{"cmd":"wifiStart","fullScan":1}` or `wifistart?fullScan=1`): this start forgets the remembered
  AP and scans all channels (of the `wifiBand` band) for the strongest AP - e.g. after a restart, so that a
  better AP is found, or when connections keep failing (the AP may have changed). A scan that stops at the
  first AP found (`wifiSTAConnectBySignal` 0) can join a weak AP on a network with several - and fast reconnect
  then keeps using it - so leave `wifiSTAConnectBySignal` at its default (1) with fast reconnect.
- The remembered AP and the full scan config are set with RAM storage, so the stored (flash) WiFi config is
  unchanged and nothing is written to flash on each start.
- Not for networks that rely on 802.11v/k roaming (BTM/MBO): the device chooses the AP.
- `startMode` `onDemand`: `w`, `wap` and `wc` start WiFi if it isn't started. SysManager's reboot-if-disconnected
  doesn't count time while WiFi is stopped (in any other case it is as before).
- **Credentials in config** (`wifiSSID` / `wifiPW`): NetMan applies them on every WiFi start, but now only when
  they differ from the stored ones (`configWifiSTA(..., skipIfUnchanged)`) - reconfiguring the same credentials
  meant a disconnect and reconnect on every start, and forgot the remembered AP. The `w` API still always
  reconnects.

## What changes for an app that selects none of this

Nothing in the connection sequence: WiFi starts at setup as before, the band isn't touched, DHCP runs, the
remembered-AP code never runs and reboot-if-disconnected counts as it did. The differences are:

- credentials from config no longer cause a disconnect and reconnect at setup when they are already stored
- `configWifiSTA()` (the `w` API) clears any BSSID or channel pin in the stored config - Raft never set one, so
  this only affects an app that pinned them itself with `esp_wifi_set_config()`
- the `wifistart`, `wifistop` and `wifiip` APIs exist on every command channel (`wifiip` persists in NVS, as
  `w` does)
- RSSI is read when the IP address is obtained (about 2 ms on the event task), and 48 bytes of RTC memory
  hold the remembered AP

## Per-device static IP (`wifiip` API, kept in NVS namespace `netman`)

```
wifiip/192.168.1.30/192.168.1.1[/netmask[/dns]]    set (netmask defaults to 255.255.255.0, DNS to the gateway)
wifiip/clear                                        use the SysType setting (normally DHCP)
wifiip                                              show the setting
```

Applied from the next WiFi start. A static IP saves the 2-3 s of DHCP seen even with the DHCP options below - but
it must be outside the router's DHCP pool (or reserved for the device).

## NetMan named values

`RSSI` (any name starting R, as before), `started`, `connected`, `connMs` (WiFi start to IP address),
`fastReconnect` (the latest start used the remembered AP), `fastFallbacks`.

RSSI is now read when the IP address is obtained, so a connection that lasts less than the 2 s RSSI update still
reports it.

## sdkconfig fragments (features.cmake)

```cmake
set(RAFT_SDKCONFIG_FRAGMENTS wifi-fast-connect)
```

`wifi-fast-connect`: DHCP restores the last lease and doesn't ARP-check the offered address. Also
`deep-sleep-fast-wake` (no app image check on a deep-sleep wake, bootloader warnings only). See
`sdkconfig-fragments/README.md` - fragments sit between `systypes/Common/sdkconfig.defaults` and the SysType's own.
