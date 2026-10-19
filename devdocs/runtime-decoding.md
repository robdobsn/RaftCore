# Runtime decoding of device type records (D11)

Date: 2026-10-19.

**Status:**
- **Option 1, attribute decoding: implemented** (this document, sections 1-4).
- **Option 2, the code interpreter: planned** (section 5).

## 1. Why

A record compiled into the firmware gets a generated C++ decoder. A record that arrives at runtime gets none, unless it overrides a compiled record with the same response layout. That covers a new record from a device type file, an override with a changed `resp`, and a record registered by firmware code.

Without a decoder, the on-device users of decoded data don't work for that record: DataLogger CSV, RaftROS AutoPub (N5) and named values. Only clients that decode the data themselves (raftjs, the WebUI) see its values. That limited the "all records at runtime" setup (record management plan, M8).

## 2. Design

**Where:** `DeviceTypeRuntimeDecoder` (RaftCore `components/core/DeviceTypes`).

**What it does:**
- It reads a record's `resp` attribute list.
- It builds the same struct layout and field descriptions (`AttrFieldDesc`) that `DecodeGenerator.py` generates.
- It decodes poll results the way the generated standard decode does.

**Exactness:** each step mirrors the generated code.
- The value is read with the same `Raft::get…AndInc` reader, unsigned for a signed type with `m`.
- The working value is the generator's intermediate type, a signed type one size larger. It is held in 64 bits and wrapped to the intermediate type's width after each operation. Bitwise and modular arithmetic then give the same bits as the generated code, with any C literal type.
- A float intermediate stays a `float`, so even a NaN's bits match.
- The value is stored to the field's type, then `d` and `a` are applied in that type. An integer literal (`8192`) and a floating one (`1024.0`) behave as they do in C++: integer or float arithmetic.
- `lut` is worked in a double, as generated.
- An `at` list of byte positions assembles the bytes into one value. The generated code's bounds check for this case compares pointers into different buffers; no record uses it.

**Hook-up:** `DeviceTypeRecords` gives any record with no decoder a runtime decoder when the record is added (`attachRuntimeDecoderLocked`). It fills the record's `pollResultDecodeFn`, `pollFieldDescs`, `pollFieldCount` and `pollStructSize`, so every user of a record works unchanged.

**The function pointer:** `DeviceTypeRecordDecodeFn` is a plain function pointer. So each decoder takes one of `RAFT_RUNTIME_DECODE_MAX_DECODERS` (default 64) slots, and each slot has its own small function.

**Lifetime:** records are never freed while their table exists. Decoders live as long as the table, and free their slots with it.

**Re-adding a record:** a record given no decode function that has one attached (inherited or runtime) still matches itself when added again (`DeviceTypeRecordPacked::contentMatches`).

**Reporting:**
- `devman/typeinfo` gives `"decoder": "compiled" | "runtime" | "none"`.
- The type file check's `noDecoder` reflects what the firmware built.
- The host-side type file build (`AssembleDeviceTypes.py --typefile`) reports `decoder: "runtime"` for a record with no compiled decoder, or `"none"` for one with custom code.

**Not decoded at runtime:**
- records with custom decode code (`resp.c`), which needs option 2;
- records with an attribute the build couldn't decode (e.g. `t: "h"` with no endian). These are logged.

## 3. Build flags

Set in a SysType's `features.cmake`:

| Flag | Default | Effect |
|-|-|-|
| `RAFT_RUNTIME_DECODE` | `ON` | Builds the attribute decoder. `OFF` saves about 17 KB of flash (Axiom018: 1,944,112 vs 1,926,608 bytes) |
| `RAFT_RUNTIME_DECODE_CODE` | `OFF` | The code interpreter (option 2). Not yet implemented |

The RaftCore CMake turns them into compile definitions of the same names. The Linux unit tests build with `RAFT_RUNTIME_DECODE`.

**Flash, by function:** about 15.6 KB in all.
- `parse` (the attribute JSON): 4.9 KB.
- `decodeElem` (the per-field-type store paths): 4 KB.
- The rest: small functions, plus 64 slot functions.

A first version templated on every working, field and literal type cost 41 KB.

## 4. Tests

**`linux_unit_tests/test_runtime_decoder`:**
- **Equivalence:** for every compiled RaftCore record with a standard decode (47 records), a decoder made from the record's own JSON must have the same struct size and field descriptions. It must also decode 3000 random poll buffers to the same bytes, record count and timestamp state as the compiled decoder. The buffers are biased towards 0x00, 0xFF, 0x7F and 0x80, and include several records and truncated ends. The first runs caught a float NaN difference, which is fixed.
- **Integration:** a type file record gets a runtime decoder and decodes through its `pollResultDecodeFn`. The same file applied again isn't a conflict. Custom code gets no decoder. Slots are freed with the table.
- **Also tested:** `at` lists, and attributes the decoder refuses.

**Elsewhere:**
- `test_device_type_file`: `noDecoder` follows the build flag.
- Python: `decoder` entries in the type file build.

**On Axiom018:** the Power device's code-registered record now reports `"decoder":"runtime"`, and compiled records report `"compiled"`.

## 5. Option 2: the code interpreter (planned)

**Why:**
- 11 RaftCore records use custom decode code: BME280/680/690, MS8607PT, GravityO2, LTR-329, MICS6814Pimoroni, SCD30, MAX30101 and LSM6DS3/TRC.
- The same interpreter would let type files carry `initCode` (`computed-init-values.md`).

**The language:** int and float variables, `if`/`else`, `while`, `next`, `buf[]`, `out.<name>` (including array elements) and C operators (PseudocodeHandler).

**Plan:**
- **Compile on the device** when the record is added: parse to a compact bytecode, rejecting anything outside the language. That's safer than trusting bytecode from a file.
- **Run** with an instruction budget per poll, since `while` could loop forever, and with `buf[]` bounds-checked.
- **Match the generated decoder's semantics,** including the appended `next`, the per-sample zeroing of array attributes, the sign extension of signed attributes stored as float, and `us` sample time increments.
- **Test** the same way: equivalence against the 11 compiled custom decoders over random buffers. Also compare against raftjs's JavaScript transpiler for the same code.
- **Build flag:** `RAFT_RUNTIME_DECODE_CODE`.
