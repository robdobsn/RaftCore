# Variable-Length Poll Reads

## Problem

The current poll command format uses fixed read sizes (e.g. `0x3a=r4&0x3e=r192`). Each `=rN` specifies a static byte count determined at parse time. For FIFO-based devices like the LSM6DS, this forces a worst-case read every poll cycle — reading 192 bytes even when only 12 bytes of useful data are in the FIFO — wasting I2C bus time and bandwidth.

More generally, many I2C devices provide a status or count register that indicates how much data is actually available. An efficient driver would read the count first, then read only the required number of data bytes.

## Current Architecture

### Poll command string

The `pollInfo.c` field is a `&`-separated list of I2C operations. Each operation is:

```
<register_address>=<read_or_write_spec>
```

Where `<read_or_write_spec>` can be:
- `rN` — read N bytes from the register
- `0xHH...` — write hex data to the register
- `pN` — pause N ms after the operation

### Parsing (DeviceTypeRecords.cpp)

`getPollInfo()` splits on `&`, then for each pair:
1. Left side → `extractBufferDataFromHexStr()` → write data (register address bytes)
2. Right side → `extractMaskAndDataFromHexStr()` → read length from `rN`

Each pair becomes a `BusRequestInfo` with a fixed `_readReqLen`.

### Execution (DevicePollingMgr.cpp)

`taskService()` iterates `pollInfo.pollReqs[]` sequentially. For each request:
1. Calls `_busReqSyncFn(&busReqRec, &readData)` — synchronous I2C transaction
2. Appends `readData` to `pollDataResult`
3. If pause required, stores partial result and resumes next cycle

The key observation: **execution is already sequential** and earlier read results are available in `pollDataResult` before later requests execute. The infrastructure for partial results and multi-cycle polls already exists.

### Result storage (DevicePollingInfo)

`pollResultSizeIncTimestamp` is computed at parse time as the sum of all `rN` values + 2 (timestamp). This sets the fixed buffer size for storing results.

## Proposal: Computed Read Length Expressions

### Syntax extension

Extend the `rN` syntax with a new form `r{expr}` where `expr` is a compact expression that computes the read length at poll-time from previously-read bytes.

The expression language operates on a virtual register file populated from prior read results in the current poll cycle:

```
0x3a=r4&0x3e=r{$0.w12:mask0FFF*2:max192}
```

Breaking this down:
- `$0` — reference to the result of poll operation at index 0 (the `0x3a=r4` read)
- `.w12` — extract a 12-bit little-endian word starting from byte offset 0
- `:mask0FFF` — AND with 0x0FFF
- `*2` — multiply by 2 (each FIFO word = 2 bytes)
- `:max192` — clamp to maximum of 192

### Expression elements

#### Source references

| Syntax | Meaning |
|--------|---------|
| `$N` | Result bytes from poll operation index N (0-based) |
| `$N[B]` | Single byte at offset B from operation N's result |
| `$N.wE` | Little-endian extract starting at byte 0, E bits wide |
| `$N[B].wE` | Little-endian extract starting at byte B, E bits wide |
| `$N[B].WE` | Big-endian extract starting at byte B, E bits wide |

#### Operators (applied left-to-right, no precedence)

| Syntax | Meaning |
|--------|---------|
| `&HH` or `:maskHH` | Bitwise AND with hex value |
| <code>&#124;HH</code> | Bitwise OR with hex value |
| `>>N` | Right shift by N bits |
| `<<N` | Left shift by N bits |
| `*N` | Multiply by N |
| `/N` | Integer divide by N |
| `+N` | Add N |
| `-N` | Subtract N |
| `:maxN` | Clamp to maximum value N |
| `:minN` | Clamp to minimum value N |
| `:alignN` | Round down to nearest multiple of N |

### Worked example: LSM6DS

Current:
```json
"c": "0x3a=r4&0x3e=r192"
```

Proposed:
```json
"c": "0x3a=r4&0x3e=r{$0.w12:mask0FFF*2:max192}"
```

Evaluation at poll time:
1. Execute `0x3a=r4` → reads 4 bytes, e.g. `[0x1E, 0x00, 0x03, 0x00]`
2. Evaluate `$0.w12` → extract 12-bit LE from bytes [0],[1] → `0x001E` = 30 (FIFO word count)
3. `:mask0FFF` → `30 & 0x0FFF` = 30 (mask off status bits, no-op in this case)
4. `*2` → `30 * 2` = 60 (convert words to bytes)
5. `:max192` → `min(60, 192)` = 60
6. Execute `0x3e=r60` — reads only 60 bytes instead of 192

### Buffer sizing

Since the read length is variable, `pollResultSizeIncTimestamp` must still be computed as the **maximum possible** size (same as today). The `r{expr}` is evaluated at runtime, but the parse-time max is derived from the `:maxN` clamp in the expression. If no `:max` is present, fall back to a configurable default or require one.

Alternatively, introduce an explicit max-size annotation:

```
r{$0.w12*2:max192}    — max is 192, same as current static allocation
```

The parser extracts the `:maxN` value during `getPollInfo()` to set `pollResultSizeIncTimestamp`.

## Implementation Plan

### Phase 1: Core expression evaluator

Add a lightweight expression evaluator to `DeviceTypeRecords` (or a new utility class):

```cpp
class PollReadLenExpr {
public:
    /// Parse expression string like "$0.w12:mask0FFF*2:max192"
    bool parse(const String& expr);

    /// Evaluate with access to prior read results
    /// @param priorResults vector of result buffers from earlier poll operations
    /// @return computed read length in bytes
    uint32_t evaluate(const std::vector<std::vector<uint8_t>>& priorResults) const;

    /// Return the maximum possible value (from :max clamp)
    uint32_t getMaxValue() const;

private:
    uint32_t _sourceIdx = 0;        // $N index
    uint32_t _byteOffset = 0;       // [B] byte offset
    uint32_t _extractBits = 8;      // .wE bit width
    bool _bigEndian = false;        // .W vs .w

    struct Op {
        enum Type { AND, OR, SHR, SHL, MUL, DIV, ADD, SUB, MAX, MIN, ALIGN };
        Type type;
        uint32_t operand;
    };
    std::vector<Op> _ops;
    uint32_t _maxValue = UINT32_MAX;
};
```

### Phase 2: Integrate into parsing

In `DeviceTypeRecords::extractMaskAndDataFromHexStr()`:

```
Current: if readStr contains "rN", extract N as literal
New:     if readStr contains "r{...}", parse as PollReadLenExpr
         else if readStr contains "rN", extract N as literal (unchanged)
```

Store the parsed expression in `BusRequestInfo` alongside the existing `_readReqLen`:

```cpp
class BusRequestInfo {
    // ... existing fields ...
    uint16_t _readReqLen = 0;                          // static length (existing)
    std::shared_ptr<PollReadLenExpr> _readLenExpr;     // dynamic length (new, nullptr if static)
};
```

Add a method:
```cpp
uint16_t getReadReqLen(const std::vector<std::vector<uint8_t>>* priorResults = nullptr) const {
    if (_readLenExpr && priorResults) {
        return _readLenExpr->evaluate(*priorResults);
    }
    return _readReqLen;
}
```

### Phase 3: Integrate into execution

Modify `DevicePollingMgr::taskService()` to track per-operation results and pass them when requesting read lengths:

```cpp
// Track individual operation results for expression evaluation
std::vector<std::vector<uint8_t>> perOpResults;

for (uint32_t i = nextReqIdx; i < pollInfo.pollReqs.size(); i++)
{
    BusRequestInfo& busReqRec = pollInfo.pollReqs[i];

    // If this request has a dynamic read length, evaluate it now
    if (busReqRec.hasDynamicReadLen()) {
        uint16_t computedLen = busReqRec.getReadReqLen(&perOpResults);
        busReqRec.setReadReqLen(computedLen);    // or pass to the sync function
    }

    std::vector<uint8_t> readData;
    auto rslt = _busReqSyncFn(&busReqRec, &readData);

    // Store this operation's result for use by later expressions
    perOpResults.push_back(readData);

    // Append to aggregate result as before
    pollDataResult.insert(pollDataResult.end(), readData.begin(), readData.end());
    // ... rest unchanged ...
}
```

### Phase 4: Update DeviceTypeRecords.json

Change the LSM6DS entry:

```json
"c": "0x3a=r4&0x3e=r{$0.w12:mask0FFF*2:max192}"
```

No change needed to `"b": 196` or the decode logic — the decode `"c"` expression already handles variable-length data via the word count in the status bytes.

## Alternative Syntax Considered

### Option A: Register-style (rejected)

```
0x3e=r($0&0x0FFF*2,192)
```

Compact but ambiguous — parentheses conflict with pause syntax `pN`, and the comma overloads the address-range separator.

### Option B: Stack-based / RPN (rejected)

```
0x3e=r[$0 0FFF & 2 * 192 min]
```

Space-separated stack machine. Flexible but harder to read and parse within the existing `&`/`=` delimited format (spaces would need escaping or the delimiter scheme would need rethinking).

### Option C: Curly-brace infix (selected)

```
0x3e=r{$0.w12:mask0FFF*2:max192}
```

- Curly braces clearly delimit the expression from the rest of the command string
- No spaces needed
- Colon-prefixed named operators (`:mask`, `:max`, `:min`, `:align`) are self-documenting
- Symbolic operators (`*`, `+`, `>>`, `&`) for common arithmetic
- The `$N` reference is unambiguous within the hex-oriented format

## Scope and Constraints

- **Memory**: `PollReadLenExpr` is small (~40 bytes per instance). Only devices with dynamic reads allocate one.
- **Performance**: Expression evaluation is trivial integer arithmetic — nanoseconds compared to the millisecond-scale I2C transaction.
- **Backwards compatibility**: Existing `rN` syntax is unchanged. The `r{...}` form is opt-in per device.
- **Partial polls**: The existing partial-poll mechanism (pause between operations, resume next cycle) continues to work. `perOpResults` would need to be persisted in `DevicePollingInfo` alongside `_pollDataResult` for multi-cycle polls.
- **Error handling**: If a referenced operation `$N` hasn't executed yet (e.g. index out of range), fall back to the `:max` value. If no `:max` is specified, fall back to 0 (skip the read) and log a warning.
