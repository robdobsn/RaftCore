#!/usr/bin/env python3
"""Test vectors for resolving a DeviceTypeSources.json selection, shared with raftjs (RaftDeviceTypeSources.test.ts)

Each vector gives the selection files (Common's first, then the SysType's), the records each source holds (as
catalogue JSON text) and the section to resolve, and what AssembleDeviceTypes.assemble makes of them: the records
chosen (with the source each came from, and policies applied), the records local copies replaced, the policies for
compiled types (a type file's), the messages, the warnings (records with a _warning) and the provenance (a type file's _sources) - or the error. Sources are
RaftCore's ("bundled") and folders ("path"); git sources choose as bundled ones do.

Usage: make_selection_vectors.py [--check]   (writes selection_vectors.json next to this script; --check: exit 1 if
assemble no longer gives the stored outputs). After a change, copy the file to raftjs testdata/.
"""

import json
import pathlib
import shutil
import sys
import tempfile
from collections import OrderedDict

TESTS_DIR = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(TESTS_DIR.parent))
import AssembleDeviceTypes as A  # noqa: E402

VECTORS_PATH = TESTS_DIR / "selection_vectors.json"
RAFTCORE_VERSION = "1.62.0"


def record(addresses="0x10", name="Test"):
    return OrderedDict([("addresses", addresses), ("detectionValues", "0x00=0x12"),
                        ("devInfoJson", OrderedDict([("name", name)]))])


def cat(**records):
    return json.dumps({"devTypes": records})


def _load(text):
    return json.loads(text, object_pairs_hook=OrderedDict)


def build(vec):
    """A vector's outputs from its inputs: the selection files and source records are written to a folder (a path
    source's folder is named after the source) and assembled"""
    vec = OrderedDict((k, v) for k, v in vec.items() if k in ("name", "files", "sources", "section"))
    tmp = pathlib.Path(tempfile.mkdtemp())
    try:
        raftcore = tmp / "RaftCore"
        (raftcore / "devtypes" / "records").mkdir(parents=True)
        (raftcore / "library.json").write_text(json.dumps({"version": RAFTCORE_VERSION}))
        paths = []
        for label, content in vec["files"].items():
            path = tmp / "project" / label
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
            paths.append(str(path))
        for name, source in vec["sources"].items():
            folder = raftcore / "devtypes" / "records" if source["kind"] == "bundled" else tmp / "project" / name
            folder.mkdir(parents=True, exist_ok=True)
            for set_name, keys in source.get("sets", {}).items():
                (raftcore / "devtypes" / "sets").mkdir(parents=True, exist_ok=True)
                (raftcore / "devtypes" / "sets" / f"{set_name}.json").write_text(json.dumps({"records": keys}))
            for key, rec in _load(source["records"])["devTypes"].items():
                (folder / f"{key}.json").write_text(json.dumps(rec, indent=4))
        try:
            result = A.assemble(paths, str(raftcore), vec["section"])
            vec["records"] = OrderedDict((k, OrderedDict([("source", result.origin[k][0]),
                                                          ("record", json.dumps(r, separators=(",", ":")))]))
                                         for k, r in result.records.items())
            vec["replaced"] = OrderedDict((k, [n for n, _ in v]) for k, v in result.replaced.items())
            vec["policyOnly"] = OrderedDict(result.policy_only)
            vec["messages"] = [m.replace(str(tmp / "project") + "/", "") for m in result.messages]
            vec["warnings"] = list(result.warnings)
            vec["provenance"] = json.dumps(result.provenance, separators=(",", ":"))
        except A.AssembleError as err:
            vec["error"] = str(err).replace(str(tmp / "project") + "/", "")
        return vec
    finally:
        shutil.rmtree(tmp)


def vectors():
    out = []
    raftcore = {"kind": "bundled", "records": cat(Alpha=record("0x11"), Bravo=record(name="Bravo core"),
                                                  Charlie=record("0x12"))}
    common = "systypes/Common/DeviceTypeSources.json"
    main = "systypes/Main/DeviceTypeSources.json"

    def add(name, files, sources, section="builtin"):
        out.append(build(OrderedDict([("name", name),
                                      ("files", OrderedDict((k, json.dumps(v)) for k, v in files.items())),
                                      ("sources", sources), ("section", section)])))

    add("include defaults to everything", {common: {"sources": {"raftcore": {"bundled": True}}}}, {"raftcore": raftcore})
    add("include forms and exclude", {common: {"sources": {"raftcore": {"bundled": True}, "local": {"path": "../../local"}},
                                               "builtin": {"include": ["raftcore:Alpha", "local:*", "Charlie"],
                                                           "exclude": ["local:Delta"]}}},
        {"raftcore": raftcore, "local": {"kind": "path", "records": cat(Delta=record("0x13"), Echo=record("0x14"))}})
    add("exclude a wildcard and a source", {common: {"sources": {"raftcore": {"bundled": True}},
                                                     "builtin": {"include": ["*"], "exclude": ["raftcore:Bravo", "Charlie"]}}},
        {"raftcore": raftcore})
    add("a local copy replaces", {common: {"sources": {"raftcore": {"bundled": True}, "local": {"path": "../../local"}}}},
        {"raftcore": raftcore, "local": {"kind": "path", "records": cat(Bravo=record(name="Bravo local"))}})
    add("the later path source wins", {common: {"sources": {"raftcore": {"bundled": True}, "a": {"path": "../../a"},
                                                            "b": {"path": "../../b"}}}},
        {"raftcore": raftcore, "a": {"kind": "path", "records": cat(Bravo=record(name="A"))},
         "b": {"kind": "path", "records": cat(Bravo=record(name="B"))}})
    add("a named source wins over a local copy", {common: {"sources": {"raftcore": {"bundled": True}, "local": {"path": "../../local"}},
                                                           "builtin": {"include": ["raftcore:Bravo", "local:*"]}}},
        {"raftcore": raftcore, "local": {"kind": "path", "records": cat(Bravo=record(name="Bravo local"))}})
    two_bundled = {"sources": {"raftcore": {"bundled": True}, "other": {"bundled": True}}}
    add("error: the same key from two other sources", {common: two_bundled}, {"raftcore": raftcore, "other": raftcore})
    add("the same key from two other sources, one named", {common: dict(two_bundled, builtin={"include": ["other:Alpha"]})},
        {"raftcore": raftcore, "other": raftcore})
    add("error: named from two sources", {common: dict(two_bundled, builtin={"include": ["other:Alpha", "raftcore:Alpha"]})},
        {"raftcore": raftcore, "other": raftcore})
    add("error: no such record", {common: {"sources": {"raftcore": {"bundled": True}}, "builtin": {"include": ["Zulu"]}}},
        {"raftcore": raftcore})
    add("error: no such record in a source", {common: {"sources": {"raftcore": {"bundled": True}},
                                                        "builtin": {"include": ["raftcore:Zulu"]}}}, {"raftcore": raftcore})
    add("error: no such source", {common: {"sources": {"raftcore": {"bundled": True}}, "builtin": {"include": ["acme:*"]}}},
        {"raftcore": raftcore})
    add("policies applied", {common: {"sources": {"raftcore": {"bundled": True}},
                                      "builtin": {"policy": {"Alpha": "disabled", "Bravo": "disabledClaim", "Charlie": "enabled"}}}},
        {"raftcore": {"kind": "bundled", "records": cat(Alpha=record("0x11"), Bravo=record(),
                                                       Charlie=dict(record("0x12"), disable=True))}})
    add("error: policy for a record not selected", {common: {"sources": {"raftcore": {"bundled": True}},
                                                             "builtin": {"include": ["Alpha"], "policy": {"Bravo": "disabled"}}}},
        {"raftcore": raftcore})
    add("error: unknown policy", {common: {"sources": {"raftcore": {"bundled": True}}, "builtin": {"policy": {"Alpha": "off"}}}},
        {"raftcore": raftcore})
    add("error: unknown key", {common: {"sources": {"raftcore": {"bundled": True}}, "typefile": {}}}, {"raftcore": raftcore})
    add("error: bad source name", {common: {"sources": {"a:b": {"bundled": True}}}}, {})
    add("error: include not a list of strings", {common: {"sources": {"raftcore": {"bundled": True}}, "builtin": {"include": [1]}}},
        {"raftcore": raftcore})
    add("error: fsImage outside typeFile", {common: {"sources": {"raftcore": {"bundled": True}}, "builtin": {"fsImage": True}}},
        {"raftcore": raftcore})
    add("include as a string", {common: {"sources": {"raftcore": {"bundled": True}}, "builtin": {"include": "Alpha"}}},
        {"raftcore": raftcore})
    add("the older name builtin: true", {common: {"sources": {"raftcore": {"builtin": True}}}}, {"raftcore": raftcore})
    add("a SysType's file adds to Common's",
        {common: {"sources": {"raftcore": {"bundled": True}, "local": {"path": "../../local"}},
                  "builtin": {"include": ["raftcore:Alpha"], "policy": {"Alpha": "disabled"}},
                  "typeFile": {"include": ["local:Delta"]}},
         main: {"sources": {"local": {"path": "../../local"}},
                "builtin": {"include": ["raftcore:Bravo"], "policy": {"Alpha": "enabled"}},
                "typeFile": {"policy": {"Charlie": "disabled"}}}},
        {"raftcore": raftcore, "local": {"kind": "path", "records": cat(Delta=record("0x13"))}})
    type_file = {common: {"sources": {"raftcore": {"bundled": True}, "local": {"path": "../../local"}},
                          "typeFile": {"include": ["local:*"], "policy": {"Charlie": "disabledClaim", "Delta": "disabled"}}}}
    local = {"kind": "path", "records": cat(Delta=record("0x13"), Echo=record("0x14"))}
    add("a type file: its records and policies for compiled types", type_file, {"raftcore": raftcore, "local": local},
        "typeFile")
    add("a type file includes nothing unless it says so", {common: {"sources": {"raftcore": {"bundled": True}}}},
        {"raftcore": raftcore}, "typeFile")
    add("a type file's provenance: only the sources it uses", {common: {"sources": {"raftcore": {"bundled": True},
                                                                                   "local": {"path": "../../local"}},
                                                                       "typeFile": {"include": ["raftcore:Alpha"],
                                                                                    "exclude": ["local:*"]}}},
        {"raftcore": raftcore, "local": local}, "typeFile")
    sets = dict(raftcore, sets={"small": ["Alpha", "Charlie"], "one": ["Charlie"], "stale": ["Alpha", "Zulu"]})
    add("a named set", {common: {"sources": {"raftcore": {"bundled": True}}, "builtin": {"include": ["raftcore:@small"]}}},
        {"raftcore": sets})
    add("excluding a named set", {common: {"sources": {"raftcore": {"bundled": True}},
                                           "builtin": {"include": ["raftcore:*"], "exclude": ["raftcore:@one"]}}},
        {"raftcore": sets})
    add("a named set in a type file, with a local copy replacing one record",
        {common: {"sources": {"raftcore": {"bundled": True}, "local": {"path": "../../local"}},
                  "typeFile": {"include": ["raftcore:@small", "local:*"]}}},
        {"raftcore": sets, "local": {"kind": "path", "records": cat(Alpha=record("0x11", name="Alpha local"))}}, "typeFile")
    add("error: no such set", {common: {"sources": {"raftcore": {"bundled": True}}, "builtin": {"include": ["raftcore:@nope"]}}},
        {"raftcore": sets})
    add("error: a set naming a record the source hasn't", {common: {"sources": {"raftcore": {"bundled": True}},
                                                                    "builtin": {"include": ["raftcore:@stale"]}}},
        {"raftcore": sets})
    add("error: a set without a source", {common: {"sources": {"raftcore": {"bundled": True}}, "builtin": {"include": ["@small"]}}},
        {"raftcore": sets})
    add("error: a set from a path source", {common: {"sources": {"raftcore": {"bundled": True}, "local": {"path": "../../local"}},
                                                     "builtin": {"include": ["local:@small"]}}},
        {"raftcore": sets, "local": local})
    # Records with a _warning: a wildcard leaves them out unless acknowledged; named, they're reported unless
    # acknowledged or disabled
    warned = {"kind": "bundled", "records": cat(Alpha=record("0x11"),
                                                Wake=OrderedDict(list(record("0x0c").items()) + [("_warning", "Sends a general call")]))}
    add("warning: a wildcard leaves the record out", {common: {"sources": {"raftcore": {"bundled": True}}}}, {"raftcore": warned})
    add("warning: named, the record is selected and reported", {common: {"sources": {"raftcore": {"bundled": True}},
                                                                         "builtin": {"include": ["*", "raftcore:Wake"]}}},
        {"raftcore": warned})
    add("warning: acknowledged, the wildcard selects it", {common: {"sources": {"raftcore": {"bundled": True}}},
                                                           main: {"acknowledge": ["Wake"]}}, {"raftcore": warned})
    add("warning: disabled, not reported", {common: {"sources": {"raftcore": {"bundled": True}},
                                                     "builtin": {"include": ["Wake"], "policy": {"Wake": "disabled"}}}},
        {"raftcore": warned})
    add("warning: in a type file", {common: {"sources": {"raftcore": {"bundled": True}}, "typeFile": {"include": ["Wake"]}}},
        {"raftcore": warned}, "typeFile")
    add("error: a warning that isn't a string", {common: {"sources": {"raftcore": {"bundled": True}}}},
        {"raftcore": {"kind": "bundled", "records": cat(Wake=OrderedDict(list(record().items()) + [("_warning", "")]))}})
    add("error: acknowledge not a list of keys", {common: {"sources": {"raftcore": {"bundled": True}}, "acknowledge": [3]}},
        {"raftcore": raftcore})
    return out


def vectors_text(vecs):
    return json.dumps(OrderedDict([("_doc", "DeviceTypeSources.json selection vectors - see make_selection_vectors.py"),
                                   ("raftCore", RAFTCORE_VERSION), ("vectors", vecs)]), indent=1) + "\n"


def main(argv):
    old = VECTORS_PATH.read_text(encoding="utf-8") if VECTORS_PATH.exists() else None
    if "--check" in argv:
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
