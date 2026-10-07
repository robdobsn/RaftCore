# Array Outputs in Custom Decode, and Device Samples Over 255 Bytes — Plan

Status: **Plan only — no code changes yet** (2026-10-05).

Scope: RaftCore, RaftI2C, RaftSysMods (firmware) and raftjs.

Motivation: the VL53L5CX multizone ToF work
(RoboticalAxiom1 `devdocs/vl53l5cx-vl53l8cx-multizone-tof-plan.md`) exposed two framework
limits. Together they stopped a record from decoding its raw 8x8 frame generically:

1. **Custom decode (pseudocode) cannot write array attributes.** Only scalar
   `out.name = v` is supported.
2. **Published device samples are capped at 255 bytes.** The devbin per-sample length
   is a single byte. An 8x8 frame is 320 bytes; a full-output frame is 1444.

The VL53L5CX itself is out of scope here: it keeps its current on-device decode (option A).
This plan only removes the two limits, so that records can do this kind of thing in future.

Backwards compatibility is a hard requirement throughout:

- existing records, firmware and app builds in the field must behave exactly as now;
- a new raftjs must work with old firmware;
- an old raftjs must fail gracefully, not show garbage, if it meets the new encoding.

---

## Part A — Array outputs in custom decode

### A.1 Where things stand

Array attributes already exist end to end for **standard** (non-custom) decode:
- `"t": "<h[64]"` / `"B[9]"`;
- raftjs `getAttrElemsPerSample` and `elemsPerSample`;
- `DecodeGenerator` emits C array fields (`int16_t dist[64]`) and per-element extraction
  loops (`DecodeGenerator.py:199-203, 406-409`).

Custom decode (`resp.c`, pseudocode) is transpiled twice:

| Target | Where | How `out.x = v` is handled | Arrays today |
|---|---|---|---|
| Firmware C++ | `PseudocodeHandler.generate_cpp_code` via `DecodeGenerator.gen_custom_extraction_code` (`DecodeGenerator.py:251-327`) | Text substitution `out.` → `pOut->` on ID tokens (`:299`); `next` becomes the loop-end lines | `out.dist[i]=v` probably *compiles* to `pOut->dist[i]=v` (the struct field is an array), with caveats below |
| raftjs | `transpilePseudocodeToJs` (`PseudocodeTranspiler.ts:78-127`), compiled with `new Function` (`RaftCustomAttrHandler.ts:92`) | `out` is a Proxy whose `set` pushes `v` onto `attrValues[name]`; `next` is a no-op | `out.dist[i]=v` **throws**: `out.dist` reads as `undefined` |

**Other gaps found:**
- **raftjs sign extension (custom path).** `RaftAttributeHandler.ts:83-84` computes
  `byteWidth = structSizeOf(attrDef.t)`. For `"<h[64]"` that is the whole array (128 bytes),
  so the sign-bit shift is wrong. The standard path uses the element size correctly (`:361`).
- **Firmware sign extension (custom path).** `DecodeGenerator.py:270-288` looks up the raw
  type in `pystruct_map`. `"<h[64]"` is not found, so arrays are silently skipped. This only
  matters when `o` is `float`.
- **Bounds.** A firmware `pOut->dist[i]` with `i` out of range writes past the struct.
- **Stale elements.** Struct memory is not cleared per sample, so elements a decode does not
  write keep whatever was there.
- **`AttrFieldDesc` has no element count** (`DeviceTypeRecord.h:37-45`). By-name field access
  (`DeviceManager.cpp:~2128`) treats an array as a scalar and reads element 0.
- **`next` semantics:** decodes that never call `next` (e.g. SCD30, GravityO2) emit exactly one
  sample. Array writes must follow the same rule.
- **Workaround that already exists:** a record can provide explicit JavaScript in `resp.c.j`,
  which can push array elements directly (`attrValues.dist.push(...)`). It means keeping
  the same logic twice, so it is not the answer, but it stays supported.

### A.2 Syntax and semantics

```
out.<attr>[<int expression>] = <expression>;
```

- **Simple assignment only.** No compound operators (`+=`) and no reads of `out.*`. This
  matches how scalars are used today.
- `<attr>` must be an array attribute in `resp.a`, i.e. its `t` has a repeat count.
- **Out-of-range writes are ignored** on both targets, never a crash.
- **One sample** is one `next;` or, for decodes without `next`, the end of the code. At sample
  end:
  - every array attribute contributes exactly `elemsPerSample` values;
  - elements not written in that sample are **0**.
- **Signedness:** values are treated like scalars. Pseudocode produces raw integers, and sign
  extension then applies **per element** using the element type's width.
- Scalars and arrays may be mixed in one decode. Existing scalar-only decodes are unchanged,
  token for token.

Example (illustrative, word-swapped int16 grid):

```
int i=0; while(i<64){ int k=OFF+i*2; out.dist[i]=buf[(k&~3)+3-(k&3)]|(buf[((k+1)&~3)+3-((k+1)&3)]<<8); i++; }
```

### A.3 Firmware changes (RaftCore scripts + one struct)

1. **`PseudocodeHandler`: rewrite array writes.**
   - Recognise the token pattern `ID("out.<name>") LBRACK … RBRACK ASSIGN … SEMI` and emit
     `{ int __i=(<idx>); if (__i>=0 && __i<<N>) pOut-><name>[__i]=(<val>); }`.
   - `N` comes from a new `array_sizes` argument (attr name → repeat), supplied by
     `DecodeGenerator`.
   - Token-level, like the existing substitutions; no parser needed.
2. **`DecodeGenerator.gen_custom_extraction_code`:**
   - pass `array_sizes`;
   - zero the array fields at the start of each sample: after the timestamp extract, and
     after `pOut++` inside the `next` lines;
   - give sign extension a per-element loop for arrays, using the base type.
3. **Build-time validation in `ProcessDevTypeJsonToC.py`**, which fails the build:
   - `out.x[…]` where `x` is not an array attribute;
   - `out.x = …` where `x` *is* an array.
4. **`AttrFieldDesc`:** add `uint16_t count` (1 for scalars) and emit it in
   `get_field_desc_def`. This is generated code only, so no compatibility concern.
   - The `DeviceManager` by-name access keeps returning element 0 (as now) but can then
     check `count`.
   - Optional follow-up: `name[i]` lookup.
5. **Python output of `PseudocodeHandler`** (`generate_python_code`, CLI only): bring it to
   parity, or mark arrays as unsupported there.

### A.4 raftjs changes

1. **`transpilePseudocodeToJs`:**
   - Rewrite `out.<name>[` … `] =` … `;` into `__setElem("<name>", <idx>, <val>);`. Same
     token pattern as the firmware.
   - `NEXT` becomes `__endSample();` instead of a no-op.
   - The `PREAMBLE` gains:
     - a per-sample staging map;
     - `__setElem`, which bounds-checks against the attribute's element count and ignores
       out-of-range writes;
     - `__endSample`, which for each array attribute pushes exactly `elemsPerSample` values,
       zero-filled, onto `attrValues[name]` and resets the stage.
   - Array names are passed in rather than inferred, so a write to an attribute that is not
     an array falls through to the existing scalar path.
   - A trailing `__endSample()` runs if staged writes remain at the end. That covers decodes
     without `next`; scalar-only decodes never stage anything, so their output is identical.
2. **`RaftCustomAttrHandler`:** pass an `elemsPerSampleByName` map as an extra `new Function`
   parameter. It is internal, and `j` functions can ignore it. Include it in the cache key.
3. **`RaftAttributeHandler` custom path (`:83-84`):** sign-extend with the element width
   (`structSizeOf(t) / getAttrElemsPerSample(t)`). This also fixes signed `B[n]`/`<h[n]` arrays
   that come from `j` code.
4. **No change** to `processMsgAttrGroup`'s per-sample count check. It already divides by
   `elemsPerSample`.

### A.5 Compatibility

- The new syntax only appears in new records.
- **Existing decodes:**
  - C++ output is unchanged apart from the zeroing lines (which arrays only) and
    `AttrFieldDesc.count`;
  - JS output for scalar-only decodes is byte-identical apart from the preamble.
- **An old raftjs given a new record with array pseudocode** throws inside the custom function.
  `RaftCustomAttrHandler` already catches this and logs (`:70`), so that device shows no data
  and nothing else breaks.
  - Records needing old-app support can also carry a `j` implementation, which old raftjs
    prefers.
- **Firmware:** the change is generated at build time from the same JSON, so firmware and
  records always match.

### A.6 Tests

- **Python:**
  - a unit test of `PseudocodeHandler` and `DecodeGenerator` on a record with
    `out.a[i]=…`, an out-of-range index and a mix of scalar and array writes;
  - compile the generated C++ in the RaftCore linux unit tests (RaftROS `linux_unit_tests`
    has the harness).
- **raftjs:**
  - `PseudocodeTranspiler.test.ts`: the rewrite, plus existing decodes unchanged;
  - `RaftAttributeHandler.test.ts`: `<h[4]` signed, `B[9]`, partial writes zero-filled,
    `next` with arrays, no-`next` single sample;
  - one end-to-end firmware ↔ raftjs check using a test record (see Phase 3).

---

## Part B — Device samples over 255 bytes

### B.1 Where things stand

The devbin record body is `[deviceSeq:1] ([sampleLen:1][sample])*` ("DevbinV1Framed" in
raftjs `devdocs/devbin-protocol-versioning.md`). Each sample is the 2-byte poll timestamp
plus `resp.b` bytes.

**Firmware producers** — each writes the 1-byte length itself and **truncates** anything
longer, which silently corrupts that sample:

| Producer | Location | Notes |
|---|---|---|
| Bus devices (I2C) | `RaftI2C` `DeviceIdentMgr::getQueuedDeviceDataBinary` (`DeviceIdentMgr.cpp:552`) | Lengths come from the aggregator as `uint16`. Truncates to 255 but advances by the full length, so framing survives and the data is cut short |
| Direct devices (Power, LEDs, Demo) | `RaftCore` `RaftDevice::genBinaryDataMsg` (`RaftDevice.cpp:103`) | |
| BLE bus devices (BTHome) | `RaftSysMods` `BLEBusDeviceManager::getQueuedDeviceDataBinary` (`BLEBusDeviceManager.cpp:210`) | Always small in practice |

Record framing: `[recordLen:2]` covers the body, so **a record is limited to 65,535 bytes**.
There is no guard today: `genBinaryDeviceRecord` would wrap silently.

**Consumers.** raftjs is the only devbin parser in this workspace, in two places:
- the sample loop (`RaftDeviceManager.ts:519`);
- the format probe `areDevbinV1FramedSamplesValid` (`:1014`).

Dashboards, marty-web-app and the RaftMotorControl WebUI all go through the raftjs package.
The camera frame topic is a separate format and is unaffected.

**What an old raftjs does with an unexpected byte:**
- a `sampleLen` of **0** ends parsing of that record (`:522`, `break`);
- the probe treats 0 as invalid (`:1016`);
- the bus number is masked to `& 0x0f` (`:369`).

### B.2 Options

| Option | Wire change | New firmware → **old** raftjs |
|---|---|---|
| a. Status-byte flag (bit 5) = 2-byte lengths for the whole record | Bit 5 set; every `sampleLen` in the record is a uint16 | Old raftjs ignores the bit and reads the length's high byte as a 1-byte length. It **misparses that record and can decode garbage values** for the device |
| b. **Long-sample escape**: `sampleLen = 0x00` followed by `uint16` BE length | Only samples over 255 bytes change; everything else is byte-identical | `sampleLen 0` → `break`. Old raftjs **silently drops that device's samples** from the first long sample onwards; no garbage. The other records in the frame are unaffected because records are framed by `recordLen` |
| c. New envelope magic (`0xDC`) selecting 2-byte lengths everywhere | Every frame changes | Old raftjs accepts `0xDB..0xDF` as one class and **misparses every device** |
| d. Split long samples across several short ones | Needs reassembly metadata | Old raftjs decodes fragments as separate samples, which is garbage |

**Recommendation: (b), the long-sample escape.**
- It is the only option where the old client fails safe.
- No existing device changes a single byte on the wire.
- No negotiation is needed. Records and samples are self-describing, and short and long
  samples can mix in one record.
- `sampleLen = 0` has never been valid (a sample is at least the 2-byte timestamp), so the
  value is free.

### B.3 Wire format

```
sample := [len:1][data:len]                    len 1..255   (unchanged)
        | [0x00][lenHi:1][lenLo:1][data:len]   len 256..65535 (new)
```

- Firmware uses the escape **only** for samples longer than 255 bytes.
- Parsers accept the escape for any length ≥ 1, for robustness.
- A record body must not exceed 65,535 bytes. Firmware starts another record for the same
  device when the next sample would overflow it. raftjs looks up device state per record,
  so repeated records for one device should be processed in order. **Verify this with a test
  before relying on it.**
- **Naming:** the raftjs versioning doc says a payload-format layout change gets a new
  `<n>`. The proposal is to call the escape-capable format **`DevbinV2Framed`** and document
  V1Framed as its strict subset. Parsers then handle both with one code path. This is a
  decision to confirm (see §D).

### B.4 Firmware changes

1. **RaftCore `RaftDevice`:** add one shared helper,
   `appendLengthPrefixedSample(std::vector<uint8_t>& payload, const uint8_t* pData, uint32_t len)`.
   It writes the short or escaped form and rejects anything over 65,535 with a warning.
2. **Use it in all three producers** in place of their own prefix-and-truncate code:
   `RaftDevice::genBinaryDataMsg`, `DeviceIdentMgr::getQueuedDeviceDataBinary` and
   `BLEBusDeviceManager::getQueuedDeviceDataBinary`.
3. **Record-size guard in `DeviceIdentMgr::getQueuedDeviceDataBinary`:**
   - stop adding samples to a record before it passes 65,535 bytes (minus the 8-byte header);
   - start a new record for the remainder, with the same address and type plus the next
     `deviceSeq`;
   - `genBinaryDeviceRecord` itself should refuse (and log) an oversize payload rather than
     wrap the length.
4. **Capability flag:** advertise `devbinLongSamples` in the firmware capability list, which
   raftjs already fetches (`refreshCapabilities`). Apps can then explain missing data
   ("update the app") instead of showing nothing. It is informational only, not a switch.
5. **Unchanged:** the aggregator (already stores `uint16` lengths), `resp.b` / `pollDataSizeBytes`
   (already `uint16`) and the devjson path (no cap).

### B.5 raftjs changes

1. **`RaftDeviceManager.handleClientMsgBinary` (`:519`):** if `sampleLen === 0` and 2 bytes
   remain, read a `uint16` BE length and continue as normal. A truncated escape gets the
   existing malformed-sample warning.
2. **`areDevbinV1FramedSamplesValid` (`:1014`):** the same escape handling, so the format
   probe still picks the framed format.
3. **Docs:** update `devdocs/devbin-protocol-versioning.md` (new variant row and truth table)
   and the record-layout comment block at `RaftDeviceManager.ts:229-277`.
4. **Optional:** read the `devbinLongSamples` capability and surface a hint.

### B.6 Compatibility matrix

| Firmware | raftjs | Result |
|---|---|---|
| old | old | unchanged |
| old | new | unchanged — old firmware never emits the escape; Cog v1.9.5 `DevbinV0Fixed` is untouched |
| new, no sample > 255 | any | **byte-identical** to today |
| new, a sample > 255 | new | decoded correctly |
| new, a sample > 255 | old | that device's samples are dropped from the first long sample in each record; other devices unaffected; no garbage |

The format probe has a theoretical edge case. An old raftjs could judge a record containing
escapes to be valid `DevbinV0Fixed` if its payload length happened to be an exact multiple of
the fixed sample size (`1 + N×(b+5) ≡ 0 mod (b+2)`). For a 320-byte sample that first happens
at N = 107 samples in one record, far beyond the configured store sizes (`s`), so it is
negligible. Note it in the versioning doc.

### B.7 Limits this does not remove

These matter for any record that wants big samples:

- **I2C reads:** each poll op still reads at most 240 bytes (the silent clamp in
  `RaftI2CCentral`), so a long sample needs several poll ops. Splitting large reads inside
  the driver is a separate item, already listed in the VL53 plan §5.
- **Memory:** the aggregator holds `s × (b+2)` per device, in PSRAM above 16 KB.
- **Bandwidth:** large samples at high rates are fine over WebSocket but can exceed BLE
  throughput. BLE splits messages into MTU-sized pieces (`BLEGattOutbound.cpp:306-316`), so it
  is a rate problem rather than a cap. Verify on hardware.

### B.8 Tests

- **Firmware:**
  - unit-test `appendLengthPrefixedSample` at 1, 255, 256 and 65,535 bytes, and above;
  - unit-test the record splitting.
- **raftjs** (`RaftDeviceManager.test.ts`):
  - escaped samples, mixed short and long samples, a truncated escape;
  - a probe test;
  - a regression test that the existing fixtures are unchanged.
- **Old-client check:** run the *current* `main` raftjs against a frame containing an escape
  and assert that it drops the device record cleanly. This keeps the fail-safe claim tested.

---

## C. Delivery

Each part can ship on its own. Suggested order:

1. **Phase 1 — B (long samples).** It is the smaller change and has no record-format impact.
   - RaftCore, RaftI2C, RaftSysMods and raftjs on a feature branch (e.g. `devbin-long-samples`)
     in each repo.
   - Release raftjs first (a new parser is harmless against old firmware), then firmware.
2. **Phase 2 — A (array pseudocode).**
   - RaftCore scripts and `DeviceTypeRecord.h`, plus raftjs, on `pseudocode-arrays` branches.
   - Same release order: raftjs first.
   - Only then may records start using the syntax. Records needing old-app support also ship
     a `j` implementation.
3. **Phase 3 — proving record.** A test-only device type that exercises both changes:
   - a dummy `ext` device whose samples are larger than 255 bytes;
   - array pseudocode decoding them, fed from a test SysMod (the `ext` + `handlePollResult`
     path from the VL53 work);
   - check it end to end in firmware decode (devjson), raftjs and the dashboard.

   The VL53L5CX record can then be moved to option B later if wanted, but that stays out of
   scope here.

**Docs to update with the code:**
- RaftCore.wiki `DeviceTypeRecordFormat.md`: custom decode arrays, the record-size note, and
  `pollInfo.ext` from the VL53 work;
- raftjs `devdocs/pseudocode-to-js-transpiler.md`;
- raftjs `devdocs/devbin-protocol-versioning.md`.

## D. Decisions needed

1. **Variant name:** `DevbinV2Framed` (per the versioning doc's rule), or keep
   `DevbinV1Framed` and document the escape as an extension of it.
2. **Unwritten array elements:** 0 (proposed), or NaN in raftjs. NaN is more honest in charts
   but differs from firmware, where the struct value is 0.
3. **Array syntax:** write-only `out.a[i] = v` (proposed), or also allow reads and compound
   ops. That would need a real expression parser on both sides.
4. **The `devbinLongSamples` capability:** add it now, or only if an app needs it.
5. **Order:** do Phase 1 and Phase 2 together or separately. Separately is recommended:
   smaller reviews, and each is useful alone.
