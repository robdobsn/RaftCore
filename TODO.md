# Todo

- [ ] provide support for device type file compilation through an interface - js? python?
- [ ] build a UI in gh that allows files to be selected composed and uploaded to file system - and maybe also a web uploader on same page
- [ ] consider sidecar files for device type info instead of embedded notes
- [ ] handle sidecar data files like VL53L5CX requires - maybe they need uploading, etc - sourced from somewhere else like ST's website - to avoid copyright?
- [ ] get VL5L5CX working natively - download binary from file system?
- [ ] implement a mechanism that allows coord system transforms for devices like rotation/inversion/etc - either as an expression or as a named transformation
- [ ] consider compressing device types json
- [x] init values computed from values read from the device (`initCode`) - done 2026-10-18 for the BME680/BME690 heater; the VL53L0X SPAD setup still needs multi-phase support, so records can write per-unit settings: the BME680 gas heater set-point (the record uses a fixed typical value, so the hot plate temperature differs between units and gas resistance isn't comparable between them) and the VL53L0X reference SPAD setup (omitted, losing accuracy and range). Design: devdocs/computed-init-values.md (section 2, init code; section 3, where it goes)
- [ ] flash writes blocking the main loop: file uploads, `filehash`, Axiom DataLogger writes and the cache refresh after a file close are over the 10 ms target. Measured 2026-10-24: a write task removes SD pauses; LittleFS also needs code running from PSRAM (`CONFIG_SPIRAM_XIP_FROM_PSRAM`) or flash auto-suspend. Next: the shared file-write worker (with OTA), a lock per file system, acks after the write. Tests, results, design and costs: devdocs/flash-write-blocking-plan.md
