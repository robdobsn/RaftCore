# RaftJson and String costs in hot paths

Proposed 2026-11-22. Not started.

## Background

On SandBot (ESP32-S3), the main loop couldn't keep a motion queue fed while
clients were subscribed to the status topic. Building one status message took
7.5 ms idle and 9.2 ms while moving, and about 40 ms per subscriber before the
first fixes. After the fixes it takes 0.43 ms idle and 0.66 ms while moving.
The full investigation is in the SandBot repo:
`SandBotFirmware/devdocs/MAIN_LOOP_PERFORMANCE.md`.

Some of the cost was in application code and RaftMotorControl. Three causes
apply to RaftCore generally, and the proposals below address them.

## 1. RaftJson lookups scan the document on every call

`RaftJson` doesn't build a parsed tree. `getDouble(path)` (via
`getNumericValue` → `locateElemByPath` → `locateElementValueWithKey`) scans
the JSON text from the start each time, skipping over every sibling element
before the key. The cost grows with the document size and the key's
position. A key that isn't found is scanned to the end of the document, then
looked up again in the chained document, if there is one.

This is fine in `setup()`. It isn't fine in anything called per loop, per
message or per named value. In RaftMotorControl, `getNamedValue` read four
config keys (`motion/geom`, `arm1LenMM`, `arm2LenMM`, `maxRadiusMM`) and
copied `AxesParams` on every call, whatever value was asked for. A status
message reads about 10 values, so that came to about 30 ms per message at
160 MHz. Fixed in RaftMotorControl PR #4. The lookups weren't timed apart
from the `AxesParams` copy, so their own share is not known.

**Proposals**

- Document the cost in `RaftJson.h` and `RaftJsonIF.h`: a lookup is
  O(document size); read config into members in `setup()` or on config
  change, never in `loop()`, `getNamedValue()`, publish callbacks or state
  hash callbacks.
- Audit the Raft libraries for `get*()` config reads in those paths:
  `grep -n "deviceConfig.get\|config.get\|configGet"` inside `loop`,
  `getNamedValue`, `getStatusJSON`, `getDataJSON` and the
  `registerDataSource` callbacks. Move hits to setup.
- Put numbers on it: extend `unit_tests/main/RaftJson_perf_extract.cpp` to
  time `getDouble` against key position in a document the size of a typical
  device config (2–10 KB). Use that to decide whether a cache is worth
  having.
- Optional, only if the audit finds reads that can't move to setup: a small
  path → value-offset cache in `RaftJson`, cleared whenever the document
  changes.

## 2. RaftJsonPrefixed allocates a String on every lookup

Every getter in `RaftJsonPrefixed` calls `getPrefixedDataPath(dataPath)`,
which returns a new `String` (prefix + "/" + path). That is a heap
allocation and free per lookup, about 7 µs for malloc on the ESP32-S3 at
240 MHz, on top of the scan.

**Proposal:** build the prefixed path in a fixed stack buffer (e.g. 128
bytes), falling back to `String` only when the path doesn't fit.

## 3. Building JSON text by hand

Status JSON is commonly built by appending to an Arduino-style `String`. Two
measured costs:

- `snprintf(buf, n, "%.3f", v)` took about 60 µs per value. The newlib
  float formatter works in double, and the S3's FPU is single precision only.
  A fixed-point formatter (scale to `int64_t`, print with `%lld` or by hand,
  insert the decimal point) costs a few µs.
- `String::equalsIgnoreCase("literal")` builds a temporary `String` from the
  literal (allocating for names over 11 characters). In a name-matching chain
  such as `getNamedValue` that is an allocation per comparison.
  `strcasecmp(param, "literal")` allocates nothing.

Appending with `+=` to a `String` that hasn't been `reserve()`d also
reallocates as the message grows.

**Proposals**

- Add `Raft::appendFixed(String& out, double v, int decimals)` (or the same
  on a `char*` buffer) to `RaftUtils`. SandBot's version, in
  `components/SandBot/SandBot/SandBot.cpp`, can be moved over as-is.
- Note in the coding guidelines for SysMods and devices: match names with
  `strcasecmp`, `reserve()` message Strings, and keep double trig out of
  per-message paths (about 25 µs per cos+sin pair at 240 MHz).

## Related

- RaftSysMods: StatePublisher builds an identical message once per
  subscriber (`devdocs/statepublisher-shared-message-build.md` there).
- RaftMotorControl PR #4: the `getNamedValue` fixes.
