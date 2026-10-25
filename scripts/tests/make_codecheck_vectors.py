#!/usr/bin/env python3
"""Test vectors for the decode code check, shared with raftjs (RaftDeviceTypeCodeCheck.test.ts)

Each vector is a record's decode code (resp.c) or init code (initCode) with its attribute names, and the problems
PseudocodeHandler.check_code finds in it (check_decode_code for whole records). The code a record from another
source may use is the decode language only, since it is compiled into the firmware or run by its interpreter.

Usage: make_codecheck_vectors.py [--check]   (writes codecheck_vectors.json next to this script, with RaftCore's
records' code as it is now; --check: exit 1 if check_code no longer gives the stored problems). After a change, copy
the file to raftjs testdata/.
"""

import json
import pathlib
import sys
from collections import OrderedDict

TESTS_DIR = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(TESTS_DIR.parent))
import AssembleDeviceTypes as A  # noqa: E402
from PseudocodeHandler import PseudocodeHandler  # noqa: E402

VECTORS_PATH = TESTS_DIR / "codecheck_vectors.json"

CASES = [
    ("decode language", "int k=0;int n=(buf[1]<<8)|buf[0];while(k<n){out.v=buf[k+2];k++;next;}", ["v"], "decode"),
    ("casts, if and else", "out.v = buf[0] + (((uint16_t)buf[1])<<8); if(out.v>3){out.v=3;} else {out.v=0;}", ["v"], "decode"),
    ("float variable", "float f=1.5; out.v=f*buf[0];", ["v"], "decode"),
    ("hex, logic and return", "int x=0x1F&buf[0]; if(!x||x>=4&&x!=7){return;} out.v=~x^2%3;", ["v"], "decode"),
    ("function call", "out.v=buf[0];system(1);", ["v"], "decode"),
    ("call of a variable", "int f=0;out.v=f(1);", ["v"], "decode"),
    ("undeclared name", "out.v=secret;", ["v"], "decode"),
    ("strings and other characters", 'out.v=buf[0]; "x" # $', ["v"], "decode"),
    ("quote characters", "out.v=1; 'a' \"b\" \\ ?", ["v"], "decode"),
    ("other characters at the end", "out.v=1;@", ["v"], "decode"),
    ("non-ASCII and control characters", "out.v=1; ° \x01", ["v"], "decode"),
    ("a dotted name", "esp.restart=1;", ["v"], "decode"),
    ("not an attribute", "out.other=1;", ["v"], "decode"),
    ("init code", "init.rate = buf[0] & 3; int g = 2; init.gain = g;", [], "init"),
    ("init code assigning out", "out.v = 1;", [], "init"),
    ("init code with a dotted name", "init.a.b = 1; x.y = 2;", [], "init"),
    ("init code calling", "init.a = read(1);", [], "init"),
    ("empty", "", [], "decode"),
]


def build(vec):
    vec = OrderedDict((k, v) for k, v in vec.items() if k != "problems")
    vec["problems"] = PseudocodeHandler().check_code(vec["code"], vec["attrs"], code_kind=vec["kind"])
    return vec


def vectors():
    out = [build(OrderedDict([("name", n), ("code", c), ("attrs", a), ("kind", k)])) for n, c, a, k in CASES]
    # RaftCore's records' code: no problems
    records, _ = A.load_catalogue(str(TESTS_DIR.parents[1] / "devtypes" / "records"))
    for key, record in records.items():
        resp = record.get("devInfoJson", {}).get("resp", {})
        custom = resp.get("c")
        if isinstance(custom, dict) and custom.get("c"):
            out.append(build(OrderedDict([("name", f"{key} decode code"), ("code", custom["c"]),
                                          ("attrs", [a.get("n", "") for a in resp.get("a", [])]), ("kind", "decode")])))
        init_code = record.get("initCode")
        init_code = init_code.get("c", "") if isinstance(init_code, dict) else (init_code or "")
        if init_code:
            out.append(build(OrderedDict([("name", f"{key} init code"), ("code", init_code), ("attrs", []), ("kind", "init")])))
    return out


def vectors_text(vecs):
    return json.dumps(OrderedDict([("_doc", "Decode code check vectors - see make_codecheck_vectors.py"),
                                   ("vectors", vecs)]), indent=1, ensure_ascii=False) + "\n"


def main(argv):
    old = VECTORS_PATH.read_text(encoding="utf-8") if VECTORS_PATH.exists() else None
    if "--check" in argv:
        stored = json.loads(old, object_pairs_hook=OrderedDict)["vectors"] if old else []
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
