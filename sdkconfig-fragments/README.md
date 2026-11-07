# sdkconfig fragments

ESP-IDF settings for a purpose, which a project selects in a SysType's `features.cmake` (or
`systypes/Common/features.cmake`) instead of copying them into `sdkconfig.defaults`:

```cmake
set(RAFT_SDKCONFIG_FRAGMENTS wifi-fast-connect deep-sleep-fast-wake)
```

Each name is a file `<name>.defaults` in this folder. An entry can also be a path to a project's own fragment
(relative to the project folder), e.g. `systypes/Common/low-power.defaults`.

The layers are applied in order, later ones overriding earlier ones (by `CONFIG_` key):

1. `systypes/Common/sdkconfig.defaults`
2. `systypes/Common/sdkconfig.<variant>.defaults` for the build variant (if it exists)
3. the fragments, in the order listed
4. the SysType's `sdkconfig.defaults`
5. the SysType's `sdkconfig.<variant>.defaults` for the build variant (if it exists)

So a SysType can still override any setting a fragment makes.

| Fragment | Purpose |
|---|---|
| `wifi-fast-connect` | Shorter DHCP on each WiFi connection (restore the last lease, no ARP check of the offered address). Use with NetMan `wifiSTAFastReconnect` |
| `deep-sleep-fast-wake` | Shorter boot after a wake from deep sleep (no app image check, bootloader warnings only) |
