# Todo

- [ ] consider sidecar files for device type info instead of embedded notes
- [ ] implement a mechanism that allows coord system transforms for devices like rotation/inversion/etc - either as an expression or as a named transformation
- [ ] consider compressing device types json
- [ ] VL53L0X reference SPAD setup from its NVM (omitted from the record, losing accuracy and range): needs read-modify-writes partway through init, so init code computing values at more than one point (run again before each slot step), and a VL53L0X to test. Design: devdocs/computed-init-values.md ("Not done: VL53L0X SPAD setup")
- [ ] flash writes blocking the main loop: file uploads, `filehash`, Axiom DataLogger writes and the cache refresh after a file close are over the 10 ms target. Measured 2026-10-24: a write task removes SD pauses; LittleFS also needs code running from PSRAM (`CONFIG_SPIRAM_XIP_FROM_PSRAM`) or flash auto-suspend. Next: the shared file-write worker (with OTA), a lock per file system, acks after the write. Tests, results, design and costs: devdocs/flash-write-blocking-plan.md

## Parked

Investigated and judged feasible, but not on any schedule at present.

- [ ] VL53L4CX histogram ranging (up to about 6 m): ST's driver works out the distances from histograms on the host, which a record can't do; and the VL53L4CX reads the same identification as the VL53L4CD (0xEBAA), so it is identified as a VL53L4CD and runs that record's on-chip ranging (short range only). Needs a SysMod with ST's driver or a compiled processing hook, and a way to tell the two apart. Parked 2026-10-26. Details and decisions: devdocs/vl53l4cx-histogram-ranging-plan.md
- [ ] VL53L5CX (and other firmware-loaded I2C devices) as ordinary device types instead of the Axiom's `VL53LxCX` SysMod: records name binary "assets" (the sensor firmware), carried with records, compiled in or put in the file system image, or uploaded with a type file; plus generic init steps (asset slices written in chunks, waits on a register, init bus speed, array init values), reads over 240 bytes and conditional polls. Parked 2026-10-25; the Axiom SysMod works meanwhile. Design, decisions and what to re-check before resuming: devdocs/firmware-loaded-i2c-devices-plan.md
