"""Device type record command grammar (Python copy of RaftCore DeviceTypeGrammar.cpp)

detectionValues, initValues and pollInfo.c share one syntax, with a different meaning for the value side of a
step in each. compile() gives the same steps and issues as the C++ DeviceTypeGrammar::compile; both are checked
against scripts/tests/grammar_vectors.json, and test_grammar.py fuzzes the two against each other.

A string is steps separated by "&" or ";"; each step is "write[=value]". See DeviceTypeGrammar.h for the meaning
of the value side in each field, and for which issues are errors and which are warnings.
"""

import re

FIELDS = ("detection", "init", "poll")

ERRORS = ("NotHex", "MultipleEquals", "BadCheck", "BadReadExpr", "ReadExprNotAllowed", "BadReadLength", "BadValue",
          "CrcLength", "BadPause", "BadPlaceholder")
WARNINGS = ("OddHex", "BarePause", "InitValueNotWritten", "InitBinaryIsReadCount", "InitValueIgnored",
            "MixedCheckLengths", "BinaryNotWholeBytes")
FIELD_NAMES = {"detection": "detectionValues", "init": "initValues", "poll": "pollInfo.c"}

HEX_DIGITS = set("0123456789abcdefABCDEF")
WHITESPACE = " \t\n\v\f\r"


def is_error(code):
    return code in ERRORS


class Step:
    def __init__(self):
        self.write = b""
        self.read = 0
        self.pause = 0
        self.has_value = False
        self.checks = []        # list of (mask bytes, value bytes)
        self.crc8w = False
        self.expr = False       # read length is an r{...} expression
        self.slots = []         # init: value slots in the write data, as (byte offset, name) - {name} placeholders

    def to_dict(self):
        out = {"write": self.write.hex(), "read": self.read, "pause": self.pause}
        if self.checks or self.has_value:
            out["checks"] = [m.hex() + ":" + v.hex() for m, v in self.checks]
        if self.crc8w:
            out["crc8w"] = True
        if self.expr:
            out["expr"] = True
        if self.slots:
            out["slots"] = [f"{offset}:{name}" for offset, name in self.slots]
        return out


class Result:
    def __init__(self):
        self.steps = []
        self.issues = []        # list of (code, pos)

    def has_errors(self):
        return any(is_error(code) for code, _ in self.issues)


# ---------------------------------------------------------------------------------------------------------------
# C library behaviour the C++ relies on
# ---------------------------------------------------------------------------------------------------------------

def _strtol(s, pos=0, base=10):
    """strtol/strtoul: skip whitespace, optional sign, digits (base 16 allows 0x). Returns (value, end); end is pos
    if there are no digits"""
    i = pos
    while i < len(s) and s[i] in WHITESPACE:
        i += 1
    sign = 1
    if i < len(s) and s[i] in "+-":
        sign = -1 if s[i] == "-" else 1
        i += 1
    if base == 16 and s[i:i + 2].lower() == "0x" and i + 2 < len(s) and s[i + 2] in HEX_DIGITS:
        i += 2
    digits = "0123456789" if base == 10 else "0123456789abcdefABCDEF"
    start = i
    while i < len(s) and s[i] in digits:
        i += 1
    if i == start:
        return 0, pos
    return sign * int(s[start:i], base), i


def _first_pause_ms(lc):
    pos = lc.find("p")
    if pos < 0:
        return 0
    ms, _ = _strtol(lc, pos + 1)
    return max(ms, 0)


def _is_pause_token(s):
    return len(s) >= 2 and s[0] in "pP" and s[1:].isdigit() and s[1:].isascii()


def _hex_to_bytes(digits):
    out = bytearray((len(digits) + 1) // 2)
    for i in range(0, len(digits) - 1, 2):
        out[i // 2] = int(digits[i:i + 2], 16)
    return bytes(out)


def _read_len_expr_max(expr):
    """PollReadLenExpr::parse: returns the :max value, or None if the expression doesn't parse or has no max"""
    max_value = 0xFFFFFFFF
    p = 0
    end = len(expr)
    if p >= end or expr[p] != "$":
        return None
    p += 1
    _, p = _strtol(expr, p)
    if p < end and expr[p] == "[":
        p += 1
        _, p = _strtol(expr, p)
        if p < end and expr[p] == "]":
            p += 1
        else:
            return None
    if p < end and expr[p] == ".":
        p += 1
        if p < end and expr[p] in "wW":
            p += 1
            _, p = _strtol(expr, p)
        else:
            return None
    while p < end:
        c = expr[p]
        if c == ":":
            p += 1
            if expr.startswith("mask", p):
                _, p = _strtol(expr, p + 4, 16)
            elif expr.startswith("max", p):
                value, p = _strtol(expr, p + 3)
                max_value = value & 0xFFFFFFFF
            elif expr.startswith("min", p):
                _, p = _strtol(expr, p + 3)
            elif expr.startswith("align", p):
                _, p = _strtol(expr, p + 5)
            else:
                return None
        elif c in "*/+-":
            _, p = _strtol(expr, p + 1)
        elif c in "&|":
            _, p = _strtol(expr, p + 1, 16)
        elif c in "><" and p + 1 < end and expr[p + 1] == c:
            _, p = _strtol(expr, p + 2)
        else:
            return None
    return None if max_value == 0xFFFFFFFF else max_value


# ---------------------------------------------------------------------------------------------------------------
# Compile
# ---------------------------------------------------------------------------------------------------------------

class _Ctx:
    def __init__(self, result, step_pos):
        self.result = result
        self.step_pos = step_pos

    def issue(self, code, pos_in_step):
        self.result.issues.append((code, self.step_pos + pos_in_step))


def _set_pause(step, ms, ctx, pos):
    if ms > 65535:
        ctx.issue("BadPause", pos)
        return False
    step.pause = ms
    return True


def _is_slot_name(name):
    return bool(name) and (name[0].isalpha() or name[0] == "_") and all(c.isalnum() or c == "_" for c in name) and \
        name.isascii()


def _parse_write(name, ctx, field="detection"):
    """Returns (bytes, slots), or None if they aren't hex. In an init string the hex may contain {name} placeholders:
    each is one byte, written with the value the record's initCode computes (slots: (byte offset, name))"""
    start = 2 if (len(name) >= 2 and name[0] == "0" and name[1] in "xX") else 0
    digits = name[start:]
    if start == 2 and digits == "":
        ctx.issue("NotHex", 2)
        return None
    out = bytearray()
    slots = []
    hex_run = ""
    i = 0
    while i < len(digits):
        c = digits[i]
        if c == "{":
            end = digits.find("}", i + 1)
            slot_name = digits[i + 1:end] if end > 0 else ""
            # A placeholder: only in init strings, a name, and at a byte boundary
            if field != "init" or end < 0 or not _is_slot_name(slot_name) or (len(hex_run) % 2 != 0):
                ctx.issue("BadPlaceholder", start + i)
                return None
            out += _hex_to_bytes(hex_run)
            hex_run = ""
            slots.append((len(out), slot_name))
            out.append(0)
            i = end + 1
            continue
        if c not in HEX_DIGITS:
            ctx.issue("NotHex", start + i)
            return None
        hex_run += c
        i += 1
    if len(hex_run) % 2 != 0:
        ctx.issue("OddHex", 0)
    out += _hex_to_bytes(hex_run)
    return bytes(out), slots


def _parse_check_value(lc, ctx, pos):
    """Returns (kind, mask, value, read_len); kind None if invalid"""
    r_pos = lc.find("r")
    if r_pos >= 0:
        length, _ = _strtol(lc, r_pos + 1)
        if length <= 0 or length > 65535:
            ctx.issue("BadReadLength", pos + r_pos)
            return None, b"", b"", 0
        return "read", bytes(length), bytes(length), length
    idx = 0
    while idx < len(lc) and lc[idx].isspace():
        idx += 1
    if idx < len(lc) and lc[idx] == "p":
        idx += 1
        while idx < len(lc) and lc[idx].isdigit():
            idx += 1
        while idx < len(lc) and lc[idx].isspace():
            idx += 1
    if lc.startswith("0x", idx):
        digits = lc[idx + 2:]
        for i, c in enumerate(digits):
            if c not in HEX_DIGITS:
                ctx.issue("BadCheck", pos + idx + 2 + i)
                return None, b"", b"", 0
        if len(digits) % 2 != 0:
            ctx.issue("OddHex", pos + idx)
        value = _hex_to_bytes(digits)
        return "hex", b"\xff" * len(value), value, len(value)
    if lc.startswith("0b", idx):
        bits = lc[idx + 2:]
        for i, c in enumerate(bits):
            if c not in "01x":
                ctx.issue("BadCheck", pos + idx + 2 + i)
                return None, b"", b"", 0
        if len(bits) % 8 != 0:
            ctx.issue("BinaryNotWholeBytes", pos + idx)
        num_bytes = (len(bits) + 7) // 8
        mask = bytearray(b"\xff" * num_bytes)
        value = bytearray(num_bytes)
        for i, c in enumerate(bits):
            bit = 0x80 >> (i % 8)
            if c == "x":
                mask[i // 8] &= ~bit & 0xFF
            elif c == "1":
                value[i // 8] |= bit
        return "binary", bytes(mask), bytes(value), num_bytes
    i = 0
    while i < len(lc):
        if lc[i] == "p":
            while i + 1 < len(lc) and lc[i + 1].isdigit():
                i += 1
            i += 1
            continue
        if not lc[i].isspace():
            ctx.issue("BadCheck", pos + i)
            return None, b"", b"", 0
        i += 1
    return "pause", b"", b"", 0


def _parse_detection_value(value, value_pos, step, ctx):
    lc = value.lower()
    suffix = ":crc8w"
    if lc.endswith(suffix):
        step.crc8w = True
        lc = lc[:-len(suffix)]
    if not _set_pause(step, _first_pause_ms(lc), ctx, value_pos):
        return False
    start = 0
    while start < len(lc):
        comma = lc.find(",", start)
        if comma < 0:
            comma = len(lc)
        kind, mask, val, _ = _parse_check_value(lc[start:comma], ctx, value_pos + start)
        if kind is None:
            return False
        if step.checks and len(val) != len(step.checks[0][1]):
            ctx.issue("MixedCheckLengths", value_pos + start)
        step.checks.append((mask, val))
        start = comma + 1
    step.read = len(step.checks[0][1]) if step.checks else 0
    if step.crc8w and (step.read == 0 or step.read % 3 != 0):
        ctx.issue("CrcLength", value_pos)
        return False
    return True


def _parse_init_value(value, value_pos, step, ctx):
    lc = value.lower()
    if not _set_pause(step, _first_pause_ms(lc), ctx, value_pos):
        return False
    if lc == "":
        return True
    if lc[0] == "r":
        length, _ = _strtol(lc, 1)
        step.read = max(length, 0) & 0xFFFF
    elif lc.startswith("0b"):
        step.read = (len(lc) - 2 + 7) // 8
        ctx.issue("InitBinaryIsReadCount", value_pos)
        return True
    elif lc.startswith("0x"):
        ctx.issue("InitValueNotWritten", value_pos)
        return True
    i = 0
    valid = True
    if i < len(lc) and lc[i] == "r":
        i += 1
        digits_start = i
        while i < len(lc) and lc[i].isdigit():
            i += 1
        valid = i > digits_start
    if valid and i < len(lc):
        valid = _is_pause_token(lc[i:])
    if not valid:
        ctx.issue("InitValueIgnored", value_pos)
    return True


def _parse_poll_value(value, value_pos, step, ctx):
    dyn_start = value.find("r{")
    if dyn_start >= 0:
        dyn_end = value.find("}", dyn_start + 2)
        if dyn_end < 0:
            ctx.issue("BadReadExpr", value_pos + dyn_start)
            return False
        max_value = _read_len_expr_max(value[dyn_start + 2:dyn_end])
        if max_value is None:
            ctx.issue("BadReadExpr", value_pos + dyn_start)
            return False
        step.expr = True
        step.read = max_value & 0xFFFF
        return _set_pause(step, _first_pause_ms(value.lower()), ctx, value_pos)
    lc = value.lower()
    issues_before = len(ctx.result.issues)
    kind, _, _, read_len = _parse_check_value(lc, ctx, value_pos)
    if kind is None:
        for i in range(issues_before, len(ctx.result.issues)):
            if ctx.result.issues[i][0] == "BadCheck":
                ctx.result.issues[i] = ("BadValue", ctx.result.issues[i][1])
        return False
    step.read = read_len & 0xFFFF
    return _set_pause(step, _first_pause_ms(lc), ctx, value_pos)


def compile(command_str, field):
    """Compile a command string. field: "detection", "init" or "poll". Returns a Result"""
    result = Result()
    s = command_str or ""
    start_pos = 0
    detection_invalid = False
    while start_pos < len(s):
        m = re.compile(r"[&;]").search(s, start_pos)
        end_pos = m.start() if m else len(s)
        step_str = s[start_pos:end_pos]
        ctx = _Ctx(result, start_pos)
        eq_pos = step_str.find("=")
        name = step_str if eq_pos < 0 else step_str[:eq_pos]
        stripped = name.strip(" \t\r\n")
        name_pos = (len(name) - len(name.lstrip(" \t\r\n"))) if stripped else 0
        name = stripped
        step = Step()
        step_valid = True
        if eq_pos < 0 and _is_pause_token(name):
            ctx.issue("BarePause", name_pos)
            ms, _ = _strtol(name, 1)
            step_valid = _set_pause(step, ms, ctx, name_pos)
        else:
            write = _parse_write(name, _Ctx(result, start_pos + name_pos), field)
            if write is None:
                step_valid = False
            else:
                step.write, step.slots = write
                if eq_pos >= 0:
                    step.has_value = True
                    value = step_str[eq_pos + 1:]
                    value_pos = eq_pos + 1
                    if "=" in value:
                        ctx.issue("MultipleEquals", value_pos + value.find("="))
                        step_valid = False
                    elif field == "detection":
                        step_valid = _parse_detection_value(value, value_pos, step, ctx)
                    elif field == "init":
                        if "r{" in value:
                            ctx.issue("ReadExprNotAllowed", value_pos + value.find("r{"))
                            step_valid = False
                        else:
                            step_valid = _parse_init_value(value, value_pos, step, ctx)
                    else:
                        step_valid = _parse_poll_value(value, value_pos, step, ctx)
        if step_valid:
            result.steps.append(step)
        else:
            detection_invalid = True
        start_pos = end_pos + 1
    if field == "detection" and detection_invalid:
        result.steps = []
    return result


def detection_strength(result):
    """Strength of a compiled detection string: the number of bits it compares (for each step, the fewest bits any of
    its alternatives compares, plus 8 for each CRC byte checked by :crc8w); -1 if it has errors (it never matches).
    The same as DeviceTypeGrammar::detectionStrength"""
    if result.has_errors():
        return -1
    strength = 0
    for step in result.steps:
        bits = [sum(bin(b).count("1") for b in mask) for mask, _ in step.checks]
        if bits and min(bits) > 0:
            strength += min(bits)
        if step.crc8w:
            strength += 8 * (step.read // 3)
    return strength


def crc8(data):
    """Sensirion CRC-8 (poly 0x31, init 0xff)"""
    crc = 0xFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x31) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def check_record(dev_type):
    """Compile a record's three command strings. Returns a list of (field, Result)"""
    return [("detection", compile(dev_type.get("detectionValues", ""), "detection")),
            ("init", compile(dev_type.get("initValues", ""), "init")),
            ("poll", compile(dev_type.get("pollInfo", {}).get("c", ""), "poll"))]
