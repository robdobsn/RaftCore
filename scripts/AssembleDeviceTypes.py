#!/usr/bin/env python3
"""Assemble the device type records for a build from DeviceTypeSources.json

Device type records are kept one per file (records/<Key>.json, the record body as it appears under "devTypes"), and
a project selects the records it needs in DeviceTypeSources.json (systypes/Common/ and systypes/<SysType>/). This
script resolves the selection into one catalogue file that ProcessDevTypeJsonToC.py reads - see
devdocs/device-type-record-management-plan.md.

DeviceTypeSources.json:
    {
      "sources":  { "raftcore": { "builtin": true }, "local": { "path": "devtypes" } },
      "builtin":  { "include": ["raftcore:LSM6DS", "local:*"], "exclude": [], "policy": { "TrillRing": "disabled" } },
      "typeFile": { "include": [], "policy": {} }
    }
- sources: "builtin" (RaftCore's records), "path" (a folder of records, one record file or a catalogue file, relative
  to the DeviceTypeSources.json that names it). "git" sources come in a later version
- include/exclude: "*", "<source>:*", "<source>:<key>" or "<key>" (a key only one source has). include defaults to
  "*". A key from a path source replaces the same key from another source (the later path source wins); the same key
  from two other sources is an error unless include names the source
- policy: "enabled", "disabled" or "disabledClaim" for a selected key
- A SysType's file adds to Common's: sources are added (or replaced by name), include and exclude lists are joined,
  policies merged (the SysType's win)

Usage:
    AssembleDeviceTypes.py --raftcore <dir> --select <file>[,<file>] [--section builtin|typeFile]
                           --out <catalogue.json> [--manifest <manifest.json>] [--list-inputs]
    AssembleDeviceTypes.py --raftcore <dir> --all --out <catalogue.json>     (every RaftCore record, one file)
    AssembleDeviceTypes.py --split <catalogue.json> --out-dir <records dir>   (one file per record, as written)

--list-inputs prints the files and folders read, one per line ("file:<path>" or "dir:<path>"), for the build's
dependencies.
"""

import argparse
import hashlib
import json
import os
import sys
from collections import OrderedDict

RECORDS_SUBDIR = os.path.join("devtypes", "records")
LEGACY_CATALOGUE = os.path.join("devtypes", "DeviceTypeRecords.json")
POLICY_VALUES = {"enabled": None, "disabled": True, "disabledClaim": "claim"}
SECTIONS = ("builtin", "typeFile")


class AssembleError(Exception):
    pass


################################################################################################################
# Reading records
################################################################################################################

def _read_json(path):
    try:
        with open(path, "r", encoding="utf-8") as json_file:
            return json.load(json_file, object_pairs_hook=OrderedDict)
    except (OSError, ValueError) as err:
        raise AssembleError(f"{path}: {err}")


def _record_from_file(path, key):
    record = _read_json(path)
    if not isinstance(record, dict):
        raise AssembleError(f"{path}: a record file holds one record (a JSON object)")
    if "devTypes" in record:
        raise AssembleError(f"{path}: a catalogue file (with \"devTypes\") in a folder of records")
    if "deviceType" in record and record["deviceType"] != key:
        raise AssembleError(f"{path}: deviceType \"{record['deviceType']}\" differs from the file name \"{key}\"")
    return record


def load_catalogue(path, record_file=True):
    """Records from a catalogue file ({"devTypes": {...}}), a record file (<Key>.json) or a folder of record files
    (in key order). With record_file False a file without "devTypes" has no records (as the generator has always
    read files). Returns (OrderedDict key -> record, OrderedDict key -> file)"""
    records = OrderedDict()
    files = OrderedDict()
    if os.path.isdir(path):
        names = sorted(name for name in os.listdir(path) if name.endswith(".json"))
        for name in sorted(names, key=lambda n: n[:-5].encode("utf-8")):
            key = name[:-5]
            records[key] = _record_from_file(os.path.join(path, name), key)
            files[key] = os.path.join(path, name)
        return records, files
    content = _read_json(path)
    if isinstance(content, dict) and "devTypes" in content:
        for key, record in content["devTypes"].items():
            records[key] = record
            files[key] = path
        return records, files
    if not record_file:
        return records, files
    key = os.path.splitext(os.path.basename(path))[0]
    records[key] = _record_from_file(path, key)
    files[key] = path
    return records, files


def raftcore_records_path(raftcore_dir):
    """RaftCore's records: the records folder, or the single catalogue file of an older RaftCore"""
    records_dir = os.path.join(raftcore_dir, RECORDS_SUBDIR)
    if os.path.isdir(records_dir):
        return records_dir
    legacy = os.path.join(raftcore_dir, LEGACY_CATALOGUE)
    if os.path.isfile(legacy):
        return legacy
    raise AssembleError(f"no device type records in {raftcore_dir} ({RECORDS_SUBDIR} or {LEGACY_CATALOGUE})")


################################################################################################################
# Selection
################################################################################################################

def read_selection(paths):
    """Merge DeviceTypeSources.json files (Common first, then the SysType's)"""
    merged = {"sources": OrderedDict(), "sections": {s: {"include": [], "exclude": [], "policy": OrderedDict(),
                                                           "hasInclude": False, "fsImage": False} for s in SECTIONS}}
    for path in paths:
        content = _read_json(path)
        if not isinstance(content, dict):
            raise AssembleError(f"{path}: expected a JSON object")
        base_dir = os.path.dirname(os.path.abspath(path))
        for key in content:
            if key not in ("sources", "_notes") + SECTIONS:
                raise AssembleError(f"{path}: unknown key \"{key}\"")
        for name, spec in content.get("sources", {}).items():
            if not isinstance(spec, dict):
                raise AssembleError(f"{path}: source \"{name}\" must be an object")
            if ":" in name or name == "*":
                raise AssembleError(f"{path}: source name \"{name}\" can't contain ':' or be '*'")
            merged["sources"][name] = (spec, base_dir, path)
        for section in SECTIONS:
            spec = content.get(section)
            if spec is None:
                continue
            if not isinstance(spec, dict):
                raise AssembleError(f"{path}: \"{section}\" must be an object")
            target = merged["sections"][section]
            for list_name in ("include", "exclude"):
                value = spec.get(list_name, [])
                if isinstance(value, str):
                    value = [value]
                if not isinstance(value, list) or not all(isinstance(v, str) for v in value):
                    raise AssembleError(f"{path}: {section}.{list_name} must be a list of strings")
                target[list_name].extend(value)
            if "include" in spec:
                target["hasInclude"] = True
            for key, policy in spec.get("policy", {}).items():
                if policy not in POLICY_VALUES:
                    raise AssembleError(f"{path}: {section}.policy \"{key}\" must be one of {', '.join(POLICY_VALUES)}")
                target["policy"][key] = policy
            if "fsImage" in spec:
                target["fsImage"] = bool(spec["fsImage"])
    return merged


def _load_source(name, spec, base_dir, raftcore_dir):
    """(records, files, is_local, input paths)"""
    if spec.get("builtin"):
        path = raftcore_records_path(raftcore_dir)
        records, files = load_catalogue(path)
        return records, files, False, [path]
    if "path" in spec:
        path = spec["path"]
        if not os.path.isabs(path):
            path = os.path.normpath(os.path.join(base_dir, path))
        if not os.path.exists(path):
            raise AssembleError(f"source \"{name}\": {path} not found")
        records, files = load_catalogue(path)
        return records, files, True, [path]
    if "git" in spec:
        raise AssembleError(f"source \"{name}\": git sources are not supported yet (planned)")
    raise AssembleError(f"source \"{name}\": needs \"builtin\", \"path\" or \"git\"")


def _parse_pattern(pattern, source_names):
    if ":" in pattern:
        source, key = pattern.split(":", 1)
        if source not in source_names:
            raise AssembleError(f"\"{pattern}\": no source called \"{source}\"")
        return source, key
    return None, pattern


class Assembly:
    def __init__(self):
        self.records = OrderedDict()     # key -> record, sorted by key
        self.origin = OrderedDict()      # key -> (source name, file)
        self.messages = []
        self.inputs = []                 # files and folders read
        self.fs_image = False


def assemble(selection_paths, raftcore_dir, section="builtin"):
    """Resolve a selection into records (sorted by key)"""
    selection = read_selection(selection_paths)
    result = Assembly()
    result.inputs.extend(os.path.abspath(p) for p in selection_paths)
    sources = OrderedDict()
    for name, (spec, base_dir, _) in selection["sources"].items():
        records, files, is_local, inputs = _load_source(name, spec, base_dir, raftcore_dir)
        sources[name] = (records, files, is_local)
        result.inputs.extend(inputs)
    names = list(sources)
    chosen = selection["sections"][section]
    result.fs_image = chosen["fsImage"]
    includes = chosen["include"] if chosen["hasInclude"] else ["*"]

    # Candidates for each key: the sources it was selected from, and whether a source was named for it
    candidates = OrderedDict()
    for pattern in includes:
        source, key = _parse_pattern(pattern, names)
        matched = False
        for name in ([source] if source else names):
            records = sources[name][0]
            keys = list(records) if key == "*" else ([key] if key in records else [])
            for k in keys:
                entry = candidates.setdefault(k, {"named": OrderedDict(), "any": OrderedDict()})
                (entry["named"] if (source and key != "*") else entry["any"])[name] = True
                matched = True
        if not matched and key != "*":
            where = f"source \"{source}\"" if source else "any source"
            raise AssembleError(f"{section}.include \"{pattern}\": no record \"{key}\" in {where}")
    for pattern in chosen["exclude"]:
        source, key = _parse_pattern(pattern, names)
        for k in list(candidates):
            if key == "*" or k == key:
                for group in ("named", "any"):
                    for name in list(candidates[k][group]):
                        if source is None or name == source:
                            del candidates[k][group][name]
                if not candidates[k]["named"] and not candidates[k]["any"]:
                    del candidates[k]

    # One source for each key
    for key in sorted(candidates, key=lambda k: k.encode("utf-8")):
        named = list(candidates[key]["named"])
        if len(named) > 1:
            raise AssembleError(f"\"{key}\" is named from more than one source: {', '.join(named)}")
        if named:
            source = named[0]
        else:
            options = [n for n in names if n in candidates[key]["any"]]
            local = [n for n in options if sources[n][2]]
            if local:
                source = local[-1]
                replaced = [n for n in options if n != source]
                if replaced:
                    result.messages.append(f"\"{key}\" from {source} replaces the record from {', '.join(replaced)}")
            elif len(options) > 1:
                raise AssembleError(f"\"{key}\" is in sources {', '.join(options)} - name the source to use in "
                                    f"{section}.include (e.g. \"{options[0]}:{key}\")")
            else:
                source = options[0]
        records, files, _ = sources[source]
        result.records[key] = json.loads(json.dumps(records[key]), object_pairs_hook=OrderedDict)
        result.origin[key] = (source, files[key])

    # Policies
    for key, policy in chosen["policy"].items():
        if key not in result.records:
            raise AssembleError(f"{section}.policy \"{key}\": not a selected record")
        value = POLICY_VALUES[policy]
        if value is None:
            result.records[key].pop("disable", None)
        else:
            result.records[key]["disable"] = value
    return result


################################################################################################################
# Output
################################################################################################################

def _sha256(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def catalogue_text(records):
    return json.dumps({"devTypes": records}, indent=1) + "\n"


def manifest_text(result, section):
    records = OrderedDict()
    for key, (source, path) in result.origin.items():
        records[key] = {"source": source, "file": os.path.abspath(path), "sha256": _sha256(path)}
    return json.dumps({"formatVersion": 1, "section": section, "records": records}, indent=1) + "\n"


def write_if_changed(path, text):
    """Write a file only if its content changes (so the build doesn't regenerate needlessly)"""
    if os.path.isfile(path):
        with open(path, "r", encoding="utf-8") as f:
            if f.read() == text:
                return False
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)
    return True


################################################################################################################
# Splitting a catalogue file into record files
################################################################################################################

def split_catalogue_text(text):
    """Each record of a catalogue file as written (OrderedDict key -> record text, indented as a file of its own)"""
    decoder = json.JSONDecoder()
    start = text.index("\"devTypes\"")
    pos = text.index("{", start) + 1
    records = OrderedDict()
    while True:
        while text[pos] in " \t\r\n,":
            pos += 1
        if text[pos] == "}":
            break
        key, pos = decoder.raw_decode(text, pos)
        while text[pos] in " \t\r\n:":
            pos += 1
        value_start = pos
        _, pos = decoder.raw_decode(text, pos)
        value = text[value_start:pos]
        # Indentation of the record's closing brace, which is removed from every line after the first
        line_start = text.rfind("\n", 0, value_start) + 1
        indent = len(text[line_start:value_start]) - len(text[line_start:value_start].lstrip())
        lines = value.split("\n")
        lines = [lines[0]] + [line[indent:] if line[:indent].strip() == "" else line.lstrip() for line in lines[1:]]
        records[key] = "\n".join(lines) + "\n"
    return records


def split_catalogue(catalogue_path, out_dir):
    with open(catalogue_path, "r", encoding="utf-8") as f:
        text = f.read()
    parsed = json.loads(text, object_pairs_hook=OrderedDict)["devTypes"]
    os.makedirs(out_dir, exist_ok=True)
    for key, record_text in split_catalogue_text(text).items():
        if json.loads(record_text, object_pairs_hook=OrderedDict) != parsed[key]:
            raise AssembleError(f"{key}: the split record differs from the catalogue's")
        with open(os.path.join(out_dir, key + ".json"), "w", encoding="utf-8") as f:
            f.write(record_text)
    return list(parsed)


################################################################################################################
# Command line
################################################################################################################

def main(argv=None):
    parser = argparse.ArgumentParser(description="Assemble device type records (DeviceTypeSources.json)")
    parser.add_argument("--raftcore", help="RaftCore folder")
    parser.add_argument("--select", help="DeviceTypeSources.json files, comma separated (Common first)")
    parser.add_argument("--section", default="builtin", choices=SECTIONS)
    parser.add_argument("--all", action="store_true", help="every RaftCore record")
    parser.add_argument("--out", help="catalogue file to write")
    parser.add_argument("--manifest", help="manifest file to write (where each record came from)")
    parser.add_argument("--list-inputs", action="store_true", help="print the files and folders read")
    parser.add_argument("--split", help="catalogue file to split into one file per record")
    parser.add_argument("--out-dir", help="folder for --split")
    args = parser.parse_args(argv)
    try:
        if args.split:
            if not args.out_dir:
                raise AssembleError("--split needs --out-dir")
            keys = split_catalogue(args.split, args.out_dir)
            print(f"DeviceTypeSources: {len(keys)} records written to {args.out_dir}")
            return 0
        if not args.raftcore or not args.out:
            raise AssembleError("--raftcore and --out are needed")
        if args.all:
            path = raftcore_records_path(args.raftcore)
            records, _ = load_catalogue(path)
            write_if_changed(args.out, catalogue_text(OrderedDict((k, records[k]) for k in
                                                                  sorted(records, key=lambda k: k.encode("utf-8")))))
            if args.list_inputs:
                print(("dir:" if os.path.isdir(path) else "file:") + os.path.abspath(path))
            return 0
        if not args.select:
            raise AssembleError("--select (or --all) is needed")
        paths = [p for p in args.select.split(",") if p.strip()]
        result = assemble(paths, args.raftcore, args.section)
        for message in result.messages:
            print(f"DeviceTypeSources: {message}", file=sys.stderr)
        write_if_changed(args.out, catalogue_text(result.records))
        if args.manifest:
            write_if_changed(args.manifest, manifest_text(result, args.section))
        if args.list_inputs:
            for path in result.inputs:
                print(("dir:" if os.path.isdir(path) else "file:") + os.path.abspath(path))
        return 0
    except AssembleError as err:
        print(f"DeviceTypeSources ERROR: {err}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
