#!/usr/bin/env python3
"""Test vectors for building device type files, shared with raftjs (RaftDeviceTypeFile.test.ts)

Each vector gives the records selected for a type file, the compiled records and the type file's provenance, as
JSON text (so the records' numbers, key order and characters are as written), and what
AssembleDeviceTypes.build_type_file makes of them: the file's text and entries, or the start of its error. raftjs's
TypeScript builder must give the same text (the device's CRC of the file then agrees) and entries.

Usage: make_typefile_vectors.py [--check]   (writes typefile_vectors.json next to this script, with RaftCore's records
as they are now; --check: exit 1 if build_type_file no longer gives the stored outputs for the stored inputs). After changing build_type_file or the vectors, run it and copy the file to raftjs testdata/.
"""

import json
import pathlib
import sys
import types
from collections import OrderedDict

TESTS_DIR = pathlib.Path(__file__).resolve().parent
SCRIPTS_DIR = TESTS_DIR.parent
sys.path.insert(0, str(SCRIPTS_DIR))
import AssembleDeviceTypes as A  # noqa: E402

VECTORS_PATH = TESTS_DIR / "typefile_vectors.json"
RECORDS_DIR = SCRIPTS_DIR.parent / "devtypes" / "records"


def _load(text):
    return json.loads(text, object_pairs_hook=OrderedDict)


def _catalogue(records):
    return json.dumps({"devTypes": records}, indent=1, ensure_ascii=False)


def _raftcore_records():
    """RaftCore's records as written (in key order), key -> record text"""
    out = OrderedDict()
    for path in sorted(RECORDS_DIR.glob("*.json"), key=lambda p: p.stem.encode("utf-8")):
        out[path.stem] = path.read_text(encoding="utf-8")
    return out


def _catalogue_text(record_texts):
    return "{\"devTypes\":{" + ",".join(json.dumps(k) + ":" + t for k, t in record_texts.items()) + "}}"


def record(addresses="0x10", name="Test", **extra):
    rec = OrderedDict([("addresses", addresses), ("detectionValues", "0x00=0x12"), ("initValues", ""),
                       ("pollInfo", OrderedDict([("c", "0x00=r1"), ("i", 100), ("s", 1)])),
                       ("devInfoJson", OrderedDict([("name", name), ("resp", OrderedDict(
                           [("b", 1), ("a", [OrderedDict([("n", "v"), ("t", "B"), ("o", "uint8")])])]))]))])
    rec.update(extra)
    return rec


PROVENANCE = OrderedDict([("formatVersion", 1),
                          ("sources", OrderedDict([("raftcore", OrderedDict([("bundled", True), ("raftCore", "1.62.0")])),
                                                   ("acme", OrderedDict([("git", "https://github.com/acme/devtypes.git"),
                                                                         ("ref", "v1.0.0"),
                                                                         ("commit", "3f2a9c1e7b0d" + "0" * 28)]))])),
                          ("typeFile", OrderedDict([("include", ["raftcore:*", "acme:Gadget"]),
                                                    ("policy", OrderedDict([("TrillRing", "disabled")]))]))])

# Numbers, characters and keys that JavaScript's JSON.parse and JSON.stringify treat differently from Python's json
LEXEMES_RECORD = r'''{
    "addresses": "0x48",
    "detectionValues": "0x00=0x12",
    "pollInfo": {"c": "0x00=r2", "i": 1E3, "s": 1},
    "devInfoJson": {
        "name": "Lexemes °C ✓ 😀 \u007f\t/ café °",
        "resp": {"b": 2, "a": [{"n": "v", "t": ">H", "d": 16.0, "a": -0.0, "m": 0.000000125, "x": 1.50,
                                "big": 36893488147419103232.0, "e": 2.5e-5, "s": 1e16, "f": 123456789012345.0,
                                "g": 1234567890123456.0, "h": 12345678901234567.0, "neg": -0, "huge": 12345678901234567890123,
                                "small": 0.0001, "smaller": 0.00001, "r": [-40, 125.5]}]},
        "actions": [{"n": "rate", "map": {"12.5": 1, "26": 2, "104": 3, "0": 4, "-1": 5, "01": 6}}],
        "dup": {"a": 1, "b": 2, "a": 3}
    }
}'''


def vectors():
    raftcore = _raftcore_records()
    out = []

    def add(name, records_text, compiled_text="{\"devTypes\":{}}", policy_only=None, provenance=None,
            max_bytes=A.TYPE_FILE_MAX_BYTES_DEFAULT):
        out.append(build(OrderedDict([("name", name), ("records", records_text), ("compiled", compiled_text),
                                      ("policyOnly", OrderedDict(policy_only or {})),
                                      ("provenance", json.dumps(provenance) if provenance else None),
                                      ("maxBytes", max_bytes)])))

    all_text = _catalogue_text(raftcore)
    add("every RaftCore record, new", all_text, provenance=PROVENANCE, max_bytes=1000000)
    add("every RaftCore record, all compiled: unchanged", all_text, all_text)

    # Overrides, decoder kinds, policies, unchanged with _notes and deviceType
    compiled = OrderedDict((k, _load(raftcore[k])) for k in ("LSM6DS3", "MCP9808", "TrillRing", "VL53L0X", "AHT20"))
    lsm = _load(raftcore["LSM6DS3"])
    lsm["pollInfo"]["i"] = 20000
    mcp = _load(raftcore["MCP9808"])
    mcp["devInfoJson"]["resp"]["a"][0]["n"] = "tempC"
    aht = _load(raftcore["AHT20"])
    aht["_notes"] = "a note"
    aht["deviceType"] = "AHT20"
    code = record("0x31", name="Code")
    code["devInfoJson"]["resp"]["c"] = OrderedDict([("n", "f"), ("c", "out.v=buf[0];")])
    selected = OrderedDict([("AHT20", aht), ("Code", code), ("LSM6DS3", lsm), ("MCP9808", mcp),
                            ("Std", record("0x30", name="Std", _notes="dropped")),
                            ("NoData", OrderedDict([("addresses", "0x32"), ("detectionValues", "0x00=0x01")]))])
    add("overrides, decoder kinds and policies", _catalogue(selected), _catalogue(compiled),
        policy_only={"VL53L0X": "disabled", "TrillRing": "disabledClaim"}, provenance=PROVENANCE)

    add("numbers, characters and key order as Python writes them", "{\"devTypes\":{\"Lexemes\":" + LEXEMES_RECORD + "}}",
        provenance=OrderedDict([("formatVersion", 1), ("note", "café ° 😀"), ("n", 1.0)]))

    # Keys sorted by UTF-8 bytes
    keyed = OrderedDict((k, record("0x40", name=k)) for k in ("b", "B", "a_1", "a", "é", "Z9", "\U0001f600", "￮"))
    add("keys in UTF-8 byte order", _catalogue(keyed))

    # Init values and init code
    slot_compiled = record("0x50", name="Slot", initValues="0x10{rate}", initCode=OrderedDict([("c", "init.rate = 4;")]))
    add("init value slots: own initCode", _catalogue({"Own": record("0x51", initValues="0x10{rate}&{?on}0x11=",
                                                                      initCode="init.rate = 3; init.on = 1;")}))
    add("init value slots: the compiled record's initCode", _catalogue({"Slot": record("0x50", initValues="0x10{rate}")}),
        _catalogue({"Slot": slot_compiled}))
    add("init code: own code and compiled code", _catalogue({"Slot": record("0x50", initValues="0x10{rate}",
                                                                              initCode={"c": "init.rate = 5;"})}),
        _catalogue({"Slot": slot_compiled}))
    add("init code: own code computing more", _catalogue({"Slot": record("0x50", initValues="0x10{rate}&{?on}0x11=",
                                                                           initCode="init.rate = 5; init.on = 1;")}),
        _catalogue({"Slot": slot_compiled}))

    # Errors
    add("error: over the size limit", _catalogue({"Std": record()}), max_bytes=100)
    add("error: a type file can't enable a compiled type", "{\"devTypes\":{}}", _catalogue({"Std": record()}),
        policy_only={"Std": "enabled"})
    add("error: policy for a type that isn't compiled", "{\"devTypes\":{}}", policy_only={"Zulu": "disabled"})
    add("error: slots without init code", _catalogue({"Slot": record("0x50", initValues="0x10{rate}")}))
    add("error: initCode doesn't compute a slot", _catalogue({"Slot": record("0x50", initValues="0x10{rate}{gain}",
                                                                              initCode="init.rate = 1;")}))
    add("error: compiled initCode doesn't compute a slot", _catalogue({"Slot": record("0x50", initValues="0x10{gain}")}),
        _catalogue({"Slot": slot_compiled}))
    return out


def build(vec):
    """A vector's outputs (text, crc and entries, or error) from its inputs"""
    vec = OrderedDict((k, v) for k, v in vec.items() if k not in ("text", "crc", "entries", "error"))
    records = _load(vec["records"])["devTypes"]
    compiled = _load(vec["compiled"])["devTypes"]
    provenance = _load(vec["provenance"]) if vec["provenance"] else False
    result = types.SimpleNamespace(records=records, policy_only=OrderedDict(vec["policyOnly"]), provenance=provenance)
    try:
        text, entries = A.build_type_file(result, compiled, max_bytes=vec["maxBytes"], provenance=provenance)
        vec["text"] = text
        vec["crc"] = "%04x" % crc16(text.encode("utf-8"))
        vec["entries"] = entries
    except A.AssembleError as err:
        vec["error"] = str(err)
    return vec


def crc16(data):
    """CRC16-CCITT as the firmware's devman/typefile reports it (as DeviceTypesTool.crc16)"""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def vectors_text(vecs):
    return json.dumps(OrderedDict([("_doc", "Device type file build vectors - see make_typefile_vectors.py"),
                                   ("vectors", vecs)]), indent=1, ensure_ascii=False) + "\n"


def main(argv):
    old = VECTORS_PATH.read_text(encoding="utf-8") if VECTORS_PATH.exists() else None
    if "--check" in argv:
        # The stored outputs are what build_type_file gives for the stored inputs (the inputs, RaftCore's records
        # among them, are as they were when the vectors were written)
        stored = _load(old)["vectors"] if old else []
        if not stored or vectors_text([build(v) for v in stored]) != old:
            print(f"{VECTORS_PATH.name} is out of date - run {pathlib.Path(__file__).name}")
            return 1
        return 0
    text = vectors_text(vectors())
    if old != text:
        VECTORS_PATH.write_text(text, encoding="utf-8")
        print(f"wrote {VECTORS_PATH}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
