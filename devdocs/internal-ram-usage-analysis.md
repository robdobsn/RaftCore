# Internal RAM Usage Analysis — Axiom009 (ESP32-S3)

## 1. Platform Overview

| Parameter | Value |
|---|---|
| Target | ESP32-S3 (240 MHz dual-core Xtensa LX7) |
| Internal SRAM | ~512 KB total (DRAM + IRAM, shared DIRAM region) |
| PSRAM | Octal SPI (CONFIG_SPIRAM_MODE_OCT), auto-detect size |
| Flash | 16 MB |
| SPIRAM config | `CAPS_ALLOC` + `USE_MALLOC` + `TRY_ALLOCATE_WIFI_LWIP` |
| BLE stack | NimBLE (host task stack 6144 bytes) |
| Main task stack | 10000 bytes |
| FreeRTOS tick | 1000 Hz |

## 2. Current Static Memory Map (from linker map)

### Summary

| Memory Region | Used (bytes) | Used % | Remaining (bytes) | Total (bytes) |
|---|---|---|---|---|
| **DIRAM** | **158,511** | **46.4%** | **183,249** | **341,760** |
| IRAM | 16,384 | 100% | 0 | 16,384 |
| RTC SLOW | 32 | 0.4% | 8,160 | 8,192 |
| RTC FAST | 24 | 0.3% | 8,168 | 8,192 |
| Flash Code | 1,270,006 | — | — | — |
| Flash Data | 253,972 | — | — | — |

**Key observation:** DIRAM is 46.4% used statically. The remaining ~183 KB is the heap at boot. IRAM is fully consumed.

### DIRAM Breakdown (static)

| Section | Bytes | Notes |
|---|---|---|
| .text (IRAM-eligible code placed in DIRAM) | 107,755 | Functions that run from RAM |
| .bss (zero-initialized globals) | 28,344 | Cleared at boot, comes from heap pool |
| .data (initialized globals) | 22,412 | Copied from flash at boot |

### Top Internal RAM Consumers (static .bss + .data in DIRAM)

| Archive | DIRAM total | .bss | .data | .text |
|---|---|---|---|---|
| libfreertos.a | 19,156 | 757 | 3,106 | 15,293 |
| libpp.a (WiFi PHY) | 18,333 | 1,234 | 2,640 | 14,459 |
| libbtdm_app.a (BT controller) | 16,092 | 684 | 481 | 14,927 |
| libesp_hw_support.a | 8,913 | 205 | 599 | 8,109 |
| libspi_flash.a | 13,524 | 32 | 2,148 | 11,344 |
| libhal.a | 12,404 | 4 | 4,063 | 8,337 |
| libnet80211.a (WiFi MAC) | 12,347 | 7,603 | 936 | 3,808 |
| libphy.a | 8,407 | 86 | 3,272 | 5,049 |
| libbt.a (NimBLE host) | 5,711 | 5,220 | 437 | 54 |
| liblwip.a | 3,866 | 3,850 | 16 | 0 |
| libesp_driver_rmt.a | 2,826 | 12 | 77 | 2,737 |
| libespressif__mdns.a | 2,402 | 2,354 | 48 | 0 |
| libesp_psram.a | 1,980 | 62 | 10 | 1,908 |
| **libmain.a** | **1,064** | **1,064** | 0 | 0 |
| **libRaftCore.a** | **919** | **734** | **132** | 53 |
| **libRaftSysMods.a** | **267** | **216** | **51** | 0 |
| **libRaftI2C.a** | 88 | 0 | 0 | 88 |
| **libDevicePower.a** | 44 | 44 | 0 | 0 |
| **libRaftWebServer.a** | 36 | 36 | 0 | 0 |

## 3. Understanding Dynamic (Heap) RAM Usage

The ~183 KB remaining DIRAM after static allocation is the internal heap at boot. This is consumed at runtime by:

1. **FreeRTOS task stacks** — each task stack is allocated from internal heap (unless SPIRAM-capable malloc is used, stacks MUST be in internal RAM)
2. **WiFi/BT driver buffers** — DMA buffers, TX/RX queues (must be internal/DMA-capable)
3. **lwIP TCP stack** — TCP send/receive buffers (DMA-capable internal RAM)
4. **NVS** — page cache and working buffers
5. **Application heap allocations** — anything using `malloc`/`new` without explicit SPIRAM placement
6. **TLS session state** — mbedTLS contexts during HTTPS/WSS connections

### Known Task Stacks (internal RAM)

| Task | Stack Size (bytes) | Source |
|---|---|---|
| Main task | 10,000 | `sdkconfig.defaults` |
| I2C worker | Configurable (JSON `taskStack`) | `BusI2C.cpp` |
| BLE NimBLE host | 6,144 | `sdkconfig.defaults` |
| BLE outbound msg | Configurable (JSON `taskStack`) | `BLEGattOutbound.cpp` |
| WebServer | 5,000 (default) | `RaftWebServerSettings.h` |
| ESP OTA Update | Configurable | `ESPOTAUpdate.cpp` |
| FreeRTOS IDLE (x2) | ~2,048 each | ESP-IDF default |
| FreeRTOS Timer | ~2,048 | ESP-IDF default |

**Estimated task stack total:** ~30–40 KB of internal RAM.

## 4. Tools & Techniques for Measuring Runtime Internal RAM

### 4.1. Built-in Periodic Monitoring (Already Active)

SysManager's `statsShow()` (called every `monitorPeriodMs` = 10000 ms) logs:
```
{"n":"Axiom009","v":"...","hpInt":<free_internal>,"hpMin":<min_ever_internal>,"hpAll":<free_all>,...}
```

- `hpInt` = `heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)` — current free internal
- `hpMin` = `heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)` — low watermark since boot
- `hpAll` = `heap_caps_get_free_size(MALLOC_CAP_8BIT)` — all 8-bit-capable (internal + SPIRAM)

**Action: Monitor these values in serial output to establish baseline.**

### 4.2. Per-SysMod Setup Heap Delta (Existing, Disabled by Default)

In `SysManager::postSetup()`, there is a `#define DEBUG_SYSMOD_MEMORY_USAGE` (currently commented out) that measures heap before/after each SysMod's `setup()`:

```cpp
// #define DEBUG_SYSMOD_MEMORY_USAGE   // <-- uncomment this
#ifdef DEBUG_SYSMOD_MEMORY_USAGE
    uint32_t heapBefore = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    pSysMod->setup();
    uint32_t heapAfter = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_LOGI(MODULE_PREFIX, "%s setup heap before %d after %d diff %d", ...);
#endif
```

**Action: Enable `DEBUG_SYSMOD_MEMORY_USAGE` to get per-module setup costs.**

### 4.3. ESP-IDF Heap Tracing (Advanced)

For tracking individual allocations to source lines:

```cpp
#include "esp_heap_trace.h"

// In startup (before the code under test):
#define HEAP_TRACE_RECORDS 200
static heap_trace_record_t trace_records[HEAP_TRACE_RECORDS];
heap_trace_init_standalone(trace_records, HEAP_TRACE_RECORDS);

// Around suspicious code:
heap_trace_start(HEAP_TRACE_LEAKS);  // or HEAP_TRACE_ALL
// ... code under test ...
heap_trace_stop();
heap_trace_dump();
```

Requires `CONFIG_HEAP_TRACING_STANDALONE=y` in sdkconfig.defaults.

### 4.4. ESP-IDF `heap_caps_get_info()` — Detailed Fragmentation

```cpp
multi_heap_info_t info;
heap_caps_get_info(&info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
ESP_LOGI(TAG, "Internal heap: free %d, largest_block %d, min_free %d, alloc_blocks %d, free_blocks %d, total_blocks %d",
    info.total_free_bytes, info.largest_free_block,
    info.minimum_free_bytes, info.total_allocated_blocks,
    info.free_blocks, info.total_blocks);
```

The `largest_free_block` value is critical — even with adequate total free bytes, fragmentation can cause allocation failures.

### 4.5. Task Stack High-Water Mark

```cpp
UBaseType_t hwm = uxTaskGetStackHighWaterMark(NULL); // current task
// or for a specific task:
UBaseType_t hwm = uxTaskGetStackHighWaterMark(taskHandle);
ESP_LOGI(TAG, "Stack HWM: %d words (%d bytes) remaining", hwm, hwm * 4);
```

### 4.6. `idf.py size` / `esp_idf_size` (Static Analysis)

```bash
python3 -m esp_idf_size build/Axiom009/Axiom009.map                    # Summary
python3 -m esp_idf_size --archives build/Axiom009/Axiom009.map         # Per-archive
python3 -m esp_idf_size --archive_details libRaftCore.a build/Axiom009/Axiom009.map  # Per-object in archive
```

### 4.7. REST API Endpoint for Runtime Query

The system already provides `/api/sysmoddebug/SysMan` which returns the supervisor stats. Consider adding a dedicated memory endpoint.

## 5. Mitigation Strategies

### 5.1. Move Allocations to SPIRAM

SPIRAM is already enabled with `CONFIG_SPIRAM_USE_MALLOC=y` and `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y`. The codebase already uses `SpiramAwareAllocator` in several places. Review remaining large internal allocations:

- **String buffers** — `String` class uses internal malloc by default. Large JSON responses, config strings, etc. can be significant.
- **std::vector** — default vector uses internal malloc. Use `SpiramAwareAllocator<T>` for large vectors.
- **Ring buffers / queues** — check if any can be moved to SPIRAM (note: DMA buffers cannot).

**Action: Audit large `std::vector`, `String`, and `new[]` allocations for SPIRAM eligibility.**

### 5.2. Reduce Task Stack Sizes

Measure actual stack usage with `uxTaskGetStackHighWaterMark()` and reduce stacks to measured_usage × 1.25 (safety margin).

- Main task: 10,000 bytes — measure and potentially reduce
- I2C worker: check configured value vs actual usage
- BLE NimBLE host: 6,144 — NimBLE recommended minimum; measure before reducing
- WebServer: 5,000 — may be reducible if no TLS on this task

### 5.3. WiFi/BT Memory Optimization

| Technique | sdkconfig Option | Impact |
|---|---|---|
| NimBLE memory in SPIRAM | `CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=y` | Moves NimBLE host allocations to SPIRAM (currently commented out in sdkconfig.defaults!) |
| WiFi IRAM optimization | `CONFIG_ESP_WIFI_IRAM_OPT=n` | Trades WiFi throughput for ~10 KB IRAM |
| WiFi RX IRAM optimization | `CONFIG_ESP_WIFI_RX_IRAM_OPT=n` | Trades WiFi RX performance for ~17 KB IRAM |
| WiFi static RX buffer count | `CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM` | Default 10 × 1600 = 16 KB; reduce if BLE-primary |
| WiFi dynamic RX buffer count | `CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM` | Default 32; reduce for less concurrent traffic |
| WiFi dynamic TX buffer count | `CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM` | Default 32 |
| lwIP TCP send buffer | `CONFIG_LWIP_TCP_SND_BUF_DEFAULT` | Default 5744; reduce cautiously |
| Disable mDNS if unused | Remove/disable `espressif__mdns` | Saves ~2.4 KB static DIRAM + runtime |

### 5.4. Disable Unused Features

Review `SysTypes.json` for enabled features that may not be needed:

- **ESPOTAUpdate** — if OTA is not needed in this build, disable it
- **LogManager** — already `"enable": 0` ✓
- **Specific WebServer websockets** — each open websocket consumes TCP buffers

### 5.5. IRAM Optimization

IRAM is at 100% — if builds fail or there's flash cache contention, consider:

- `CONFIG_COMPILER_OPTIMIZATION_SIZE=y` instead of performance optimization
- Moving less-critical ISR code out of IRAM with `CONFIG_ESP_WIFI_IRAM_OPT=n`

### 5.6. Reduce Static .bss/.data

The top .bss consumers are ESP-IDF WiFi/BT stacks (not easily reducible). Application-level .bss is modest (~2 KB across Raft + app libraries).

## 6. Recommended Measurement Procedure

### Step 1: Enable Per-Module Setup Profiling

Uncomment `#define DEBUG_SYSMOD_MEMORY_USAGE` in `SysManager.cpp` line 35. Build and flash. Record the output showing per-SysMod heap consumption during setup.

### Step 2: Capture Steady-State Baseline

After boot, wait for device to reach steady state (WiFi connected, BLE advertising, I2C scanning complete). Record several `statsShow()` outputs noting `hpInt`, `hpMin`, and `hpAll`.

### Step 3: Measure Under Load

Perform these scenarios and record `hpMin` (all-time low watermark) after each:

1. BLE connect + sustained data streaming
2. WiFi connect + HTTP REST API queries
3. WebSocket connection + sustained devjson streaming
4. OTA update attempt
5. Multiple I2C devices connected and polling at maximum rate
6. Combination: BLE + WiFi + multiple devices

### Step 4: Task Stack Audit

Add temporary stack high-water-mark logging to each task. Record minimum remaining bytes per task.

### Step 5: Fragmentation Check

At steady state and under load, log `heap_caps_get_info()` to check `largest_free_block` vs `total_free_bytes`. If largest block is much smaller than total, fragmentation is an issue.

## 7. Quick-Win Actions (Prioritized)

1. **Enable `CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=y`** — already commented out in sdkconfig.defaults; this alone could save several KB of internal RAM.
2. **Enable `DEBUG_SYSMOD_MEMORY_USAGE`** — zero-cost data collection to identify the largest module consumers.
3. **Add `heap_caps_get_info()` logging** to `statsShow()` to monitor fragmentation.
4. **Audit task stack sizes** with high-water-mark measurements.
5. **Evaluate `CONFIG_ESP_WIFI_IRAM_OPT=n`** if IRAM is limiting.

## 8. Results Log

Record measurement results here as they are collected.

### 8.1. Boot Heap (per-SysMod setup)

_TODO: Enable `DEBUG_SYSMOD_MEMORY_USAGE`, build, flash, and paste output here._

```
# Example expected output format:
# I (xxx) SysMan: <ModuleName> setup heap before <N> after <M> diff <delta>
```

### 8.2. Steady-State Monitoring

_TODO: Record several statsShow() lines after system reaches steady state._

```
# Example:
# I (10000) SysMan: {"n":"Axiom009","v":"...","hpInt":XXXXX,"hpMin":XXXXX,"hpAll":XXXXX,...}
```

### 8.3. Under-Load Scenarios

| Scenario | hpInt (free) | hpMin (watermark) | hpAll | largest_free_block | Notes |
|---|---|---|---|---|---|
| Idle (WiFi+BLE adv) | | | | | |
| BLE connected + streaming | | | | | |
| WiFi HTTP queries | | | | | |
| WebSocket streaming | | | | | |
| Multi-device I2C polling | | | | | |
| Combined load | | | | | |

### 8.4. Task Stack High-Water Marks

| Task | Stack Size | HWM Remaining | Actual Usage | Reducible To |
|---|---|---|---|---|
| Main | 10,000 | | | |
| I2C Worker | | | | |
| BLE NimBLE Host | 6,144 | | | |
| BLE Outbound | | | | |
| WebServer | 5,000 | | | |
| ESP OTA | | | | |

### 8.5. Actions Taken

| Date | Action | Impact (bytes saved) | Notes |
|---|---|---|---|
| | | | |
