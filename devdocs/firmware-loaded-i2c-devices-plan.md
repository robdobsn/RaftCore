# Firmware-loaded I2C devices (VL53L5CX) as ordinary device types: investigation and plan

Date: 2026-10-25.

> **Status: feasible, and parked (2026-10-25).** The investigation concludes that a firmware-loaded device such as
> the VL53L5CX can be an ordinary device type, driven by its record (§1, §6). The changes needed are identified
> (§4, §5): each is a generic change useful to other devices, and none needs the Axiom's ST driver code. The work
> is **not on any schedule**: it is parked, and listed under "Parked" in RaftCore's `TODO.md`. No code has been
> changed, and decisions F1-F9 (§10) are open.
>
> **A second case, the MLX90640 thermal camera, was added on 2026-10-26** (§12): the Axiom drives it with an
> `MLX90640` SysMod in the same way, and it shows what a generic version needs besides assets.
>
> Meanwhile the Axiom keeps its `VL53LxCX` SysMod, which works (Axiom
> `devdocs/vl53l5cx-vl53l8cx-multizone-tof-plan.md`, §9 Phase 2). Before resuming, see §11.

The VL53L5CX multizone time-of-flight sensor is the first device that needs a firmware image downloaded into it
before it works. The Axiom supports it with its own SysMod, outside RaftCore's and RaftI2C's device handling. This
document looks at how it could become an ordinary device type: a record identifies it, initialises it, polls it and
decodes it, from compiled records or from a device type file. It also considers devices like it (VL53L8CX, BHI260,
and others with a blob or a long scripted init).

**Companion documents:**
- Axiom `devdocs/vl53l5cx-vl53l8cx-multizone-tof-plan.md`. Sensor facts (§3), bus budget (§7) and the bench results
  of the current SysMod (§9 Phase 2). They are not repeated here.
- `device-type-record-management-plan.md`. Records, sources, type files and the raft CLI.
- `computed-init-values.md`. Init code.
- `runtime-decoding.md`. Decoding from records at runtime.
- `variable-length-poll-reads.md`.

---

## 1. Summary

- **The idea works, with one change: name an asset, not a path.**
  - A record lists the binary files it needs as named, versioned, hashed **assets**.
  - The record's source ships them beside the record file.
  - A build either compiles an asset into the firmware or puts it in the file system image.
  - A type file can bring its assets along, uploaded with it.
  - The device finds an asset by name: compiled first, then on the file system.
  - Paths are left to the build and the loader, because one asset can serve several records and a device's file
    layout is a board choice.
- **The download is then one more init step:** "write this slice of this asset to this register, in chunks". The
  identification job sends one chunk per scheduler pass, interleaved with other devices' polls. The Axiom's SysMod
  already does the same with `busReqSync` (400 kHz, 512-byte chunks, about 2.6 s, no IMU samples lost).
- **The asset is necessary but not sufficient.** The sensor's init also has polled handshakes, an init bus speed,
  per-unit calibration arrays, reads over 240 bytes and a frame format to decode. Six generic additions cover them
  (§5). Each is useful to other devices too.
- **Since the Axiom plan (2026-10-03) most of the groundwork has been done** (§3):
  - init is a resumable job, and an init failure backs off and retries;
  - init code computes values from what init read;
  - records are decoded at runtime from their attributes and custom code;
  - type files, sources, the catalogue and devbin's long samples all exist.

  That plan rejected the data-driven route ("Option A") largely because these were missing.
- **Recommendation:**
  - Go data-driven: a VL53L5CX record with an asset and an init script, configured at one resolution and rate for
    each board through `typeSettings`. Reconfiguring re-runs init.
  - Keep a generic C++ **init handler** hook in RaftI2C (the Axiom plan's "Option B") as an escape hatch for a
    device that a script can't express. Build it only when such a device turns up.
  - Remove the Axiom SysMod once the record matches it on the bench.

---

## 2. The VL53L5CX on the Axiom today

| Part | What it does |
|-|-|
| Record `VL53L5CX` (Axiom `devtypes/`) | Detection only: `0x7FFF00=p1&0x0000=0xF002&0x7FFF02=p1` at `0x29`. `pollInfo.ext: 1`, so the record isn't polled; its data comes from code. `resp.b` 196 (grid, seq, tempC, nValid, `dist <h[64]`, `status B[64]`) |
| `VL53LxCXSysMod` (Axiom `components/VL53LxCX`, 842 lines) | Checks every 1 s whether the bus has identified a VL53L5CX. Runs ST's driver (ULD 2.0.0, unmodified) in its own FreeRTOS task. `init`, `set_resolution`/`frequency`/`ranging_mode`/`integration_time`/`sharpener`/`target_order`, `start`/`stop`, `check_data_ready` and `get_ranging_data` decode each frame in C. Pushes a 196-byte sample through `handlePollResult`. Retries a failed init with backoff and forgets a sensor that stops answering. REST `vl53/...` |
| `VL53LxCXBusIF` (platform layer) | Each ULD register access becomes chunked `RaftBus::busReqSync()` calls on the sensor's slot, with their own bus speed |
| ULD (`uld/`, BSD-3-Clause © ST) | `vl53l5cx_buffers.h` holds `VL53L5CX_FIRMWARE` (86,016 bytes), `VL53L5CX_DEFAULT_CONFIGURATION` (972), `VL53L5CX_DEFAULT_XTALK` (776) and `VL53L5CX_GET_NVM_CMD` (40). These must match the ULD version, because the register and block indexes change between releases |

**What makes it special.** Each point is a gap in the generic path:

1. An 86 KB firmware download at every power-up, at about 2.6 s with 400 kHz and 512-byte chunks.
2. About 13 handshakes, each "poll a register until a bit is set", some with 2 s or 5 s timeouts.
3. Per-unit NVM data (492 bytes read) transformed into 488 bytes of offset data and written back. At 8x8 that's a
   shift plus a fixed footer; at 4x4 it's 2x2 averaging.
4. Config and xtalk buffers of 972 and 776 bytes.
5. Ranging configuration through ST's "DCI" mailbox (read-modify-write plus a handshake). Resolution changes the
   frame size.
6. Frames of 128 bytes (4x4) to 320 bytes (8x8, distance + status + targets) or more. That's above the 240-byte read
   limit. Every 32-bit word is byte-swapped, and the frame is a series of headed blocks.
7. A data-ready check, a 4-byte read compared with the last stream count. Without it, polls faster than the frame
   rate read repeated frames.
8. A crowded address (`0x29`, also VL6180/VL53L0X/L1X/L4CD/LTR-329). The detection's page write must only reach
   devices the other probes have ruled out.

---

## 3. What has changed since the Axiom plan

| Axiom plan (2026-10-03) finding | Now (RaftCore 1.62.0, RaftI2C current) |
|-|-|
| Init runs as one blocking burst on the bus task | Identification and init are a **resumable job** (`DeviceIdentMgr::IdentJob`). A step followed by a pause of 10 ms or more (`LONG_PAUSE_MS`) ends the pass, and the job resumes later. Each step is still one synchronous transaction |
| Init failures are ignored; a failed device is polled anyway | A bus error during init is **`InitFailed`**: the device isn't identified, and identification is retried with backoff (`RETRY_BUS_ERROR_MIN_MS` to 10 s). NACKed steps repeat init once, then the device is used with a warning |
| Values computed from device data can't be written | **`initCode`** (decode language, compiled or interpreted) computes `init.<name>` values from bytes init read, written into `{name}` slots or used as `{?name}` guards. Each slot is **one byte** |
| Binary publishing truncates samples over 255 bytes | Fixed: devbin's long-sample escape (`DEVBIN_LONG_SAMPLE_ESCAPE`, 16-bit length) |
| `LTR-329` matches any device at `0x29` | Fixed 2026-10-13 |
| Records decode only through compiled code | **Runtime decoders** from a record's attributes, and custom code (`resp.c`) through the interpreter (`RAFT_RUNTIME_DECODE_CODE`) |
| Records only in a project's catalogue | One file per record, `DeviceTypeSources.json` with git sources and a lock, runtime **type files** with an on-device check, the published catalogue, the Device Types app |
| Data supplied by code | `pollInfo.ext` with `registerExtDataSource()` (used by the SysMod today) |
| Reads silently clamped to 240 bytes | **Unchanged** (`RaftI2CCentral::MAX_I2C_READ_BYTES`). A poll can read a frame as two steps, `0x0000=r240&=r80`, as the SysMod's 8x8 frame (240 + 80) shows |
| Bus speed per device applies only to polls | **Unchanged**: `typeSettings`/`devconfig` `busHz` is applied by `DevicePollingMgr` only. Detection and init run at the bus default |
| Scheduling of other devices during a long init | The job yields only at long pauses. A run of short steps (the download) still runs back to back in one pass |

---

## 4. Assets: binary files a record needs

### 4.1 In the record

```jsonc
"VL53L5CX": {
    "assets": {
        "fw":  { "file": "vl53l5cx_uld_2_0_0_fw.bin",     "bytes": 86016, "sha256": "…" },
        "cfg": { "file": "vl53l5cx_uld_2_0_0_cfg.bin",    "bytes": 972,   "sha256": "…" },
        "xt":  { "file": "vl53l5cx_uld_2_0_0_xtalk.bin",  "bytes": 776,   "sha256": "…" }
    },
    ...
}
```

- **The key** (`fw`) is how the record's steps refer to the asset.
- **`file`** is the asset's name: unique within the source, and the name it has on the device. Including the
  driver version in the name means two versions can coexist.
- **`bytes` and `sha256`** are integrity checks. The build checks them, the type file loader checks them, the device
  checks them when it opens the asset, and the lock records the hash.
- **Why not a path:**
  - a device's file system layout is a board choice;
  - the same file can serve several records (VL53L5CX and its variants);
  - the device needs to know the file is the right one, not just where it is.

### 4.2 In a source

Assets sit in an `assets/` folder beside `records/`, in RaftCore or in a git or path source, with a licence file:
`assets/vl53l5cx_uld_2_0_0_fw.bin` and `assets/LICENSE-ST-ULD.md`. The assembler (`AssembleDeviceTypes.py`):
- copies each selected record's assets with it;
- checks `bytes` and `sha256`;
- records them in the manifest and the lock (git sources' assets are pinned like their records).

A record whose asset is missing or doesn't match is an error, as a missing record is.

**Getting the bytes.** ST ships the firmware as a C array in `vl53l5cx_buffers.h`. A small script (`scripts/ExtractCArray.py`) turns the arrays into `.bin` files once per ULD version.

**Licence.** ULD is BSD-3-Clause. Redistribution in binary form is allowed if the copyright notice and conditions
are reproduced "in the documentation and/or other materials provided with the distribution". So:
- the source keeps the licence file next to the assets, and the record's `devInfoJson` names it;
- the build puts it in the file system image with the asset;
- product documentation must include it (decision F7).

The TODO's alternative, fetching from ST's website at build time, isn't practical: STSW-IMG023 is behind a login and
click-through.

### 4.3 On the device

| Where the record comes from | Asset | Notes |
|-|-|-|
| Compiled in (`builtin`) | **Compiled into the firmware** (default): the generator emits the bytes as a `const` array, flash-mapped, read in place | 86 KB of app partition. The Axiom app is 2.03 MB of a 2.5 MB partition. Nothing extra has to be flashed |
| Compiled in, `"assets": "fsImage"` in the `builtin` section | Put in the file system image at `/local/devassets/<file>` | Saves app space. A full flash is needed, and the device reports a missing asset (below) |
| Type file | Uploaded with it to `/local/devassets/<file>`, by `raft devtypes push` and the Device Types app; or put in the FS image with `typeFile.fsImage` | A type file entry refers to the asset by `file`, `bytes` and `sha256` |

- **Finding an asset:** compiled assets by name first, then `/local/devassets/<file>`. A DevMan setting
  `assetsPath` changes the folder.
- **Checks:** when an asset is first needed, its size and hash are checked. That's 86 KB through SHA-256, a few tens
  of ms, once per boot. The result is cached per asset.
- **What's reported:**
  - `devman/typefile/check` and the type file loader report a record whose asset is missing or doesn't match
    (`assetMissing`, `assetMismatch`);
  - `devman/typeinfo` reports it per type;
  - a device of such a type is `InitFailed`, with the reason, and retried with backoff (an asset uploaded later is
    then found).
- **Reading:**
  - A compiled asset is read in place.
  - A file asset is read a chunk at a time into a buffer the init job owns: 512 bytes, or the chunk size. Reading
    from LittleFS on the bus task while DataLogger writes could stall the bus for a flash write (see
    `flash-write-blocking-plan.md`). So the chunk is read ahead on the main task, and the init step waits for it.
    Measure first; read it inline if that's fast enough.
- **Memory:** no 86 KB copy. One chunk buffer per init in progress.

### 4.4 In raftjs, the app and the CLI

- **Type files:**
  - `buildTypeFile` puts a record's asset references in the file;
  - `DeviceTypeFileManager.push` uploads the assets, skipping any whose file on the device already has the hash;
    then the device's check verifies them;
  - `pull` reports missing assets.
- **Sources:**
  - `readGitHubSource` and `recordsFromFiles` read `assets/` too;
  - the lock and `_sources` cover them;
  - the Pages catalogue publishes a release's assets with their licence (decision F7).
- **CLI:** `raft devtypes push` the same; `typefile` writes the assets beside the file.
- **Flasher:** nothing extra. Assets in the FS image go with it.

---

## 5. Other changes the VL53L5CX needs

These are in order of need. With items 1-5 and assets, the sensor runs from a record at one configuration; items 6-8
make it better.

1. **Write an asset slice in chunks.**
   - **Form:** a new init step,
     `0x0000{@fw[0x0000:0x8000]/512}`, means "write bytes 0x0000-0x7FFF of asset `fw`, starting at register 0x0000,
     in 512-byte writes whose register address advances with each chunk". The register width comes from the
     step's write bytes before the `{`.
   - **Running it:** the identification job runs one chunk per pass, as a step. It keeps its place in the asset
     between passes.
   - **Also:** a plain `{@cfg}` writes a whole small asset as one write. The 972- and 776-byte buffers fit under the
     1529-byte write limit.
   - **Checks:** the grammar (C++, Python, TypeScript and the shared vectors) checks the asset key, range and chunk
     size.
2. **Wait until a register reads a value.**
   - **Form:** `0x2C00=w2{1:0xFF=0x03}t2000p10` (illustrative), meaning:
     - read 2 bytes at 0x2C00, until byte 1 masked with 0xFF equals 0x03;
     - try every 10 ms;
     - fail init after 2000 ms.
   - **Running it:** each try is one pass of the job. Between tries the job is paused, so other devices are served.
   - The ST sensors need about 13 of these. Many other devices have "wait for ready" bits that records currently
     cover with fixed pauses.
3. **Init gives way after each step.**
   - **Now:** the job only yields at long pauses.
   - **Change:** it gives way after any step once the pass has used its time budget (about 5 ms), or after every
     chunk of an asset write.
   - **Measure:** the LSM6DS FIFO test from the Axiom plan (§9 Phase 2), against the SysMod's numbers.
4. **Init bus speed.** `typeSettings` and `devconfig` gain `initBusHz`, applied to the init steps of the type and
   restored after each step, as `busHz` is for polls. The SysMod's measurements say 400 kHz with 512-byte chunks. At
   1 MHz, writes of 768 bytes or more fail on the S3, and 512-byte writes sometimes fail under WiFi load.
5. **Init values that are arrays.**
   - **What:** `{name[N]}` slots, N bytes from init code assigning `init.name[i]`. The decode language already writes
     array attributes (`out.x[i]`).
   - **Why:** the offset step reads 492 bytes of NVM and writes 488 transformed bytes.
   - **At 8x8** the transform is a shift plus a fixed footer.
   - **At 4x4** it's 2x2 averaging. Init code can do that with loops, if the interpreter's instruction limit allows
     about 2000 operations (decision F3).
6. **Reads over 240 bytes.**
   - **Now:** the clamp is silent. A step that asks for more gets zeros past byte 240.
   - **Change:** either split such a read into consecutive read-only transactions (the ST sensors carry the address
     across them, as the SysMod relies on), or refuse it in the grammar and the type file check.
   - **Prefer:** splitting, so a record can say `0x0000=r320`. It's needed for 8x8.
7. **Polling only new frames.**
   - **Now:** a poll at the frame rate reads each frame about once. The sensor's rate (15.19 Hz for "15") drifts
     against the poll interval, so frames are sometimes repeated or skipped.
   - **Options:**
     - (a) A conditional poll: read 4 bytes, then the frame only if byte 1 is 0x05 and byte 0 differs from last
       time. That's an extension of the `r{...}` expressions (`variable-length-poll-reads.md`) with a comparison
       and a remembered byte.
     - (b) Poll faster than the frame rate and drop repeats in the decoder by the stream count, which needs decoder
       state.
   - **Prefer** (a): it saves bus time as well.
8. **Decoding ST frames.**
   - **How:** custom decode code (`resp.c`) that un-swaps the 32-bit words and walks the block headers to the
     distance, status and target blocks. The decode language has the loops and indexing (`buf[k^3]`).
   - **Output:** the attributes the Axiom record has now (grid, seq, tempC, nValid, `dist[64]`, `status[64]`), so the
     dashboard's zone grid visualiser and clients don't change.
   - **Runs:**
     - compiled for builtin records;
     - by the interpreter for type file records;
     - in raftjs for clients.
   - The publishing choice in the Axiom plan (decode on the device, publish compact samples) stays.

### 5.1 Configuration

- **Settings for the board:** resolution, frequency, integration time, sharpener and target order are written during
  init.
- **Where the values come from:** init code gets `typeSettings` values as inputs, e.g. `cfg.res` and `cfg.hz`, with
  the record's defaults (decision F4). Guards (`{?is8x8}`) and slots choose the steps.
- **Changing one:** a `devconfig` or `typeSettings` change re-runs init (`devman/reinit`). It takes about 2.6 s. That
  replaces the SysMod's live stop-reconfigure-start; the Axiom plan expected live reconfiguration to need
  stop → write → start anyway.
- **Records per resolution:** resolution changes the frame size (`pollInfo` read length, `resp.b`). Either:
  - the record's poll and decode handle the larger layout, with the poll length from `cfg.res`
    (`r{...}` with a setting); or
  - there are two records, `VL53L5CX` (8x8) and `VL53L5CX4x4`, with the board choosing one with `typePolicy`.

  Start with one resolution (8x8 at 15 Hz, as the SysMod defaults), and decide when 4x4 is needed (decision F5).

### 5.2 Other points

- **Stop on disable.**
  - **Now:** `devenable poll=0` stops polling, but the sensor keeps ranging at about 95 mA.
  - **Change:** an optional `stopValues` init-style string, run when polling stops and before power-down. It's
    useful for other always-on sensors.
  - Low priority.
- **Probes at `0x29`.**
  - **Order:** the VL53L5CX detection writes page register `0x7FFF`, so it must come after the
    VL6180/VL53L0X/L1X/L4CD probes. Give it an `identPriority` below theirs (order is `identPriority`, then
    detection strength, then key).
  - **False match:** the Axiom plan's question 10, the VL6180 probe matching a ranging L5CX about 1 in 255 times,
    is still open. Tighten the VL6180 detection (more ID registers), and record `hwProbeEffects`.
- **Size of the init string.**
  - **Estimate:** ULD's source has about 120 register accesses across `init` and the setters, and `init` has 13
    waits. As steps that's a few kilobytes of text (to be measured when the script is written).
  - **Parsing:** command strings are parsed once and cached per record now, so the cost is fine. Measure the
    parse and the cache's RAM.
  - **Larger blocks:** the config buffers go in assets, not hex.
- **Writing the init script.**
  - **Source:** take it from ULD 2.0.0 `vl53l5cx_init()` and its setters, step by step.
  - **Check:** compare a logic-analyser trace of the script against the SysMod's (decision F6).
  - **Upgrades:** a ULD upgrade means regenerating the script and the assets together. A script comment and the
    asset names carry the ULD version.
- **VL53L8CX:**
  - **Record:** a second record, revision `0x0C`, with its own assets.
  - **Init differences:** its steps differ in places, and it adds a firmware checksum check — a read and compare,
    which is a detection-style check inside init (`0x2FFC=0x0C0B6C9E`, a "check" step that fails init).
  - **Hardware:** it needs 1.8 V I/O.

---

## 6. The alternative: a C++ init handler

Its form is in the Axiom plan (Option B):
- a class named by the record (`"initHandler": "VL53LxCX"`), registered by name as `DeviceFactory` classes are;
- stepped by the identification job on the bus task, with polling held off until it's ready.

| | Data-driven (assets + §5) | C++ init handler |
|-|-|-|
| Works from a type file, a git source, the app | Yes | Only if the firmware already has the handler |
| Device-specific code in RaftCore/RaftI2C | None | The handler, in a Raft library (it would move out of Axiom), plus the ULD port or ULD itself |
| Effort for the VL53L5CX | Six generic features (§5), the script, the decode code | The hook, plus the driver as a state machine (or ULD in a task, as now) |
| Reuse | Each feature helps other devices (waits, init speed, larger reads, conditional polls) | The hook helps other complex devices |
| Live reconfiguration | Re-run init (about 2.6 s) | Natural |
| Risk | The script must match ULD exactly. 4x4 offset averaging needs the interpreter's limits checked | ULD's blocking calls need its own task (as now), or a port |

**Recommendation:** data-driven first. It fits how records now travel (sources, type files, the catalogue, the app),
and the features are generic. Add the init handler only when a device appears whose init can't be scripted, for
example one that needs a host-side algorithm running continuously. Until the record works on the bench, the Axiom
SysMod stays as it is.

---

## 7. The record, illustrated

```jsonc
"VL53L5CX": {
    "addresses": "0x29",
    "identPriority": -10,
    "detectionValues": "0x7FFF00=p1&0x0000=0xF002&0x7FFF02=p1",
    "assets": { "fw": {…}, "cfg": {…}, "xt": {…}, "nvmcmd": {…} },
    "initValues": "0x7FFF00&0x0009=p1&…(reboot)…&0x0006=w1{0:0x01=0x01}t2000p10&…&0x7FFF09&0x0000{@fw[0:0x8000]/512}&0x7FFF0A&0x0000{@fw[0x8000:0x10000]/512}&0x7FFF0B&0x0000{@fw[0x10000:0x15000]/512}&…&0x2FD8{@nvmcmd}&0x2C00=w4{2:0xFF=0x03}t1000p10&0x2C04=r492&0x2E18{off[488]}&…&0x2C34{@cfg}&…&(start ranging)",
    "initCode": { "c": "… the offset transform for cfg.res …" },
    "pollInfo": { "c": "0x0000=r{…ready test…}&0x0000=r320", "i": 66, "s": 4 },
    "devInfoJson": { "name": "Multizone Distance Sensor", "resp": { "b": 196, "a": [ … as now … ], "c": { "n": "vl53l5cx_frame", "c": "… un-swap, walk blocks …" } } }
}
```

Syntax is illustrative; the forms are decisions F1-F2.

---

## 8. Phases

| Phase | Work | Repos | Verify |
|-|-|-|-|
| **0** | Decisions F1-F9. Extract the ULD 2.0.0 arrays to `.bin` with their licence. Take a logic-analyser trace of the SysMod's init and an 8x8 frame for reference | RaftCore, Axiom | Byte-identical arrays (hash) |
| **1** | Assets: record field, assembler (copy, check, manifest, lock), generator (compiled assets), loader (type file references), FS image placement, device lookup and checks, `typefile/check` and `typeinfo` reporting | RaftCore | Host tests (assembler, loader, lookup); a type file with an asset uploaded and checked on a FeatherS3 |
| **2** | Init steps: asset slice writes, waits; the job gives way after each chunk and when its budget is used; init bus speed; array init values; reads over 240 split | RaftCore (grammar, records), RaftI2C (job, bus speed, reads) | Shared grammar vectors; host tests of the job (a simulated device answering waits); bench: a 64 KB write to a scratch device; LSM6DS FIFO test during a long init |
| **3** | Conditional polls (`r{...}` comparison with a remembered byte) | RaftCore, RaftI2C, raftjs (if clients need the length) | Host tests; vectors |
| **4** | The VL53L5CX record (8x8, 15 Hz): script, assets, decode code, `identPriority`; in RaftCore's `devtypes/records` (or a separate ST source, F8) | RaftCore | On the Axiom with the SysMod disabled: init time, frames against the SysMod's, the LSM6DS FIFO test, hot-plug, power cycle; the dashboard's zone grid |
| **5** | Remove the SysMod from Axiom: `DeviceTypeSources.json` selects the record, `typeSettings` sets `initBusHz` | Axiom | Axiom018 build and bench as phase 4 |
| **6** | raftjs and the app: assets in type files, push, pull, the catalogue, the lock | raftjs, RaftCore (Pages) | Vectors; push a type file with an asset from the app |
| **7** | Optional: 4x4 (array transform in init code), `stopValues`, VL53L8CX | RaftCore | Bench |

Phases 1 and 2 are independent. Phase 4 needs both, and phase 3 to avoid repeated frames.

---

## 9. Risks

- **The script must match ULD exactly.** A small difference (a missed DCI write, a wrong offset transform) gives
  plausible but wrong distances. The phase 0 trace and a frame-by-frame comparison with the SysMod are the check.
- **File assets need the file system.** A device flashed without its FS image (`--no-fs`, an OTA update) has no
  file asset. Compiled assets avoid that, and the device reports `assetMissing` clearly. Default builtin records to
  compiled assets (F2).
- **Bus timing.** A long init interleaved with polls must not starve fast devices (LSM6DS FIFO), and the chunk size
  and budget trade init time against that. The SysMod's results show 512-byte chunks at 400 kHz are fine.
- **Interpreter limits.** The 4x4 offset transform and the frame decode in a type file record run in the
  interpreter, whose instruction limit stops runaway loops. Check that both fit, or raise the limit for init code.
- **Record size.** A few kilobytes of init string per record. Command strings are parsed once and cached now, so
  the cost is RAM for the cache. Measure it.

---

## 10. Decisions

| # | Question | Proposal |
|-|-|-|
| F1 | Asset reference syntax in steps (`{@key[from:to]/chunk}`), and the record field (`assets`) | As §4.1 and §5 item 1 |
| F2 | Default for a builtin record's assets: compiled in, or in the FS image | **Compiled in**; the FS image by choice (`"assets": "fsImage"` in `builtin`) |
| F3 | Array init values (`{name[N]}`) and the interpreter's limit for init code | Add both; set the limit after measuring the 4x4 transform |
| F4 | Settings as init code inputs (`cfg.<name>` from `typeSettings`, with record defaults) | Yes. It makes resolution, rate and the like board choices without separate records |
| F5 | One resolution first (8x8 at 15 Hz), or both | 8x8 first; 4x4 when wanted |
| F6 | Script source: hand-ported from ULD 2.0.0, checked against a trace | Yes |
| F7 | Publishing ST's firmware: in RaftCore's records and the Pages catalogue (with the BSD-3 notice), or only in a separate source | RaftCore, with the licence file next to the assets and in the catalogue; product documentation includes the notice |
| F8 | Where the record lives: RaftCore `devtypes/records`, or a separate ST source | RaftCore, as other ST records |
| F9 | Keep the C++ init handler as a later escape hatch | Yes, not built until needed |

---

## 11. Resuming this work

The plan rests on the code as it was on 2026-10-25 (RaftCore 1.62.0 and the following merges, RaftI2C at the same
time). Before starting:

1. **Re-check §3 against the code.** In particular:
   - whether init still yields only at long pauses (`DeviceIdentMgr::runInitSteps`, `LONG_PAUSE_MS`);
   - whether reads are still clamped at 240 bytes (`RaftI2CCentral::MAX_I2C_READ_BYTES`);
   - whether `busHz` still applies only to polls;
   - whether init code values are still single bytes.

   Any of these done in the meantime shrinks §5.
2. **Settle F1-F9 (§10)**, F2, F5 and F7 first. They decide what phase 1 builds and whether ST's firmware can be
   published with RaftCore.
3. **Check the Axiom SysMod still works** and take the phase 0 reference: a logic-analyser trace of its init and of an
   8x8 frame. Its bench numbers (§9 Phase 2 of the Axiom plan) are the targets.
4. **Hardware questions still open** (Axiom plan §10):
   - **Q5:** do the VL53L0X probes upset a ranging L5CX?
   - **Q6:** is a sensor left mid-download still detectable?
   - **Q7:** what does status read in zones with no target when nb_target is off?
   - **Q10:** the VL6180 false match.

   Q6 and Q10 decide the detection string and its `identPriority`.
5. **Decide whether the Axiom still wants the sensor**, and at which resolutions (F5). Also whether a VL53L8CX is
   planned, since it needs 1.8 V I/O on the module.

Phases 1 (assets) and 2 (init steps) are useful without the VL53L5CX: assets for any device needing a calibration or
firmware file, waits and the init bus speed for many existing records that use fixed pauses today. Either could be
taken up on its own.

---

## 12. A second case: the MLX90640 (2026-10-26)

The Melexis MLX90640 32x24 thermal camera (0x33) has no firmware to load, but it can't be an ordinary record
either, so the Axiom drives it with a SysMod like the VL53L5CX's: Axiom `components/MLX90640`, the Melexis library
(Apache-2.0) in its own task, reading through chunked `busReqSync()` calls. The record (Axiom `devtypes/MLX90640.json`)
only identifies it (the EEPROM's I2C address word, 0x240F, ends 0x33) and takes the frames (`pollInfo.ext`).

**On Axiom018:** calibration read in 175 ms; 4 subpages a second (2 full frames); a subpage read in 44-58 ms
(832 words, eight transactions at 400 kHz) and its temperatures worked out in 7-20 ms; frames of 768 temperatures
(0.01 C) published through the device data pipeline (1540-byte samples, devbin's long-sample form), logged by
DataLogger and drawn by the dashboard's thermal visualizer.

**What a record would need** - mostly not covered by §4-§5:

| Need | What the MLX90640 does | Generic addition |
|-|-|-|
| Per-unit calibration kept for decoding | Every pixel's temperature uses the unit's EEPROM (832 words: gains, offsets, alphas, Kta, Kv, bad pixels), read once | Init steps that read blocks into a per-device store which the decode code is given beside the poll data (`computed-init-values.md` only feeds init writes) |
| A heavy decode | `MLX90640_CalculateTo`: about 50 floating-point operations per pixel, with `pow` and `sqrt`, for 768 pixels | Maths functions in the decode language, or a compiled decode handler named by the record (the decode-side twin of §6's init handler) |
| Decode state across samples | In the chess pattern each subpage updates half the pixels, alternately, so a frame is the latest two subpages | Per-device decode state kept between samples |
| Long reads, conditional polls | A frame is 832 words (1664 bytes) read only when the status register's data-ready bit is set | §5's reads over 240 bytes and conditional polls - the same as the VL53L5CX |

**Recommendation:** as for the VL53L5CX, keep the SysMod until a record can do it. Of the routes, a compiled decode
handler (with the calibration store) is the smallest step that covers the MLX90640: the calculation stays Melexis's
code, and the device data pipeline, type files and clients work as for any record.

## 13. References

- Axiom `devdocs/vl53l5cx-vl53l8cx-multizone-tof-plan.md`: sensor facts, bus budget, bench results, hardware
  questions.
- Axiom `components/VL53LxCX/`: the SysMod, its platform layer and ULD 2.0.0 (`uld/LICENSE.md`).
- RaftCore devdocs:
  - `device-type-record-management-plan.md`: records, sources, type files, the CLI;
  - `computed-init-values.md`: init code;
  - `runtime-decoding.md`: runtime decoding;
  - `variable-length-poll-reads.md`: `r{...}` read lengths;
  - `flash-write-blocking-plan.md`: file system writes and the main loop.
- RaftI2C `devdocs/i2c-bus-speed-change-investigation.md`: the 240-byte read limit, 400 kHz findings.
- RaftCore wiki: *Device Type Record Format* (init values, init code, runtime decoding), *DeviceManager Settings*
  (`typeSettings`, device type file), *I2C Device Identification and Polling*.
- ST: UM2884 (VL53L5CX ULD), DS13754, STSW-IMG023 (ULD).

