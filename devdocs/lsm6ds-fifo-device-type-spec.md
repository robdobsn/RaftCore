# LSM6DS FIFO-Enabled Device Type Record — Specification

## Overview

This document specifies a new `LSM6DS_FIFO` device type record for the LSM6DS family of 6-axis IMUs (LSM6DS3, LSM6DS33, LSM6DSM, LSM6DSOX) with FIFO enabled, configurable ODR (Output Data Rate) actions, and custom pseudo-code to decode batched FIFO poll responses. The goal is to support higher sampling rates without data loss by buffering samples in the device's internal FIFO.

### Problem with the Current LSM6DS Record

The existing `LSM6DS` record uses direct register reads (`0x22=r12`) to fetch one 12-byte sample per poll cycle. At the default 104 Hz ODR with a 100 ms poll interval (`"i": 100`), only ~10 samples can be captured per second by the host—even though the device is producing 104 samples/s internally. The remaining samples are silently discarded as only the latest output register values are retained. Increasing the poll rate to match the ODR (e.g. `"i": 10`) is wasteful of I2C bus bandwidth and host CPU, and is impractical at higher ODRs (208 Hz+).

### Solution: FIFO Mode

The LSM6DS FIFO can buffer up to 4096 bytes (depending on variant), equivalent to ~341 gyro+accel sample pairs (12 bytes each). By enabling FIFO in continuous mode and reading batched samples each poll cycle, the host can retrieve all accumulated samples in a single I2C burst read, enabling reliable high-rate capture with a relaxed poll interval.

---

## LSM6DS Register Reference

The following registers are relevant to the FIFO-enabled configuration. Register addresses are consistent across the LSM6DS3/DS33/DSM/DSOX family unless noted.

| Register | Address | Description |
|----------|---------|-------------|
| FIFO_CTRL1 | 0x06 | FIFO threshold low byte (watermark) |
| FIFO_CTRL2 | 0x07 | FIFO threshold high bits [2:0], FIFO TEMP enable |
| FIFO_CTRL3 | 0x08 | Gyro & Accel decimation / batching for FIFO |
| FIFO_CTRL4 | 0x09 | FIFO_CTRL4: only-high, FIFO timestamp decimation (DS3); BDR for DSOX |
| FIFO_CTRL5 | 0x0A | FIFO mode & ODR (DS3/DS33/DSM); not present on DSOX |
| CTRL1_XL | 0x10 | Accel ODR and full-scale selection |
| CTRL2_G | 0x11 | Gyro ODR and full-scale selection |
| CTRL3_C | 0x12 | Control register 3 (BDU, IF_INC, etc.) |
| CTRL6_C | 0x16 | High-performance mode disable, etc. |
| FIFO_STATUS1 | 0x3A | Num unread words low byte |
| FIFO_STATUS2 | 0x3B | Num unread words high bits, FIFO full/overrun/watermark flags |
| FIFO_DATA_OUT_L | 0x3E | FIFO data output low byte |
| FIFO_DATA_OUT_H | 0x3F | FIFO data output high byte |

### FIFO Modes (FIFO_CTRL5 bits [2:0] on DS3/DSM)

| Value | Mode | Description |
|-------|------|-------------|
| 0b000 | Bypass | FIFO disabled |
| 0b001 | FIFO | Stop when full |
| 0b011 | Continuous-to-FIFO | Continuous until trigger, then FIFO |
| 0b100 | Bypass-to-Continuous | Bypass until trigger, then Continuous |
| 0b110 | Continuous | Overwrite oldest on overflow (recommended) |

### FIFO ODR (FIFO_CTRL5 bits [6:3] on DS3/DSM)

| Value | ODR (Hz) |
|-------|----------|
| 0x0 | Disabled |
| 0x1 | 12.5 |
| 0x2 | 26 |
| 0x3 | 52 |
| 0x4 | 104 |
| 0x5 | 208 |
| 0x6 | 416 |
| 0x7 | 833 |
| 0x8 | 1660 |
| 0x9 | 3330 |
| 0xA | 6660 |

### Accel ODR (CTRL1_XL bits [7:4])

| Value | ODR (Hz) |
|-------|----------|
| 0x1 | 12.5 |
| 0x2 | 26 |
| 0x3 | 52 |
| 0x4 | 104 |
| 0x5 | 208 |
| 0x6 | 416 |
| 0x7 | 833 |
| 0x8 | 1660 |
| 0x9 | 3330 |
| 0xA | 6660 |

### Gyro ODR (CTRL2_G bits [7:4])

Same mapping as Accel ODR above.

### FIFO_CTRL3: Accel & Gyro Decimation (DS3/DSM)

Bits [2:0] = Gyro decimation, Bits [5:3] = Accel decimation.

| Value | Decimation |
|-------|------------|
| 0b000 | Sensor not in FIFO |
| 0b001 | No decimation (every sample) |
| 0b010 | Factor 2 |
| 0b011 | Factor 3 |
| 0b100 | Factor 4 |
| etc. | ... |

For matched accel+gyro with no decimation: `FIFO_CTRL3 = 0x09` (bits [5:3]=001, bits [2:0]=001).

---

## New Device Type Record: `LSM6DS_FIFO`

### Full Record

```json
"LSM6DS_FIFO": {
    "_notes": "FIFO-enabled 6-axis IMU. Init: 0x1248 (Accel 104Hz, FS ±4g), 0x134c (Gyro 104Hz, FS 2000dps), 0x1640 (high-perf mode), 0x0809 (accel+gyro to FIFO, no decimation), 0x0a26 (FIFO continuous mode, 104Hz ODR). Poll: read FIFO_STATUS (0x3a=r2) then FIFO data (0x3e=rN). Detection same as LSM6DS. WHOAMI 0x0f = 0x69 (DS3/DS33), 0x6a (DSM), 0x6c (DSOX).",
    "addresses": "0x6a",
    "deviceType": "LSM6DS_FIFO",
    "detectionValues": "0x0f=0x69,0x6a,0x6c",
    "initValues": "0x0a00&0x1248&0x134c&0x1640&0x0809&0x0a26",
    "pollInfo": {
        "c": "0x3a=r2&0x3e=r192",
        "i": 50,
        "s": 16
    },
    "scanPriority": "high",
    "devInfoJson": {
        "name": "LSM6DS FIFO",
        "desc": "6-Axis IMU (FIFO)",
        "manu": "ST",
        "type": "LSM6DS_FIFO",
        "clas": ["ACC", "GYRO"],
        "resp": {
            "b": 194,
            "a": [
                {
                    "n": "gx",
                    "t": "<h",
                    "u": "dps",
                    "r": [-2000, 2000],
                    "d": 16.384,
                    "f": ".2f",
                    "o": "float"
                },
                {
                    "n": "gy",
                    "t": "<h",
                    "u": "dps",
                    "r": [-2000, 2000],
                    "d": 16.384,
                    "f": ".2f",
                    "o": "float"
                },
                {
                    "n": "gz",
                    "t": "<h",
                    "u": "dps",
                    "r": [-2000, 2000],
                    "d": 16.384,
                    "f": ".2f",
                    "o": "float"
                },
                {
                    "n": "ax",
                    "t": "<h",
                    "u": "g",
                    "r": [-4.0, 4.0],
                    "d": 8192,
                    "f": ".2f",
                    "o": "float"
                },
                {
                    "n": "ay",
                    "t": "<h",
                    "u": "g",
                    "r": [-4.0, 4.0],
                    "d": 8192,
                    "f": ".2f",
                    "o": "float"
                },
                {
                    "n": "az",
                    "t": "<h",
                    "u": "g",
                    "r": [-4.0, 4.0],
                    "d": 8192,
                    "f": ".2f",
                    "o": "float"
                }
            ],
            "c": {
                "n": "lsm6ds_fifo",
                "c": "int N=((buf[1]&0x0F)<<8)|buf[0];if(N>16){N=16;}int k=2;int i=0;while(i<N){out.gx=(buf[k+1]<<8)|buf[k];out.gy=(buf[k+3]<<8)|buf[k+2];out.gz=(buf[k+5]<<8)|buf[k+4];out.ax=(buf[k+7]<<8)|buf[k+6];out.ay=(buf[k+9]<<8)|buf[k+8];out.az=(buf[k+11]<<8)|buf[k+10];k+=12;i++;next;}"
            },
            "us": 9615
        },
        "actions": [
            {
                "n": "_sampleConfig",
                "t": "B",
                "w": "10",
                "r": [12.5, 6660],
                "d": 104,
                "desc": "Accelerometer & Gyro ODR",
                "map": {
                    "12.5": "18",
                    "26": "28",
                    "52": "38",
                    "104": "48",
                    "208": "58",
                    "416": "68",
                    "833": "78",
                    "1660": "88",
                    "3330": "98",
                    "6660": "A8"
                }
            }
        ]
    }
}
```

---

## Detailed Field Explanations

### `initValues`: `"0x0a00&0x1248&0x134c&0x1640&0x0809&0x0a26"`

Initialization is performed as a sequence of I2C register writes, separated by `&`:

| Step | Bytes | Register | Value | Purpose |
|------|-------|----------|-------|---------|
| 1 | `0x0a00` | FIFO_CTRL5 (0x0A) | 0x00 | Disable FIFO first (bypass mode) — clean slate |
| 2 | `0x1248` | CTRL1_XL (0x10) → **Note: written as two bytes starting at 0x12** — correction: see below | | |

**Corrected init sequence using register addresses directly:**

| Step | Hex | Register(s) | Value(s) | Purpose |
|------|-----|-------------|----------|---------|
| 1 | `0x0a00` | FIFO_CTRL5 (0x0A) | 0x00 | Disable FIFO (bypass mode) |
| 2 | `0x1248` | CTRL3_C (0x12) | 0x48 | BDU=1, IF_INC=1 (block data update + auto-increment) |
| 3 | `0x1048` | CTRL1_XL (0x10) | 0x48 | Accel ODR=104 Hz, FS=±4g |
| 4 | `0x114c` | CTRL2_G (0x11) | 0x4C | Gyro ODR=104 Hz, FS=2000 dps |
| 5 | `0x0809` | FIFO_CTRL3 (0x08) | 0x09 | No decimation for accel+gyro in FIFO |
| 6 | `0x0a26` | FIFO_CTRL5 (0x0A) | 0x26 | FIFO continuous mode (0b110), ODR=104 Hz (0x4<<3 = 0x20) → 0x20|0x06 = 0x26 |

> **Note on init order:** FIFO is first disabled (step 1) to flush any stale data. Sensor ODR and scales are configured (steps 2–4), then FIFO routing (step 5), and finally FIFO is enabled in continuous mode (step 6).

### `pollInfo`

```json
"pollInfo": {
    "c": "0x3a=r2&0x3e=r192",
    "i": 50,
    "s": 16
}
```

| Field | Value | Meaning |
|-------|-------|---------|
| `c` | `"0x3a=r2&0x3e=r192"` | Two-part poll: first read 2 bytes from FIFO_STATUS (0x3A–0x3B) to get unread sample count, then read up to 192 bytes from FIFO_DATA_OUT (0x3E) — enough for 16 × 12-byte samples |
| `i` | `50` | Poll interval 50 ms (20 Hz host poll rate) |
| `s` | `16` | Max 16 decoded samples per poll (matches 192/12) |

**Why 50 ms / 16 samples?** At 104 Hz ODR, ~5.2 samples accumulate in 50 ms. Reading 16 provides headroom for timing jitter and higher ODR settings. At 208 Hz, ~10.4 samples/50 ms — still within the 16-sample buffer. For ODRs above 416 Hz, poll interval and read size should be increased via `/devman/devconfig`.

**Why 192 bytes?** Each accel+gyro sample pair in the FIFO occupies 12 bytes (6 bytes gyro + 6 bytes accel). 16 samples × 12 bytes = 192 bytes. This is a conservative default that works well at ≤208 Hz and keeps I2C transaction time reasonable (~1.9 ms at 100 kHz, ~0.5 ms at 400 kHz).

### Custom Decode Pseudo-Code (`c`)

```json
"c": {
    "n": "lsm6ds_fifo",
    "c": "int N=((buf[1]&0x0F)<<8)|buf[0];if(N>16){N=16;}int k=2;int i=0;while(i<N){out.gx=(buf[k+1]<<8)|buf[k];out.gy=(buf[k+3]<<8)|buf[k+2];out.gz=(buf[k+5]<<8)|buf[k+4];out.ax=(buf[k+7]<<8)|buf[k+6];out.ay=(buf[k+9]<<8)|buf[k+8];out.az=(buf[k+11]<<8)|buf[k+10];k+=12;i++;next;}"
}
```

#### Pseudo-Code Walkthrough

```
// buf[] = raw poll response: [FIFO_STATUS1, FIFO_STATUS2, FIFO_DATA...]

// Step 1: Extract unread sample count from FIFO_STATUS registers
//   FIFO_STATUS1 (buf[0]) = low 8 bits of unread word count
//   FIFO_STATUS2 (buf[1]) bits [3:0] = high 4 bits of unread word count
//   On LSM6DS3/DSM: count is in "words" where 1 word = 2 bytes
//   Each gyro+accel sample = 6 words (12 bytes)
//   But since we read matched gyro+accel pairs, N here represents
//   the number of word-pairs available, which we divide implicitly
//   by treating 12-byte chunks as one sample.
int N = ((buf[1] & 0x0F) << 8) | buf[0];

// Step 2: Clamp to max samples we can fit in our read buffer
//   We read 192 bytes of FIFO data (16 × 12), so cap at 16
if (N > 16) { N = 16; }

// Step 3: Iterate through FIFO data starting after the 2-byte status
int k = 2;   // byte offset into buf (skip FIFO_STATUS bytes)
int i = 0;

while (i < N) {
    // Each FIFO "set" for gyro+accel = 12 bytes, little-endian int16:
    //   Bytes [k+0..k+1] = Gyro X
    //   Bytes [k+2..k+3] = Gyro Y
    //   Bytes [k+4..k+5] = Gyro Z
    //   Bytes [k+6..k+7] = Accel X
    //   Bytes [k+8..k+9] = Accel Y
    //   Bytes [k+10..k+11] = Accel Z
    out.gx = (buf[k+1] << 8) | buf[k];
    out.gy = (buf[k+3] << 8) | buf[k+2];
    out.gz = (buf[k+5] << 8) | buf[k+4];
    out.ax = (buf[k+7] << 8) | buf[k+6];
    out.ay = (buf[k+9] << 8) | buf[k+8];
    out.az = (buf[k+11] << 8) | buf[k+10];

    k += 12;  // advance to next sample
    i++;
    next;     // emit this decoded sample to the output buffer
}
```

#### Key Conventions (matching MAX30101 pattern)

- `buf[]` — raw bytes from the poll response (FIFO_STATUS bytes followed by FIFO data bytes)
- `out.<field>` — assign to named output fields matching the `"a"` array definitions
- `next;` — emit the current `out` values as a completed sample record; the framework advances the output buffer

### Sample Timing: `"us": 9615`

At 104 Hz ODR, the inter-sample period is 1/104 ≈ 9615 µs. This tells the framework the time delta between consecutive decoded samples when generating timestamps. When a poll returns N samples, the framework assigns timestamps:
- Sample 0: `poll_timestamp - (N-1) × us`
- Sample 1: `poll_timestamp - (N-2) × us`
- ...
- Sample N-1: `poll_timestamp`

This field should be updated when ODR changes (see `_sampleConfig` action below).

### `_sampleConfig` Action

```json
{
    "n": "_sampleConfig",
    "t": "B",
    "w": "10",
    "r": [12.5, 6660],
    "d": 104,
    "desc": "Accelerometer & Gyro ODR",
    "map": {
        "12.5": "18",
        "26": "28",
        "52": "38",
        "104": "48",
        "208": "58",
        "416": "68",
        "833": "78",
        "1660": "88",
        "3330": "98",
        "6660": "A8"
    }
}
```

This action writes to CTRL1_XL (0x10) to set the accelerometer ODR. The map values encode `ODR[7:4] | FS_XL[3:2]=10 (±4g) | BW_FILT[1:0]=00`:

| Desired Hz | Map Value | CTRL1_XL Byte | ODR bits [7:4] | FS = ±4g [3:2] |
|------------|-----------|---------------|----------------|-----------------|
| 12.5 | `18` | 0x18 | 0001 | 10 |
| 26 | `28` | 0x28 | 0010 | 10 |
| 52 | `38` | 0x38 | 0011 | 10 |
| 104 | `48` | 0x48 | 0100 | 10 |
| 208 | `58` | 0x58 | 0101 | 10 |
| 416 | `68` | 0x68 | 0110 | 10 |
| 833 | `78` | 0x78 | 0111 | 10 |
| 1660 | `88` | 0x88 | 1000 | 10 |
| 3330 | `98` | 0x98 | 1001 | 10 |
| 6660 | `A8` | 0xA8 | 1010 | 10 |

**Important:** When changing ODR, multiple registers should be updated in concert:
1. **CTRL1_XL** (0x10) — Accel ODR (this action)
2. **CTRL2_G** (0x11) — Gyro ODR should match (requires a second action or combined write)
3. **FIFO_CTRL5** (0x0A) — FIFO ODR should match the sensor ODR
4. **`us` field** — inter-sample timestamp should be recalculated
5. **`pollInfo.i` and `pollInfo.s`** — may need adjustment via `/devman/devconfig`

For a complete implementation, consider separate actions for gyro ODR and FIFO ODR:

```json
"actions": [
    {
        "n": "_sampleConfig.accel",
        "t": "B",
        "w": "10",
        "r": [12.5, 6660],
        "d": 104,
        "desc": "Accelerometer ODR",
        "map": {
            "12.5": "18", "26": "28", "52": "38", "104": "48",
            "208": "58", "416": "68", "833": "78", "1660": "88",
            "3330": "98", "6660": "A8"
        }
    },
    {
        "n": "_sampleConfig.gyro",
        "t": "B",
        "w": "11",
        "r": [12.5, 6660],
        "d": 104,
        "desc": "Gyroscope ODR",
        "map": {
            "12.5": "1C", "26": "2C", "52": "3C", "104": "4C",
            "208": "5C", "416": "6C", "833": "7C", "1660": "8C",
            "3330": "9C", "6660": "AC"
        }
    },
    {
        "n": "_sampleConfig.fifo",
        "t": "B",
        "w": "0a",
        "r": [12.5, 6660],
        "d": 104,
        "desc": "FIFO ODR (should match sensor ODR)",
        "map": {
            "12.5": "0E", "26": "16", "52": "1E", "104": "26",
            "208": "2E", "416": "36", "833": "3E", "1660": "46",
            "3330": "4E", "6660": "56"
        }
    }
]
```

---

## FIFO Word Count Interpretation

### LSM6DS3 / LSM6DS33 / LSM6DSM

On these variants, the FIFO stores data in **16-bit words**. FIFO_STATUS1/2 report the count of unread words. With both gyro and accel routed to FIFO (no decimation), each complete sample set uses **6 words** (3 gyro + 3 accel = 12 bytes). The data is read word-by-word from FIFO_DATA_OUT_L/H (0x3E/0x3F), or in a burst read.

The sample count N in the pseudo-code therefore needs to account for this:

```
int words = ((buf[1] & 0x0F) << 8) | buf[0];
int N = words / 6;  // 6 words per gyro+accel sample set
```

**Updated pseudo-code (corrected for word-based count):**

```
"c": "int W=((buf[1]&0x0F)<<8)|buf[0];int N=W/6;if(N>16){N=16;}int k=2;int i=0;while(i<N){out.gx=(buf[k+1]<<8)|buf[k];out.gy=(buf[k+3]<<8)|buf[k+2];out.gz=(buf[k+5]<<8)|buf[k+4];out.ax=(buf[k+7]<<8)|buf[k+6];out.ay=(buf[k+9]<<8)|buf[k+8];out.az=(buf[k+11]<<8)|buf[k+10];k+=12;i++;next;}"
```

### LSM6DSOX

On the DSOX, FIFO uses a **tag-based** system where each word has a tag byte identifying the data source. This would require a different decode approach and is out of scope for this initial record (the DSOX may need its own device type record).

---

## Recommended Poll Configuration by ODR

| ODR (Hz) | `us` (µs) | Samples/50ms | Recommended `i` (ms) | Recommended `s` | Read bytes | Notes |
|-----------|-----------|-------------|----------------------|-----------------|------------|-------|
| 12.5 | 80000 | ~0.6 | 200 | 4 | 48+2 | Low-power mode |
| 26 | 38461 | ~1.3 | 200 | 8 | 96+2 | |
| 52 | 19230 | ~2.6 | 100 | 8 | 96+2 | |
| 104 | 9615 | ~5.2 | 50 | 16 | 192+2 | **Default** |
| 208 | 4807 | ~10.4 | 50 | 16 | 192+2 | |
| 416 | 2403 | ~20.8 | 50 | 32 | 384+2 | Increase read size |
| 833 | 1200 | ~41.6 | 25 | 32 | 384+2 | High bandwidth |

For ODRs ≥416 Hz, the poll read size (`0x3e=r192`) and `s` value should be increased via `/devman/devconfig` to avoid FIFO overflow:

```
GET /devman/devconfig?deviceid=<id>&intervalUs=25000&numSamples=32
```

---

## Changes from Existing LSM6DS Record

| Aspect | Current `LSM6DS` | New `LSM6DS_FIFO` |
|--------|------------------|---------------------|
| FIFO | Disabled (`0x0a00`) | Continuous mode (`0x0a26`) |
| FIFO routing | None | Accel+Gyro, no decimation (`0x0809`) |
| BDU / IF_INC | Set (`0x1640`) | Set (`0x1248` for CTRL3_C, `0x1640` for CTRL6_C) |
| Poll command | `0x22=r12` (direct read) | `0x3a=r2&0x3e=r192` (status + FIFO burst) |
| Poll interval | 100 ms | 50 ms |
| Max samples/poll | 3 | 16 |
| Custom decode | None | `lsm6ds_fifo` pseudo-code |
| Sample timing | None (`us` absent) | `"us": 9615` (104 Hz) |
| ODR actions | None | `_sampleConfig` for accel/gyro/FIFO ODR |
| Response size | 12 bytes | 194 bytes (2 status + 192 data) |

---

## Implementation Considerations

### 1. FIFO Flush on Init

The init sequence starts by disabling the FIFO (`0x0a00`) to clear any stale data from a previous session. This is important because the device may retain FIFO contents across soft resets depending on the variant.

### 2. FIFO Overrun

If the host doesn't poll fast enough, the FIFO will overrun in continuous mode (oldest data overwritten). The FIFO_STATUS2 register bit [6] (FIFO_OVR) can be checked to detect this. The pseudo-code currently does not check this flag but could be extended:

```
// Optionally check overrun: buf[1] & 0x40
```

### 3. DSOX Compatibility

The LSM6DSOX uses a different FIFO architecture (tagged words, different registers for FIFO control). A separate `LSM6DSOX_FIFO` device type record may be needed. The detection values should be split or the DSOX excluded from this record if FIFO behavior differs.

### 4. Coordinated ODR Changes

When the user changes the ODR via `_sampleConfig`, ideally all three registers (CTRL1_XL, CTRL2_G, FIFO_CTRL5) should be updated together. The current framework processes actions individually. Two approaches:

- **Multiple actions:** Define separate `_sampleConfig.accel`, `_sampleConfig.gyro`, `_sampleConfig.fifo` actions and have the client set all three.
- **Compound action (future):** Extend the framework to support a single action that writes to multiple registers.

### 5. Dynamic `us` Adjustment

The `us` field is static in the record. When ODR changes, the timestamp spacing between decoded samples will be incorrect unless `us` is also updated. This could be addressed by:

- Having the framework automatically compute `us = 1000000 / ODR` when `_sampleConfig` is applied
- Or having the client explicitly set it via `/devman/devconfig`

---

## Updated Record (Corrected Pseudo-Code)

Incorporating the word-count correction:

```json
"LSM6DS_FIFO": {
    "_notes": "FIFO-enabled 6-axis IMU. FIFO continuous mode at matched sensor ODR. Poll reads FIFO status (word count) then burst-reads FIFO data. Decode divides word count by 6 (3 gyro + 3 accel words per sample). Compatible with LSM6DS3, LSM6DS33, LSM6DSM. DSOX uses different FIFO architecture.",
    "addresses": "0x6a",
    "deviceType": "LSM6DS_FIFO",
    "detectionValues": "0x0f=0x69,0x6a",
    "initValues": "0x0a00&0x1248&0x1048&0x114c&0x1640&0x0809&0x0a26",
    "pollInfo": {
        "c": "0x3a=r2&0x3e=r192",
        "i": 50,
        "s": 16
    },
    "scanPriority": "high",
    "devInfoJson": {
        "name": "LSM6DS FIFO",
        "desc": "6-Axis IMU (FIFO)",
        "manu": "ST",
        "type": "LSM6DS_FIFO",
        "clas": ["ACC", "GYRO"],
        "resp": {
            "b": 194,
            "a": [
                {
                    "n": "gx",
                    "t": "<h",
                    "u": "dps",
                    "r": [-2000, 2000],
                    "d": 16.384,
                    "f": ".2f",
                    "o": "float"
                },
                {
                    "n": "gy",
                    "t": "<h",
                    "u": "dps",
                    "r": [-2000, 2000],
                    "d": 16.384,
                    "f": ".2f",
                    "o": "float"
                },
                {
                    "n": "gz",
                    "t": "<h",
                    "u": "dps",
                    "r": [-2000, 2000],
                    "d": 16.384,
                    "f": ".2f",
                    "o": "float"
                },
                {
                    "n": "ax",
                    "t": "<h",
                    "u": "g",
                    "r": [-4.0, 4.0],
                    "d": 8192,
                    "f": ".2f",
                    "o": "float"
                },
                {
                    "n": "ay",
                    "t": "<h",
                    "u": "g",
                    "r": [-4.0, 4.0],
                    "d": 8192,
                    "f": ".2f",
                    "o": "float"
                },
                {
                    "n": "az",
                    "t": "<h",
                    "u": "g",
                    "r": [-4.0, 4.0],
                    "d": 8192,
                    "f": ".2f",
                    "o": "float"
                }
            ],
            "c": {
                "n": "lsm6ds_fifo",
                "c": "int W=((buf[1]&0x0F)<<8)|buf[0];int N=W/6;if(N>16){N=16;}int k=2;int i=0;while(i<N){out.gx=(buf[k+1]<<8)|buf[k];out.gy=(buf[k+3]<<8)|buf[k+2];out.gz=(buf[k+5]<<8)|buf[k+4];out.ax=(buf[k+7]<<8)|buf[k+6];out.ay=(buf[k+9]<<8)|buf[k+8];out.az=(buf[k+11]<<8)|buf[k+10];k+=12;i++;next;}"
            },
            "us": 9615
        },
        "actions": [
            {
                "n": "_sampleConfig.accel",
                "t": "B",
                "w": "10",
                "r": [12.5, 6660],
                "d": 104,
                "desc": "Accelerometer ODR",
                "map": {
                    "12.5": "18", "26": "28", "52": "38", "104": "48",
                    "208": "58", "416": "68", "833": "78", "1660": "88",
                    "3330": "98", "6660": "A8"
                }
            },
            {
                "n": "_sampleConfig.gyro",
                "t": "B",
                "w": "11",
                "r": [12.5, 6660],
                "d": 104,
                "desc": "Gyroscope ODR",
                "map": {
                    "12.5": "1C", "26": "2C", "52": "3C", "104": "4C",
                    "208": "5C", "416": "6C", "833": "7C", "1660": "8C",
                    "3330": "9C", "6660": "AC"
                }
            },
            {
                "n": "_sampleConfig.fifo",
                "t": "B",
                "w": "0a",
                "r": [12.5, 6660],
                "d": 104,
                "desc": "FIFO ODR (match sensor ODR)",
                "map": {
                    "12.5": "0E", "26": "16", "52": "1E", "104": "26",
                    "208": "2E", "416": "36", "833": "3E", "1660": "46",
                    "3330": "4E", "6660": "56"
                }
            }
        ]
    }
}
```

---

## API Usage Examples

### Set ODR to 208 Hz with Coordinated Polling

```
GET /devman/devconfig?deviceid=1_6a&sampleRateHz.accel=208&sampleRateHz.gyro=208&sampleRateHz.fifo=208&intervalUs=50000&numSamples=16
```

### Query Current Configuration

```
GET /devman/devconfig?deviceid=1_6a
```

### High-Rate Capture at 416 Hz

```
GET /devman/devconfig?deviceid=1_6a&sampleRateHz.accel=416&sampleRateHz.gyro=416&sampleRateHz.fifo=416&intervalUs=25000&numSamples=32
```

> **Note:** At 416 Hz with 25 ms poll interval, ~10.4 samples accumulate. The FIFO read size of 192 bytes (16 samples) provides adequate headroom. For 833 Hz, consider increasing the read buffer.
