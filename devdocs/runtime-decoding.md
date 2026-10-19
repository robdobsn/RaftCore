# Runtime decoding of device type records (D11)

Date: 2026-10-19.

**Status:**
- **Option 1, attribute decoding: implemented** (sections 1-4).
- **Option 2, the code interpreter: implemented** (section 5).

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
| `RAFT_RUNTIME_DECODE_CODE` | `OFF` | The code interpreter (option 2): custom decode code and init code of type file records. About 23 KB more (Axiom018: 1,967,616 vs 1,945,040 bytes) |

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

## 5. Option 2: the code interpreter (implemented)

**Why:**
- 11 RaftCore records use custom decode code: BME280/680/690, MS8607PT, GravityO2, LTR-329, MICS6814Pimoroni, SCD30, MAX30101 and LSM6DS3/TRC.
- The same interpreter lets type files carry init code (`initCode`, `computed-init-values.md`).

**What the language means:** the build passes the code token by token into C++, so its meaning is C++'s:
- `int` and `float` are 32-bit;
- a literal such as `16.0` is a `double`;
- `buf[i]` is an unsigned byte promoted to `int`;
- `out.<name>` has its struct field's type.

**How the interpreter matches it:** `DeviceTypeCodeProgram` (`DeviceTypeCodeInterpreter.h/.cpp`) compiles the code once, when the record is added, into a typed tree.
- **At compile time:** each expression's type is worked out by C++'s rules (literal types, integer promotions, the usual arithmetic conversions).
- **At run time:** it evaluates with the same C++ operations on those types.
- **Accepted:** declarations (`int`, `float`, `int8_t` to `uint32_t`), assignment and compound assignment, `++`/`--`, casts, `if`/`else`, `while`, `return`, `next`, the C operators (no `?:` or comma operator), decimal/hex/float literals, `buf[]`, `out.<attribute>` (arrays written only, `out.<name>[i] = value`) and `init.<name>`.
- **Refused:** anything else, such as function calls, undeclared names or float operands of integer operators.

**Protections compiled code doesn't need:**
- `buf[]` outside the data gives 0;
- integer division by zero gives 0;
- an instruction budget (200,000 per decode call, 100,000 per init) stops a loop that doesn't end.

**The custom decode wrapper** (`DeviceTypeRuntimeDecoder::decodeCustom`) follows the generated one:
- the per-record timestamp;
- array fields zeroed for each sample;
- at `next`: the sign extension of signed attributes stored as float, the move to the next output record (when full, a `break` out of the innermost loop) and the `us` sample time step;
- `next` appended to code without one (found the way the build's lexer finds it);
- `return` returning the function's result.

**Init code** (`DeviceTypeRuntimeInit`) runs as the record's init values function:
- **Contract:** false if fewer bytes were read than the init steps before the first slot or guard read; the names follow their first use.
- **Slots:** its own pool of `RAFT_RUNTIME_INIT_MAX` (16).
- **Type files:**
  - With the interpreter, a record may carry `initCode`; it must compute every value slot and guard its init string uses. An override's own `initCode` replaces the compiled one.
  - Without the interpreter, the earlier rule stays: the record must override a compiled record whose init code computes them.
  - The host-side type file build reports `initCode: "runtime"` (only firmware with the interpreter runs it) or `"runtime or compiled"`.

**Tests** (`test_runtime_decoder`):
- **Decode:** all 58 records with a decode, including the 11 custom ones, decode byte for byte as their compiled decoders over 3000 random poll buffers each, with output room for 1, 4 or 32 records.
- **Init:** the 4 records with init code (BME680, BME690, MICS6814Pimoroni, SCD30) give the compiled init functions' results and values over 5000 random read buffers each.
- **Language:** cases against native C++, covering signed/unsigned comparison, integer division, double literals stored to float, casts, shifts, `++`, compound assignment, loops, `!`/`~`/`&&`/`||`, out-of-range `buf`, division by zero and `int8_t` wrapping. Plus the budget, `return`, and refused code.
- **Type files** (`test_device_type_file`): a record's own `initCode` computes its slot and writes it; a slot its code doesn't compute is refused, and so is code outside the language.
