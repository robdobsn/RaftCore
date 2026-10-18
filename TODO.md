# Todo

- [ ] implement a mechanism that allows coord system transforms for devices like rotation/inversion/etc - either as an expression or as a named transformation
- [ ] consider compressing device types json
- [x] init values computed from values read from the device (`initCode`) - done 2026-10-18 for the BME680/BME690 heater; the VL53L0X SPAD setup still needs multi-phase support, so records can write per-unit settings: the BME680 gas heater set-point (the record uses a fixed typical value, so the hot plate temperature differs between units and gas resistance isn't comparable between them) and the VL53L0X reference SPAD setup (omitted, losing accuracy and range). Design: devdocs/computed-init-values.md (section 2, init code; section 3, where it goes)
