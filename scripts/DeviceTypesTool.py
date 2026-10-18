#!/usr/bin/env python3
"""Device type tools: manage a project's DeviceTypeSources.json and build runtime type files

The logic behind `raft devtypes` (the raft CLI runs this script from the project's RaftCore, so its rules are those of
the firmware being built) - see devdocs/device-type-record-management-plan.md section 3.7. It can also be run on its
own.

Common options (before the command):
    --raftcore <dir>        RaftCore folder (its devtypes/records are the "builtin" source)
    --select <file>[,...]   DeviceTypeSources.json files, Common's first
    --edit <file>           the DeviceTypeSources.json that init, add and remove change (default: the last --select)
    --lock <file>           lock file for git sources
    --cache-dir <dir>       folder for fetched git sources
    --override-dir <dir>    folder of local copies that replace git sources (raftdevtypes/)
    --compiled <file>       the records compiled into the firmware, as a catalogue file (e.g. the build's
                            DeviceTypeRecords.compiled.json); default: the builtin selection
    --json                  machine-readable output (list, check, typefile)

Commands:
    init                      create the --edit file selecting every RaftCore record ("raftcore:*")
    list                      the records selected for the firmware and for the type file, with their sources
    add <pattern>... [--typefile]     add to the selection (e.g. raftcore:LSM6DS3, local:*)
    remove <pattern>... [--typefile]  remove from the selection (a record selected by a wildcard is excluded)
    new <key> [--source <name>] [--address 0xHH]   a new record file in a path source, ready to fill in
    check                     the grammar, decode code and generator checks, without a build
    update                    resolve git refs again and update the lock file
    diff [<key>]              records replaced by a local copy, and type file overrides, against what they replace
    typefile --out <file> [--compiled <catalogue>] [--max-bytes <n>]   build a device type file (devlib.json)
"""

import argparse
import difflib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from collections import OrderedDict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import AssembleDeviceTypes as A  # noqa: E402
import DeviceTypeGrammar  # noqa: E402

SCRIPTS_DIR = os.path.dirname(os.path.abspath(__file__))


class ToolError(Exception):
    pass


def _select_paths(args):
    return [p for p in (args.select or "").split(",") if p.strip()]


def _git_options(args, update=False):
    return A.GitOptions(args.cache_dir, args.lock, args.override_dir, update)


def _assemble(args, section, update=False):
    paths = _select_paths(args)
    if not paths:
        raise ToolError("no DeviceTypeSources.json - create one with init")
    return A.assemble(paths, args.raftcore, section, _git_options(args, update))


def _write_lock(args, result):
    if args.lock and result.lock_text is not None:
        A.write_if_changed(args.lock, result.lock_text)


def _edit_path(args):
    if args.edit:
        return args.edit
    paths = _select_paths(args)
    if not paths:
        raise ToolError("no DeviceTypeSources.json to change - create one with init")
    return paths[-1]


def _read_edit_file(path):
    if not os.path.isfile(path):
        raise ToolError(f"{path} doesn't exist - create it with init")
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f, object_pairs_hook=OrderedDict)


def _write_json(path, content):
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(json.dumps(content, indent=4) + "\n")


def _print_json(value):
    print(json.dumps(value, indent=1))


def _compiled_records(args, builtin):
    """The records compiled into the firmware: --compiled, else the builtin selection"""
    if args.compiled:
        records, _ = A.load_catalogue(args.compiled, record_file=False)
        return records
    return builtin.records


def _short_commit(result, source):
    commit = result.commits.get(source)
    return commit[:10] if commit else ""


def _policy_of(record):
    disable = record.get("disable")
    return "disabledClaim" if disable == "claim" else ("disabled" if disable is True else "enabled")


################################################################################################################
# Commands
################################################################################################################

def cmd_init(args):
    path = _edit_path(args) if (args.edit or _select_paths(args)) else None
    if not path:
        raise ToolError("--edit (the DeviceTypeSources.json to create) is needed")
    if os.path.exists(path):
        raise ToolError(f"{path} already exists")
    _write_json(path, OrderedDict([
        ("_notes", "Device type records compiled into the firmware (builtin) and for a device type file (typeFile). "
                   "See RaftCore's wiki page Selecting Device Types"),
        ("sources", OrderedDict([("raftcore", OrderedDict([("bundled", True)]))])),
        ("builtin", OrderedDict([("include", ["raftcore:*"])])),
    ]))
    print(f"Created {path} selecting every RaftCore record (raftcore:*). If features.cmake has "
          f"/devtypes/DeviceTypeRecords.json in DEV_TYPE_JSON_FILES, remove it - the selection replaces it")
    return 0


def cmd_list(args):
    builtin = _assemble(args, "builtin")
    type_file = _assemble(args, "typeFile")
    _write_lock(args, type_file if type_file.lock_text is not None else builtin)
    kinds = {}
    if type_file.records or type_file.policy_only:
        _, entries = A.build_type_file(type_file, _compiled_records(args, builtin), max_bytes=1 << 30)
        kinds = {e["key"]: e for e in entries}
    rows = []
    for key, record in builtin.records.items():
        source = builtin.origin[key][0]
        rows.append(OrderedDict([("key", key), ("section", "builtin"), ("source", source),
                                 ("commit", _short_commit(builtin, source)), ("policy", _policy_of(record))]))
    for key, record in type_file.records.items():
        source = type_file.origin[key][0]
        entry = kinds.get(key, {})
        rows.append(OrderedDict([("key", key), ("section", "typeFile"), ("source", source),
                                 ("commit", _short_commit(type_file, source)), ("policy", _policy_of(record)),
                                 ("kind", entry.get("kind", "")), ("noDecoder", bool(entry.get("noDecoder")))]))
    for key, policy in type_file.policy_only.items():
        rows.append(OrderedDict([("key", key), ("section", "typeFile"), ("source", ""), ("commit", ""),
                                 ("policy", policy), ("kind", "policy"), ("noDecoder", False)]))
    if args.json:
        _print_json(rows)
        return 0
    print(f"{'Key':28} {'Section':9} {'Source':12} {'Commit':11} {'Policy':14} Type file")
    for row in rows:
        extra = ""
        if row["section"] == "typeFile":
            extra = row["kind"] + (" (no on-device decoder)" if row["noDecoder"] else "")
        print(f"{row['key']:28} {row['section']:9} {row['source']:12} {row['commit']:11} {row['policy']:14} {extra}")
    print(f"{len(builtin.records)} records selected for the firmware, "
          f"{len(type_file.records) + len(type_file.policy_only)} entries for the type file" +
          (f" (compared with the compiled records in {args.compiled})" if args.compiled else ""))
    if type_file.fs_image:
        print("The build puts the type file in the file system image (typeFile.fsImage)")
    for message in builtin.messages + type_file.messages:
        print(f"  {message}")
    return 0


def _patterns_ok(args, section):
    """Check the selection resolves (raises AssembleError if not)"""
    _assemble(args, section)


def cmd_add(args):
    section = "typeFile" if args.typefile else "builtin"
    path = _edit_path(args)
    content = _read_edit_file(path)
    spec = content.setdefault(section, OrderedDict())
    include = spec.get("include")
    if include is None:
        include = ["*"] if section == "builtin" else []
    added = []
    for pattern in args.patterns:
        if pattern in include:
            print(f"{pattern} is already in {section}.include")
            continue
        if section == "builtin" and "*" in include:
            print(f"{pattern}: everything is already included (builtin.include has \"*\")")
            continue
        include.append(pattern)
        added.append(pattern)
        if pattern in spec.get("exclude", []):
            spec["exclude"].remove(pattern)
    spec["include"] = include
    original = _read_edit_file(path)
    _write_json(path, content)
    try:
        _patterns_ok(args, section)
    except A.AssembleError as err:
        _write_json(path, original)
        raise ToolError(f"not added: {err}")
    for pattern in added:
        print(f"Added {pattern} to {section}.include in {path}")
    return 0


def cmd_remove(args):
    section = "typeFile" if args.typefile else "builtin"
    path = _edit_path(args)
    content = _read_edit_file(path)
    spec = content.setdefault(section, OrderedDict())
    include = spec.get("include")
    if include is None:
        include = ["*"] if section == "builtin" else []
    for pattern in args.patterns:
        if pattern in include:
            include.remove(pattern)
            print(f"Removed {pattern} from {section}.include in {path}")
        else:
            # Selected by a wildcard (or not at all): exclude it
            exclude = spec.setdefault("exclude", [])
            if pattern not in exclude:
                exclude.append(pattern)
            print(f"{pattern} isn't listed in {section}.include - added to {section}.exclude in {path}")
    spec["include"] = include
    _write_json(path, content)
    return 0


def _path_sources(args):
    """Path sources that are folders: [(name, folder)]"""
    out = []
    selection = A.read_selection(_select_paths(args))
    for name, (spec, base_dir, _) in selection["sources"].items():
        if isinstance(spec, dict) and "path" in spec:
            folder = spec["path"] if os.path.isabs(spec["path"]) else os.path.normpath(os.path.join(base_dir, spec["path"]))
            if not folder.endswith(".json"):
                out.append((name, folder))
    return out


def cmd_new(args):
    sources = _path_sources(args)
    if args.source:
        sources = [s for s in sources if s[0] == args.source]
        if not sources:
            raise ToolError(f"no path source called \"{args.source}\" (a folder of records)")
    if not sources:
        raise ToolError("no path source (a folder of records) in DeviceTypeSources.json - add one, e.g. "
                        "\"local\": {\"path\": \"devtypes\"}")
    if len(sources) > 1 and not args.source:
        raise ToolError(f"more than one path source ({', '.join(s[0] for s in sources)}) - choose one with --source")
    name, folder = sources[0]
    path = os.path.join(folder, args.key + ".json")
    if os.path.exists(path):
        raise ToolError(f"{path} already exists")
    record = OrderedDict([
        ("_notes", "Fill in from the datasheet: detectionValues (a read that identifies the device), initValues, "
                   "pollInfo.c (the poll reads) and devInfoJson.resp (b = bytes the poll reads; a = the attributes). "
                   "See RaftCore's wiki page Device Type Record Format"),
        ("addresses", args.address),
        ("detectionValues", ""),
        ("initValues", ""),
        ("pollInfo", OrderedDict([("c", ""), ("i", 100), ("s", 1)])),
        ("devInfoJson", OrderedDict([("name", args.key), ("desc", ""), ("manu", ""), ("type", args.key),
                                     ("clas", []), ("resp", OrderedDict([("b", 0), ("a", [])]))])),
    ])
    _write_json(path, record)
    print(f"Created {path}")
    print(f"Select it with: raft devtypes add {name}:{args.key}   (unless {name}:* is selected already)")
    return 0


def cmd_check(args):
    problems = []
    warnings = []
    builtin = _assemble(args, "builtin")
    type_file = _assemble(args, "typeFile")
    _write_lock(args, builtin if builtin.lock_text is not None else type_file)
    # The generator checks the compiled records; the type file's records get the same grammar and decode code checks
    for key, record in type_file.records.items():
        for field, grammar in DeviceTypeGrammar.check_record(record):
            for code, pos in grammar.issues:
                line = f"type file record \"{key}\" {DeviceTypeGrammar.FIELD_NAMES[field]}: {code} at {pos}"
                (problems if DeviceTypeGrammar.is_error(code) else warnings).append(line)
        for problem in A.check_decode_code(key, record):
            warnings.append(f"type file record \"{key}\" resp.c: {problem}")
    # The generator's checks on the compiled selection
    with tempfile.TemporaryDirectory() as tmp:
        catalogue = os.path.join(tmp, "catalogue.json")
        with open(catalogue, "w", encoding="utf-8") as f:
            f.write(A.catalogue_text(builtin.records))
        run = subprocess.run([sys.executable, os.path.join(SCRIPTS_DIR, "ProcessDevTypeJsonToC.py"), catalogue,
                              os.path.join(tmp, "t.h"), os.path.join(tmp, "p.h")], capture_output=True, text=True)
        for line in (run.stdout + run.stderr).splitlines():
            if line.startswith(("WARNING", "GRAMMAR WARNING", "DECODE CODE WARNING")):
                warnings.append(line.split(": ", 1)[1])
            elif line.startswith(("ERROR", "GRAMMAR ERROR")) or "mismatch" in line:
                problems.append(line.split(": ", 1)[-1])
        if run.returncode != 0 and not problems:
            problems.append("the generator failed: " + (run.stderr.strip().splitlines() or ["?"])[-1])
    # The type file
    if type_file.records or type_file.policy_only:
        try:
            A.build_type_file(type_file, _compiled_records(args, builtin))
        except A.AssembleError as err:
            problems.append(f"type file: {err}")
    warnings = list(dict.fromkeys(warnings))
    problems = list(dict.fromkeys(problems))
    if args.json:
        _print_json(OrderedDict([("ok", not problems), ("errors", problems), ("warnings", warnings),
                                 ("builtin", len(builtin.records)), ("typeFile", len(type_file.records))]))
        return 0 if not problems else 1
    for line in problems:
        print(f"ERROR   {line}")
    for line in warnings:
        print(f"warning {line}")
    print(f"Checked {len(builtin.records)} compiled records and {len(type_file.records)} type file records: "
          f"{len(problems)} errors, {len(warnings)} warnings")
    return 0 if not problems else 1


def cmd_update(args):
    old = ""
    if args.lock and os.path.isfile(args.lock):
        with open(args.lock, "r", encoding="utf-8") as f:
            old = f.read()
    results = [_assemble(args, "builtin", update=True), _assemble(args, "typeFile", update=True)]
    # Both sections share the lock: write the one that covers the git sources of both (the last that has text)
    for result in results:
        _write_lock(args, result)
    for result in results:
        for message in result.messages:
            print(message)
    new = ""
    if args.lock and os.path.isfile(args.lock):
        with open(args.lock, "r", encoding="utf-8") as f:
            new = f.read()
    print("Lock file updated" if new != old else "Lock file unchanged")
    return 0


def _pretty(record):
    return json.dumps(record, indent=4).splitlines()


def cmd_diff(args):
    builtin = _assemble(args, "builtin")
    type_file = _assemble(args, "typeFile")
    shown = 0
    for result in (builtin, type_file):
        for key, replaced in result.replaced.items():
            if args.key and key != args.key:
                continue
            for source, record in replaced:
                chosen = result.origin[key][0]
                print(f"=== {key}: {chosen} replaces {source}")
                for line in difflib.unified_diff(_pretty(record), _pretty(result.records[key]), f"{source}:{key}",
                                                 f"{chosen}:{key}", lineterm=""):
                    print(line)
                shown += 1
    compiled_records = _compiled_records(args, builtin)
    for key, record in type_file.records.items():
        if (args.key and key != args.key) or key not in compiled_records:
            continue
        compiled = compiled_records[key]
        if A._comparable(record) == A._comparable(compiled):
            continue
        print(f"=== {key}: type file record overrides the compiled record")
        for line in difflib.unified_diff(_pretty(compiled), _pretty(record), f"compiled:{key}", f"typeFile:{key}",
                                         lineterm=""):
            print(line)
        shown += 1
    if not shown:
        print("No local copies replace another source's record, and the type file overrides no compiled record")
    return 0


def cmd_typefile(args):
    type_file = _assemble(args, "typeFile")
    compiled_from = args.typefile_compiled or args.compiled
    if compiled_from:
        compiled, _ = A.load_catalogue(compiled_from, record_file=False)
    else:
        compiled = _assemble(args, "builtin").records
    text, entries = A.build_type_file(type_file, compiled, args.max_bytes)
    _write_lock(args, type_file)
    A.write_if_changed(args.out, text)
    crc = "%04x" % crc16(text.encode("utf-8"))
    report = OrderedDict([("file", os.path.abspath(args.out)), ("bytes", len(text)), ("maxBytes", args.max_bytes),
                          ("crc", crc), ("compiled", os.path.abspath(compiled_from) if compiled_from else "builtin"),
                          ("entries", entries)])
    if args.json:
        _print_json(report)
        return 0
    counts = OrderedDict((kind, sum(1 for e in entries if e["kind"] == kind))
                         for kind in ("new", "override", "unchanged", "policy"))
    print(f"Type file {args.out}: {len(text)} bytes (limit {args.max_bytes}), CRC {crc}; " +
          ", ".join(f"{n} {kind}" for kind, n in counts.items()) +
          (" (unchanged records are left out)" if counts["unchanged"] else ""))
    print(f"  compared with the compiled records in {compiled_from}" if compiled_from else
          "  compared with the builtin selection (build the project for the records it actually compiles)")
    for entry in entries:
        print(f"  {entry['key']:28} {entry['kind']}{' - no on-device decoder' if entry.get('noDecoder') else ''}")
    no_decoder = [e["key"] for e in entries if e.get("noDecoder")]
    if no_decoder:
        print("No on-device decoder (DataLogger, RaftROS and named values won't decode them; raftjs and the WebUI "
              "will): " + ", ".join(no_decoder))
    return 0


def crc16(data):
    """CRC16-CCITT as the firmware's devman/typefile and filehash report it (DeviceTypeFileLoader::crc16)"""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


################################################################################################################
# Command line
################################################################################################################

def main(argv=None):
    parser = argparse.ArgumentParser(description="Device type tools (raft devtypes)")
    parser.add_argument("--raftcore", required=True)
    parser.add_argument("--select", default="")
    parser.add_argument("--edit")
    parser.add_argument("--lock")
    parser.add_argument("--cache-dir")
    parser.add_argument("--override-dir")
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--compiled")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("init")
    sub.add_parser("list")
    p = sub.add_parser("add")
    p.add_argument("patterns", nargs="+")
    p.add_argument("--typefile", action="store_true")
    p = sub.add_parser("remove")
    p.add_argument("patterns", nargs="+")
    p.add_argument("--typefile", action="store_true")
    p = sub.add_parser("new")
    p.add_argument("key")
    p.add_argument("--source")
    p.add_argument("--address", default="0x00")
    sub.add_parser("check")
    sub.add_parser("update")
    p = sub.add_parser("diff")
    p.add_argument("key", nargs="?")
    p = sub.add_parser("typefile")
    p.add_argument("--out", required=True)
    p.add_argument("--compiled", dest="typefile_compiled")
    p.add_argument("--max-bytes", type=int, default=A.TYPE_FILE_MAX_BYTES_DEFAULT)
    args = parser.parse_args(argv)
    commands = {"init": cmd_init, "list": cmd_list, "add": cmd_add, "remove": cmd_remove, "new": cmd_new,
                "check": cmd_check, "update": cmd_update, "diff": cmd_diff, "typefile": cmd_typefile}
    try:
        return commands[args.command](args)
    except (ToolError, A.AssembleError) as err:
        print(f"ERROR: {err}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
