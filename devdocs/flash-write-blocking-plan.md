# Flash writes blocking the main loop - tests and plan

Status: **measured** (2026-10-24). Tests 1 and 2 have been run on an ESP32-S3 with an SD card (see
[Measurements](#measurements-2026-10-24)): a write task plus code running from PSRAM keeps the main loop under 10 ms
for both LittleFS and SD writes. Nothing is implemented yet.

## The problem

The main loop should not be blocked for more than 10 ms (50 ms is the absolute maximum). File system writes
still block it for longer. All of these figures were measured on Axiom018 (ESP32-S3, 16 MB flash in DIO mode,
8 MB octal PSRAM, LittleFS):

| Where | What blocks | Measured |
|---|---|---|
| File upload (all transports: HTTP, BLE, serial) | `FileStreamSession` -> `FileSystemChunker::nextWrite` writes each received block in the caller's loop (for HTTP, inside `RaftWebConnection::loop`) | 13-42 ms per 1440-byte HTTP chunk |
| Axiom DataLogger | `writeFlushChunk` writes 1 KB per `loop()` call | 2.3 ms average, 13 ms typical worst, 50 ms for the first write after a session starts (the file's first block) |
| Closing a file | the FileSystem cache refresh after a close (`fileSystemCacheService`, run from FileManager's loop) | 88 ms at the end of a DataLogger session |

The DataLogger figures are from logging an LSM6DS3 at 416 Hz to CSV (17 KB/s); the upload figures from
uploading a 61 KB type file over HTTP.

Uploads to an SD card (FAT over SPI) block it too, and for longer: the plant growth board showed 300-900 ms per
16 KB WebSocket block in some runs (2026-10-23). Most of the longest pause turned out to be `filehash` (see below).

## Why a task alone may not fix it

On this build a flash program or erase **disables the cache on both cores** for the duration of the operation:
every task running code from flash stops, whichever task asked for the write (only IRAM code, e.g. the I2C ISR,
keeps running). Relevant settings in the Axiom018 sdkconfig:

- `CONFIG_SPI_FLASH_YIELD_DURING_ERASE=y`, `CONFIG_SPI_FLASH_ERASE_YIELD_DURATION_MS=20` - an erase yields every
  20 ms, but a single 4 KB sector erase can't be split (typically tens of ms on this class of flash chip)
- `CONFIG_SPI_FLASH_WRITE_CHUNK_SIZE=8192` - up to 8 KB is programmed per cache-disabled window
- code is not run from PSRAM (`CONFIG_SPIRAM_XIP_FROM_PSRAM` is off)

So a write's time in the main loop is two parts:

1. **Software** - LittleFS bookkeeping (metadata, CRCs, its block cache), VFS and partition layers. Moving the
   write to another task takes this out of the main loop.
2. **Hardware stall** - the cache-disabled windows of the program and erase operations. This stops the main loop
   wherever the write runs. The 13-50 ms spikes are probably mostly this (sector erases), but the split hasn't
   been measured.

## Tests

### Test 1 - how much is the hardware stall?

Measure the split before building anything.

- In a test build, have the DataLogger write its chunks from a temporary task (internal-RAM stack, priority below
  the main task) instead of `loop()`.
- Log an LSM6DS3 at 416 Hz to CSV for 30 s (`devman/devconfig?deviceid=1_76a&sampleRateHz=416`, then
  `datalog?action=start&config={"format":"csv","devices":[{"addr":"76a"}]}`), as for the measurements above.
- Compare the SysMan loop statistics (`maxUs`, `slowUs`) and the `SLOW` warnings with the same run writing from
  `loop()`.
- What's left in the main loop is the hardware stall.

### Test 2 - running code from PSRAM

`CONFIG_SPIRAM_XIP_FROM_PSRAM=y` (the ESP32-S3 supports it: `CONFIG_SOC_SPIRAM_XIP_SUPPORTED` is set). The app's
code and read-only data are copied to PSRAM at boot, so flash operations shouldn't need to disable the cache, and
other tasks should keep running during writes and erases.

- **First confirm**, from the ESP-IDF v6.1 documentation and source for this mode, that the cache really stays
  enabled during flash operations, and whether anything (IRAM-safe ISRs, `esp_flash` callers, the I2C driver's
  bounce buffer) relies on the old behaviour.
- Build with it on, then re-run Test 1's logging run (writes from `loop()` and from the task) and a 61 KB HTTP
  upload. Record the main loop's `maxUs` and the `SLOW` warnings for each.
- Also check:
  - the boot time, since the image is copied to PSRAM;
  - the free PSRAM (`hpAll`), expected to drop by about the app image size (1.98 MB);
  - performance-sensitive paths: the bus poll timing (`devman/busstatus`) and the decoder timing
    (`devman/decodeverify`, in a build with `RAFT_RUNTIME_DECODE_VERIFY`).

## Measurements (2026-10-24)

**Board:** Axiom plant growth board, an ESP32-S3 with 2 MB of quad PSRAM. LittleFS on the board's flash, and an SD card
(FAT over SPI, 20 MHz). Flash settings as above (`CONFIG_SPI_FLASH_WRITE_CHUNK_SIZE=8192`, yield during erase).

**Method:** a test build (a throwaway app, not committed):
- `app_main` times each pass of `raftCoreApp.loop()`: the longest, and how many are over 10, 50 and 200 ms.
- A test endpoint writes a 200 KB file either from the main loop (one chunk per `loop()` call, as an upload does) or
  from a task (4 KB stack in internal RAM, priority 1, on core 0; the main task is on core 1). It writes through
  `FileSystem::fileWrite`, so it takes the same lock as uploads.

When idle, the longest main loop pass is 6 ms.

### Writing from the main loop

| File system | Chunk | Average write | Longest write | Longest main loop pass |
|-|-|-|-|-|
| SD | 800 | 8 ms | 102 ms | 103 ms |
| SD | 4000 | 11 ms | 14 ms | 26 ms |
| SD | 16000 | 25 ms | 31 ms | 37 ms |
| LittleFS | 800 | 16 ms | 162 ms | 165 ms |
| LittleFS | 4000 | 78 ms | 142 ms | 144 ms |
| LittleFS | 16000 | 324 ms | 361 ms | 367 ms |

LittleFS is slow once per 4 KB flash block: the 200 KB file gave 50 passes over 50 ms at every chunk size, about
80-160 ms each.

### Writing from a task (Test 1)

| File system | Chunk | Longest main loop pass | Passes over 50 ms |
|-|-|-|-|
| SD | 4000 | 6 ms | 0 |
| SD | 16000 | 6 ms | 0 |
| LittleFS | 800 | 154 ms | 49 |
| LittleFS | 4000 | 93 ms | 51 |
| LittleFS | 16000 | 94 ms | 51 |

- **SD:** the pause is gone. The card is on its own SPI bus, so the main loop runs while the task waits for it.
- **LittleFS:** about 90-150 ms per 4 KB flash block is left. That is the hardware stall: erasing and programming the
  board's flash turns the cache off for every task.
- The task used about 2.1 KB of its 4 KB stack (`uxTaskGetStackHighWaterMark` left 1.9-2.2 KB).

### Code from PSRAM (Test 2)

`CONFIG_SPIRAM_XIP_FROM_PSRAM=y`. The ESP-IDF v6.1 documentation (`spi_flash_concurrency.rst`) confirms that in this
mode the cache stays enabled while the flash is erased or written.

| File system | Chunk | Where | Longest main loop pass | Passes over 50 ms |
|-|-|-|-|-|
| LittleFS | 4000 | task | 6 ms | 0 |
| LittleFS | 16000 | task | 6 ms | 0 |
| LittleFS | 4000 | main loop | 138 ms | 50 |
| SD | 16000 | task | 6 ms | 0 |

- **With a task, the pause is gone for LittleFS too.** From the main loop it isn't: the main loop still waits for its
  own write.
- **Costs on this board:**
  - free memory over all heaps fell from 2.2 MB to 0.6 MB, since the 1.66 MB app is copied to PSRAM;
  - internal RAM is about the same (160 KB free, against 163 KB);
  - no visible change in boot time (the app starts at about 1.19 s either way).

  On a 2 MB PSRAM board that leaves little PSRAM; on the Axiom (8 MB) it is a quarter.

### Axiom018 (ESP32-S3-WROOM-1-N16R8)

The same test build on the Axiom (16 MB quad flash in DIO mode, 8 MB octal PSRAM; flash ID manufacturer `0x46`,
device `0x4018`). Its flash is much faster than the plant growth board's: 200 KB takes 0.5-1.2 s, not 4.3 s.

| | Code from flash | Code from PSRAM (XIP) |
|-|-|-|
| Idle: longest main loop pass | 8.0 ms | 3.0 ms |
| Idle: main loop passes in 5 s | 3131 | 5153 |
| 200 KB from the main loop, 4 KB chunks: longest pass | 19 ms | 27 ms |
| 200 KB from the main loop, 16 KB chunks: longest pass | 52 ms | 43 ms |
| 200 KB from a task, 800 B / 4 KB / 16 KB chunks: longest pass | 12.4 / 12.3 / 12.5 ms | 4.0 / 4.0 / 4.0 ms |
| 200 KB from a task, 4 KB chunks: time taken | 1.18 s | 0.65 s |
| I2C bus (`devman/busstatus`): busy, longest pass | 43.5 %, 9.3 ms | 35.8 %, 5.2 ms |
| Free memory: internal, all heaps | 132 KB, 8.38 MB | 139 KB, 6.47 MB |

- **With XIP and a task, writes no longer show in the main loop at all** (longest pass 4 ms, none over 10 ms).
  Without XIP a task leaves about 12 ms, a little over the target.
- **XIP made the Axiom faster overall:** the main loop ran about 65 % more passes when idle, and the I2C bus spent
  less time per poll. The code runs from the octal PSRAM rather than the flash in DIO mode. These are single runs;
  repeat them before relying on the figures.
- XIP took 1.9 MB of the 8 MB PSRAM (the app is 2.0 MB), and boot time was no longer (mDNS up at 8.9 s, against 9.5
  s). Free internal RAM went up by 7 KB.

**Decoder timing and DataLogger** (a second pair of builds, with `RAFT_RUNTIME_DECODE_VERIFY` on; 30 s of normal
polling, then the DataLogger run from Test 1: LSM6DS3 at 416 Hz to CSV for 30 s, writing from `loop()` as now):

| | Code from flash | Code from PSRAM (XIP) |
|-|-|-|
| Decoders: mismatches over 786 polls | 0 | 0 |
| LSM6DS3 decode, compiled / runtime: average (max) | 82 (850) / 360 (1779) us | 21 (370) / 152 (648) us |
| MAX17048 decode, compiled / runtime: average (max) | 64 (99) / 193 (292) us | 14 (14) / 52 (292) us |
| VCNL4040 decode, compiled / runtime: average (max) | 43 (464) / 190 (798) us | 9 (15) / 39 (62) us |
| Decode with warm caches (LSM6DS3, compiled / runtime) | 10 / 110 us | 9 / 100 us |
| I2C bus: busy, longest pass, poll time per second | 46.8 %, 9.1 ms, 152 ms | 36.4 %, 5.6 ms, 83 ms |
| Polling: longest main loop pass, passes in 30 s | 8.0 ms, 18285 | 5.0 ms, 29639 |
| Logging: longest main loop pass, passes over 10 ms | 55 ms, 129 | 35 ms, 91 |
| Logging: main loop passes in 30 s | 13837 | 28491 |
| Logging: flush time average (max) | 2.3 (13.2) ms | 2.0 (11.5) ms |
| Stopping the log: longest main loop pass | 16 ms | 11 ms |

- The decoders give the same results either way. They are 2-5 times faster with XIP on average and in the worst
  case, which is when the code isn't in the cache: cache misses are filled from the octal PSRAM, not the flash in DIO
  mode. With warm caches the times are the same.
- The DataLogger still pauses the main loop with XIP (35 ms), since it writes from `loop()`: it needs the worker.
  Its samples were all logged either way (no buffer overflows).
- The Axiom's flash isn't on ESP-IDF's auto-suspend list (XM25xxD, GD25QxxE, FM25Q32), so XIP is the option for it.

**Flash auto-suspend** is an alternative where PSRAM is short: the ESP32-S3 supports it
(`CONFIG_SOC_SPI_MEM_SUPPORT_AUTO_SUSPEND`). It suspends an erase or program when the cache needs the flash, so the
cache stays enabled. It works only with some flash chips (ESP-IDF: "Optional Features for Flash > Auto Suspend &
Resume"), so each board's flash chip would need checking. Not tested.

### During uploads

200 KB uploads over WebSocket from RaftCLI:

| File system | Block | Speed | Main loop passes over 10 ms | over 50 ms | over 200 ms |
|-|-|-|-|-|-|
| SD | 800 | 29 KB/s | 159 | 5 | 1 |
| SD | 4000 | 52 KB/s | 53 | 1 | 1 |
| SD | 16000 | 57 KB/s | 25 | 14 | 1 |
| LittleFS | 800 | 12 KB/s | 52 | 52 | 1 |
| LittleFS | 4000 | 14 KB/s | 53 | 51 | 1 |
| LittleFS | 16000 | 12 KB/s | 106 | 15 | 14 |

- **The pause over 200 ms is `filehash`, not the upload.** RaftCLI checks each upload with it. It reads the whole
  file and works out its CRC in one main loop pass: 906 ms for 200 KB on SD, 469 ms on LittleFS. It needs splitting
  or moving to the worker too.
- **Upload blocks pause the loop for longer than a raw write of the same size** (on SD, 16 KB blocks over 50 ms
  against 25-31 ms raw). The upload keeps the file open, so the extra time is in receiving the block in the same
  pass: the HDLC decode works a byte at a time, with a CRC per byte, then RICREST handling. This needs profiling.
- RaftCLI and raftjs now ask for 4000-byte WebSocket blocks: nearly as fast as 16000 on SD, with shorter pauses.

### What the results decide

| Test 2 result | Then |
|---|---|
| Spikes gone, average write time in `loop()` acceptable | Enable XIP from PSRAM; the shared worker isn't needed |
| Spikes gone, but the software part still matters (e.g. uploads at 13+ ms per chunk) | Enable XIP from PSRAM and add the shared worker |
| Spikes remain | The worker only helps with the software part; look at the erase behaviour (flash auto-suspend support for this chip, pre-erasing/pre-allocating log files, smaller LittleFS block or cache sizes) |

**Result (2026-10-24):** spikes gone with a task plus code from PSRAM, on both boards (on the Axiom XIP also made
the main loop and the I2C bus faster). So, for boards with enough PSRAM, enable XIP
from PSRAM and add the shared worker. For boards without enough PSRAM, the worker still removes the SD pauses and the
software part of LittleFS writes; check auto-suspend for their flash chips.

## Proposed implementation: one shared file-write worker

One worker task in RaftCore (with `FileSystem`), used by every file writer. File system access is already
serialized by FileSystem's mutex, so one worker loses no throughput compared with several.

### Worker

- A FreeRTOS task with a 4 KB stack in internal RAM. A task that writes to flash can't have its stack in PSRAM.
- It runs below the main task's priority, pinned to core 0 (see the note on priorities below).
- It is created on first use and kept, as the OTA worker is. Repeatedly creating and deleting it would fragment
  internal RAM.
- Jobs arrive on a small queue (e.g. 4 entries). Each job is a write (file handle or path, data in PSRAM, length),
  a close, or a cache refresh, plus a completion flag or callback that the submitter polls from the main task.
- The API, for example:
  - `FileWriteWorker::submit(job)` returns false if the queue is full; that's the back-pressure signal.
  - `isBusy()` reports whether jobs are outstanding.
- **Priority:** at the main task's priority and unpinned, the type-file check task held the main task off for up
  to 300 ms (equal-priority tasks, one pinned and one not, aren't reliably time-sliced in ESP-IDF's SMP FreeRTOS).
  So use a priority below the main task, or pin the worker to core 0.

### Users

- **Uploads (`FileStreamSession` / `FileSystemChunker::nextWrite`):**
  - Copy each received block into one of two PSRAM chunk buffers and submit it.
  - While both are in use, apply back-pressure. For HTTP that's `readyToReceiveData()` returning false: the
    connection already refreshes its idle timer while a responder does this. For BLE and serial (the OKTO
    protocol), hold the OKTO acknowledgement until the block is written.
  - The final block's reply waits for the close job.
- **Axiom DataLogger:**
  - `writeFlushChunk` submits the buffer swapped out for writing, instead of writing it in `loop()`.
  - The swap waits while that buffer's job is outstanding; records are still accepted into the active buffer, as
    now.
  - Stopping waits for the close job before replying, keeping the reply deferred as now.
- **File close cache refresh:** `fileSystemCacheService`'s file-system info and directory scan run as a job.

### Also needed (from the measurements)

- **The FileSystem lock.** FileSystem has one lock for all its file systems, and callers wait for it forever
  (`RAFT_MUTEX_WAIT_FOREVER`). Main-loop users (static web files, file listings, config writes) would then wait out a
  worker's write, which is the same pause in a new place.
  - The worker should write in pieces (about 4 KB) and release the lock between them.
  - Use a lock per file system, so that an SD write doesn't hold up LittleFS.
- **Acknowledge after the write.** The upload protocols should acknowledge a block once it is written. With two
  buffers the next block is received while the last is written, so throughput needn't fall and may rise. The block
  size then no longer sets the main loop's pause, and bigger blocks could be used again.
- **`filehash`** (and any other whole-file read in the main loop) as a worker job, or read in pieces over several
  loop passes.
- **Stack:** 3 KB looks enough (2.1 KB used in the tests), but measure with the real worker.

### OTA

ESPOTAUpdate has its own worker (4000-byte stack, priority 5, single-slot queue of OTA blocks feeding
`esp_ota_write`, created on the first OTA and never deleted). The Axiom has no OTA partition, so that task never
exists there.

Moving OTA onto the shared worker is worth doing with it. Uploads and OTA don't run at the same time, and the
worker's stack then costs nothing extra on boards that do OTA. That makes the worker's internal RAM cost zero
there, and about 3-4.5 KB only on boards that receive files but have no OTA.

## Costs

| | Shared worker | XIP from PSRAM |
|---|---|---|
| Internal RAM | about 4.5 KB (4 KB stack, ~0.4 KB task control block, ~0.1 KB queue); Axiom018 has 131 KB free, 70 KB minimum | none |
| PSRAM | about 8 KB (two upload chunk buffers); DataLogger already buffers in PSRAM | about 2 MB (the app image is 1.98 MB), of 8 MB |
| Flash (code) | about 3-5 KB: worker 1-1.5 KB, asynchronous upload with back-pressure 1.5-2 KB, DataLogger 0.5-1 KB; 620 KB free in the app partition | none |
| Other | more complex upload flow for BLE/serial (acknowledgements held back) | a slightly longer boot |
