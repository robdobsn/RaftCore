# Flash writes blocking the main loop - tests and plan

Status: **on the back-burner** (2026-10-21). Nothing here is implemented yet.

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

### What the results decide

| Test 2 result | Then |
|---|---|
| Spikes gone, average write time in `loop()` acceptable | Enable XIP from PSRAM; the shared worker isn't needed |
| Spikes gone, but the software part still matters (e.g. uploads at 13+ ms per chunk) | Enable XIP from PSRAM and add the shared worker |
| Spikes remain | The worker only helps with the software part; look at the erase behaviour (flash auto-suspend support for this chip, pre-erasing/pre-allocating log files, smaller LittleFS block or cache sizes) |

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

### OTA

ESPOTAUpdate has its own worker (4000-byte stack, priority 5, single-slot queue of OTA blocks feeding
`esp_ota_write`, created on the first OTA and never deleted). The Axiom has no OTA partition, so that task never
exists there. OTA could later be moved onto the shared worker to save its stack on boards that do OTA, but it's
left alone for now.

## Costs

| | Shared worker | XIP from PSRAM |
|---|---|---|
| Internal RAM | about 4.5 KB (4 KB stack, ~0.4 KB task control block, ~0.1 KB queue); Axiom018 has 131 KB free, 70 KB minimum | none |
| PSRAM | about 8 KB (two upload chunk buffers); DataLogger already buffers in PSRAM | about 2 MB (the app image is 1.98 MB), of 8 MB |
| Flash (code) | about 3-5 KB: worker 1-1.5 KB, asynchronous upload with back-pressure 1.5-2 KB, DataLogger 0.5-1 KB; 620 KB free in the app partition | none |
| Other | more complex upload flow for BLE/serial (acknowledgements held back) | a slightly longer boot |
