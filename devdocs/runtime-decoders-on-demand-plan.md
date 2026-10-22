# Runtime decoders on demand - plan

Status: **implemented** (2026-10-22, branch `feature/device-type-records`). Background and results:
[runtime-decoding.md](runtime-decoding.md) section 8. One change from the plan below: decoders are made on the main
task (`DeviceManager::busElemStatusCB`), not from `getPollInfo` on the bus task - see "Building on the identifying
task".

## The problem

A record added at runtime with no compiled decoder (from a device type file, or registered by code) gets a runtime
decoder **when it is added** (`DeviceTypeRecords::attachRuntimeDecoderLocked`, from `addRecord` and
`replaceBaseRecord`). So every record in the table pays for a decoder, whether or not a device of that type is ever
connected. Measured on 2026-10-21 (ESP32-S3, `unit_tests`, records of typical size with 4 attributes):

| Board | Records added | Packed records | Decoders | Result |
|---|---|---|---|---|
| No PSRAM (`unittest`) | 32 (the default limit) | 17.7 KB | about 30.6 KB (about 960 B each) | 48.4 KB of **internal** RAM; `DeviceTypeTable_test.cpp:79` fails (its bound predates runtime decoders) |
| Octal PSRAM (`unittest_psram`) | 256 (the default limit) | 142 KB | 64 decoders, about 60 KB of PSRAM | **192 records have no decoder**: all 64 decoder slots (`RAFT_RUNTIME_DECODE_MAX_DECODERS`) were taken by the first 64 records. Only a warning per record shows this; the test passes because it doesn't check decoders |

Neither the 20 KB `typeTable.maxExtendedBytes` limit nor the slot count reflects how many decoders are needed: one
for each **type that is present**, which on a real board is a handful.

## The idea

A decoder is derived data, like the parsed command strings that `getCompiledRecord` already caches the first time
they are used. A decoder is needed only once a device of that type has been identified and is about to be polled.
So build it then:

- **At load:** a type file record is checked to see whether it *can* be decoded (as now, so `devman/typefile` still
  reports `noDecoder`), but the decoder is thrown away.
- **At identification:** the first time a device of a type is identified, its decoder is built and attached to the
  record, before the device is reported as identified. Later devices of the same type use it.
- **Never freed** while the table exists. Users keep the decode function and field pointers (Axiom's DataLogger
  caches them per device), and the set of types seen on a board is small and stable. A change to the records is
  applied by a restart anyway.

### With PSRAM and without

The same mechanism serves both. What differs is the default policy (`typeTable.runtimeDecoders`, below):

| Board | Default | Cost |
|---|---|---|
| PSRAM | `onIdentify` | About 1 KB of PSRAM for each type present; slots used only by types present, so 64 slots is ample |
| No PSRAM | `onIdentify` (recommended) or `off` - decision **D1** | `onIdentify`: about 1 KB of internal RAM for each type present, within a decoder budget. `off`: nothing |

`off` is "only build decoders when there is PSRAM": a board without PSRAM never builds runtime decoders. Records
from a type file are still identified, initialised and polled, and their raw data is still published (`devbin`,
`devjson` hex), so clients that decode themselves (raftjs, the web UIs) are unaffected. What's lost for those
records is **on-device** decoded data: DataLogger CSV, RaftROS auto-publish, named values (`devman/value`) and
anything else that uses `pollResultDecodeFn`. Compiled records are unaffected either way: they have compiled
decoders.

With `onIdentify` on a board without PSRAM, a typical board with 2-4 type file devices present pays 2-4 KB instead
of 30 KB, and still gets on-device decoding. That's why I recommend it as the default, with `off` available for a
board that is short of internal RAM.

## How it works in practice

**Startup.** The type file is loaded as now. For each record with response data and no compiled decoder, the loader
checks that a decoder can be made (`DeviceTypeRuntimeDecoder::create(..., takeSlot = false)`, then deletes it) and
marks the record `Pending`. A record that can't be decoded is marked `Failed` and reported as `noDecoder`, as it is
today. No decoder memory remains, and no slots are taken.

**A device is plugged in.** The bus identifies it (RaftI2C: `DeviceIdentMgr::identifyDevice`, on the I2C worker
task). On a match it calls `completeDeviceStatus`, which calls `deviceTypeRecords.getPollInfo(address, typeIndex,
...)`. `getPollInfo` calls `ensureDecoder(typeIndex)`:

1. The record already has a decode function (compiled, inherited, or built earlier): return at once. This is one
   atomic load, so identification of compiled types costs nothing extra.
2. Otherwise, under the table mutex, check again. If the record is `Pending` and policy and budget allow, build the
   decoder, take a slot, attach it (below), mark the record `Built` and add its bytes to the stats.
3. If it can't be built (policy `off`, budget used up, no slot free, out of memory), mark the record `Failed`, log
   it once, and return. The device is still identified and polled, just without on-device decoding.

The device is then published as identified (`BusStatusMgr`, then `DeviceManager::busElemStatusCB` on the main
task), so DataLogger, RaftROS and data subscribers see a record that already has its decoder.

**A second device of the same type** goes through step 1: nothing is built.

**Static devices** (registered by code with their own record) are present from setup, so
`DeviceManager::setupStaticDevice` calls `ensureDecoder` straight after `addRecord`.

**BLE.** `BLEBusDeviceManager` copies the `BLEBTHome` record in its constructor. `BLEBTHome` is compiled, so it
has a decoder. Only a type file override of `BLEBTHome` with a different response layout would leave the copy
without one. RaftSysMods then calls `ensureDecoder` before copying (one line).

## Design

### `ensureDecoder`

```cpp
/// @brief Make sure a record that can be decoded at runtime has its decoder (built on first use)
/// @return true if the record has a decode function (compiled or runtime)
bool DeviceTypeRecords::ensureDecoder(DeviceTypeIndexType deviceTypeIndex) const;
```

It's `const` like `getCompiledRecord`: a decoder is lazily cached derived data. The decoder list and per-record
states are `mutable`, as `_compiledRecords` is. It is called from:

- `getPollInfo(addr, deviceTypeIndex, ...)`: every bus's identification path. No RaftI2C change is needed. The
  `getPollInfo(addr, pRec, ...)` overload used by the loaders on a temporary record doesn't call it;
- `DeviceManager::setupStaticDevice`;
- `getDecoderKind` doesn't build anything: it reports the state (below).

Each extended record and base record override gets one state byte (`NotNeeded`, `Pending`, `Built`, `Failed`). It
sits in a vector beside `_extRecords` and `_baseOverridesStore`, not in the packed record, so
`DeviceTypeRecordPacked` doesn't change.

### Attaching to a published record

A record is read without a lock: `getRecord` returns its pointer, and readers read `pollResultDecodeFn`,
`pollFieldDescs`, `pollFieldCount` and `pollStructSize` from it, or copy it whole (`getDeviceInfo`). Today these
fields are set before the record is published. With this change they are set **after**, so the order matters:

- Writer, holding the table mutex: set `pollFieldDescs`, `pollFieldCount` and `pollStructSize`, then store
  `pollResultDecodeFn` last with release ordering (`__atomic_store_n(..., __ATOMIC_RELEASE)`; the field stays a
  plain function pointer, so `DeviceTypeRecord` and every user keep compiling).
- `getPollDecodeFn` and `ensureDecoder`'s fast path read it with acquire ordering.
- Every other reader acts on a device that has been reported as identified. That report goes through
  `BusStatusMgr`'s mutex and the main task's status callback, which happen after `ensureDecoder` returned. So the
  fields are already visible to them.
- A reader that copies a record of a type with no device present (e.g. `devman/typeinfo` with `full`) may see a
  record with or without its decoder. Either way it's consistent, because the decode function is written last and
  every user checks it before the fields.

### Building on the identifying task

`ensureDecoder` runs on whichever task identifies the device: the I2C worker task for RaftI2C. Two things to
measure before relying on it:

- **Stack.** The I2C worker task has 5 KB. `create` parses `devInfoJson` with RaftJson and builds vectors; it
  doesn't run the code interpreter (custom decode code is out of scope, below). Measure the least free stack with
  RaftI2C's `busstatus` `workerStack` across first identifications of the 10 largest records.
- **Time.** First identification of a type gets slower by the time of one `create`. Expected to be a few ms, but
  measure it. Identification is already paced (`allowPause`), and the bus isn't held during `create`.

If either is a problem, the fallback is to build in `DeviceManager::busElemStatusCB` on the main task instead. Then
subscribers on the I2C task may get a few polls undecoded between identification and the callback. Stored poll
results aren't affected, because they are decoded when they are read.

**Measured (2026-10-22, ESP32-S3):** `create` alone takes about 2 KB of stack for a large record (TrillSquare, 16
attributes), and 7-10 ms. The I2C worker task has about 2 KB free on an Axiom, so the fallback was taken: decoders are
made in `DeviceManager::busElemStatusCB`. There is no window of undecoded polls after all: the callback installs the
device's data dispatcher after its status listeners, so no subscriber gets the device's data before the decoder
exists. Every bus reports identification through that callback, so the BLE change below isn't needed.

### Limits and accounting

- `ExtendedRecordStats` gains `decodersBuilt`, `decodersFailed` and `decoderBytes`. A decoder reports its own size
  (`DeviceTypeRuntimeDecoder::getAllocatedBytes()`: the object plus its vectors' capacities and text).
- `typeTable.maxDecoderBytes`: the decoder budget. Default 0 (no limit beyond the slots) with PSRAM, 8192 without
  (about 8 typical decoders) - decision **D2**. A decoder that would exceed it isn't built (`Failed`, logged).
- `typeTable.runtimeDecoders`: `onIdentify` (default), `atLoad` (today's behaviour: build every decoder when the
  record is added - for diagnosis, or a board that wants decoding of every type ready at boot) or `off`.
- The slot count stays 64. On demand, it limits the number of types present that need a runtime decoder, which is
  far below 64.

### Reporting

- `DecoderKind` gains `RuntimePending` (`"runtimePending"`: can be decoded, no device of the type identified yet)
  and `RuntimeFailed` (`"runtimeFailed"`: it could be, but the decoder wasn't built, with the reason).
  `devman/typeinfo` reports these, and `devman/typefile` outcomes keep `noDecoder` for "can't be decoded".
- The `typeTable` stats where they are reported now gain the three new counts.

### Out of scope

- **Custom decode code** (`resp.c`, built with `RAFT_RUNTIME_DECODE_CODE`): compiling it takes 60-110 ms per
  record, and compiling it on the I2C task overflowed the 5 KB stack (runtime-decoding.md). Such records keep
  building their decoder at load. The option is off by default, and its programs' trees and bytecode are already in
  PSRAM. Revisit once on-demand attribute decoders have been measured.
- **Init code** (`initCode`, the interpreter): unchanged, built at load.
- **The verifier** (`RAFT_RUNTIME_DECODE_VERIFY`): unchanged. It compares compiled records, and its decoders take
  no slots.
- **Freeing decoders** of types no longer present: not needed for the memory figures above, and unsafe while
  users hold the pointers.

## Changes

| Repo | File | Change |
|---|---|---|
| RaftCore | `DeviceTypeRecords.h/.cpp` | `ensureDecoder`, per-record decoder state, release/acquire on `pollResultDecodeFn`, stats, limits, policy, `DecoderKind` values; `addRecord`/`replaceBaseRecord` check without keeping (unless `atLoad`) |
| RaftCore | `DeviceTypeRuntimeDecoder.h/.cpp` | `getAllocatedBytes()` |
| RaftCore | `DeviceManager.cpp` | `ensureDecoder` in `busElemStatusCB` (built) and `setupStaticDevice`; `typeTable.runtimeDecoders` / `maxDecoderBytes` settings; report the new decoder kinds and stats |
| RaftCore | `DeviceTypeFileLoader.cpp` | `noDecoder` from the decoder state (`Failed` or can't decode), not from whether a decoder exists |
| RaftCore | wiki `DeviceManagerSettings.md`, `runtime-decoding.md` | The settings, the states, the memory figures |
| RaftSysMods | none (built) | BLE reports identification through `busElemStatusCB`; the branch no longer copies the record |
| RaftI2C | none | |
| Axiom | none | DataLogger reads the decoder when a device registers, after identification |

## Tests

**Linux** (`linux_unit_tests`):

- `test_device_type_table`: an added record has no decode function and is `RuntimePending`. After `ensureDecoder`
  it has the same field descriptions and struct size as with `atLoad`. A second `ensureDecoder` builds nothing. A
  record that can't be decoded is `Failed` at load, and `ensureDecoder` doesn't retry it. With policy `off`, nothing
  is built. The budget stops building at its limit, and the stats add up.
- Concurrency: two threads call `ensureDecoder` on the same type while a third decodes through `getPollDecodeFn`.
  Exactly one decoder is built, and no decode uses a half-attached record.
- `test_runtime_decoder`, `test_reduced_catalogue`: call `ensureDecoder` before decoding, and also check the
  equivalence with `atLoad`.
- `test_device_type_file`: `noDecoder` outcomes unchanged.

**On the board** (`unit_tests`, both `unittest` and `unittest_psram` on the Axiom):

- `DeviceTypeTable_test`: adding the default number of records costs only the packed records. `ensureDecoder` on 4
  of them adds about 4 decoders' bytes (internal without PSRAM, PSRAM with). With PSRAM, `ensureDecoder` on 65 types
  builds 64 and reports the 65th `Failed` ("no slot").
- Stack and time of `create` on a task with a 5 KB stack, for the 10 largest records.

**On the Axiom** (Axiom018 build with its type file):

- Boot: internal and PSRAM free before and after, compared with today.
- Plug in a device whose record is in the type file: identified, `devman/typeinfo` goes `runtimePending` ->
  `runtime`, DataLogger CSV decoded, `devman/value` works, `workerStack` least free reported.
- A second device of the same type: no second decoder (stats).
- `typeTable.runtimeDecoders: "off"` on a build with PSRAM disabled: raw data still published, DataLogger reports
  the device as not decodable.

## Costs and risks

- **Size:** a few hundred bytes of code; one byte per extended record.
- **First identification** of a runtime-decoded type is slower by one `create` (to be measured).
- **Main risk:** attaching to a published record. It's covered by the write order and the identification report
  being the synchronisation point; the concurrency test checks it. A future reader that copies a record of a type
  with no device present and caches its fields would see no decoder. `getDecoderKind` makes that visible.
- **Behaviour change:** decoding of every type is no longer ready at boot, only of types present. Nothing found
  depends on that (the readers are listed above); `atLoad` keeps the old behaviour available.

## Decisions (2026-10-22)

- **D1** - default policy without PSRAM: `onIdentify` (on-device decoding for the types present, about 1 KB of
  internal RAM each). `off` is available as a setting.
- **D2** - default decoder budget without PSRAM: 8 KB.
- **D3** - done on the `feature/device-type-records` branch, before it is merged.

## Order of work

1. Measure `create` stack and time on the board (no behaviour change; a test in `unit_tests`).
2. `ensureDecoder`, states, attach ordering, the call from `getPollInfo`; Linux tests.
3. Settings, budget, stats, reporting; DeviceManager and the loader.
4. On-target tests on both SysTypes; Axiom checks; RaftSysMods BLE line.
5. Wiki and runtime-decoding.md.
