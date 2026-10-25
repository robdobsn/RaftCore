#!/usr/bin/env python3
"""Assemble the device type records for a build from DeviceTypeSources.json

Device type records are kept one per file (records/<Key>.json, the record body as it appears under "devTypes"), and
a project selects the records it needs in DeviceTypeSources.json (systypes/Common/ and systypes/<SysType>/). This
script resolves the selection into one catalogue file that ProcessDevTypeJsonToC.py reads - see
devdocs/device-type-record-management-plan.md.

DeviceTypeSources.json:
    {
      "sources":  { "raftcore": { "bundled": true }, "local": { "path": "devtypes" } },
      "builtin":  { "include": ["raftcore:LSM6DS3", "local:*"], "exclude": [], "policy": { "TrillRing": "disabled" } },
      "typeFile": { "include": [], "policy": {}, "fsImage": false }
    }
- sources: "bundled" (RaftCore's records; "builtin": true is an older name for it), "path" (a folder of records, one record file or a catalogue file, relative
  to the DeviceTypeSources.json that names it), "git" ({"git": <url>, "ref": <tag, branch or full commit hash>,
  "path": <folder or record file in the repo, default "records">})
- include/exclude: "*", "<source>:*", "<source>:<key>", "<key>" (a key only one source has), or "<source>:@<set>" (a
  named set of RaftCore's records, devtypes/sets/<set>.json, for the bundled source only). builtin's include
  defaults to "*", typeFile's to nothing. A key from a path source replaces the same key from another source (the later path source wins); the same key
  from two other sources is an error unless include names the source
- policy: "enabled", "disabled" or "disabledClaim" for a selected key
- typeFile.fsImage: true puts the type file in the SysType's file system image (see --fs-image)
- A SysType's file adds to Common's: sources are added (or replaced by name), include and exclude lists are joined,
  policies merged (the SysType's win)

Git sources:
- Fetched (shallow, only the source's path) into --cache-dir, by commit, and not fetched again for a commit already
  cached - so a build works offline once the cache is filled
- Pinned by the lock file (--lock, e.g. systypes/<SysType>/DeviceTypeSources.lock): the commit each source's ref
  resolved to, and the SHA-256 of each record used. A source whose git URL and ref are unchanged uses the locked
  commit; a new or changed source is resolved and the lock written. --update resolves every ref again
- --override-dir (e.g. raftdevtypes/): a folder or link <override-dir>/<source> replaces that git source (for
  editing records against a real build); a broken link is an error
- A source may have raftdevtypes.json at its root: {"name": ..., "formatVersion": 1, "minRaftCore": "x.y.z"}
- Decode code (resp.c) in a record from a git source must use only the decode language (it is compiled into the
  firmware): anything else is an error

Usage:
    AssembleDeviceTypes.py --raftcore <dir> --select <file>[,<file>] [--section builtin|typeFile]
                           --out <catalogue.json> [--manifest <manifest.json>] [--list-inputs]
                           [--cache-dir <dir>] [--lock <file>] [--override-dir <dir>] [--update]
    AssembleDeviceTypes.py --raftcore <dir> --all --out <catalogue.json>     (every RaftCore record, one file)
    AssembleDeviceTypes.py --split <catalogue.json> --out-dir <records dir>   (one file per record, as written)
    AssembleDeviceTypes.py --raftcore <dir> --pages <site dir> --label <v1.62.0 or main>
    AssembleDeviceTypes.py --pages-index <site dir>
        The catalogue on RaftCore's GitHub Pages site: <site dir>/<label>/ has records/<Key>.json (each file as it is),
        catalogue.json (key, file, SHA-256 and a summary of each record) and DeviceTypeRecords.json (all of them);
        index.json lists the versions, "main" first, then releases, newest first
    AssembleDeviceTypes.py --raftcore <dir> --select <file>[,<file>] --typefile --out <devlib.json>
                           [--compiled <catalogue.json>] [--max-bytes <n>] [--report <report.json>]
        A device type file for the device's file system from the typeFile selection: minified, without _notes; each
        record is new, an override of a compiled record or unchanged (left out); typeFile policies for compiled types
        become {"disable": ...} entries. Records with no on-device decoder are listed. The compiled records are the
        builtin selection of the same files unless --compiled gives them. The file's top-level _sources (ignored by
        the device) says what it was made from: the sources that supplied a record (git ones with their commit,
        RaftCore's with its version) and the typeFile selection. The size limit is the device's default for the SysType
        (more if the sdkconfig.defaults next to the --select files enable PSRAM) unless --max-bytes gives one

    AssembleDeviceTypes.py --raftcore <dir> --select <file>[,<file>] --fs-image <dir> --systypes <SysTypes.json>
                           --compiled <file or folder>[,...] [--compiled-out <catalogue.json>]
                           [--sdkconfig <sdkconfig.defaults>] [--cache-dir ...] [--lock ...] [--override-dir ...]
        For the build: if typeFile.fsImage is true, the type file is written into <dir> at the path the device loads it
        from (DevMan typeFile, else DeviceLibrary devLibFile, else /local/devlib.json, which must be on /local/), within
        DevMan typeFileLimits.maxFileBytes (default: the device's, larger if --sdkconfig enables PSRAM); the compiled records are the generator's input files, merged as it merges
        them. Prints "fsimage:<file written>" and the inputs; <dir> is emptied if fsImage isn't set. --compiled-out
        writes the compiled records as one catalogue file (build/<SysType>/raft/DeviceTypeRecords.compiled.json), which
        the raft CLI builds type files against

--list-inputs prints the files and folders read, one per line ("file:<path>" or "dir:<path>"), for the build's
dependencies.
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from collections import OrderedDict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PseudocodeHandler import PseudocodeHandler  # noqa: E402

RECORDS_SUBDIR = os.path.join("devtypes", "records")
SETS_SUBDIR = os.path.join("devtypes", "sets")
LEGACY_CATALOGUE = os.path.join("devtypes", "DeviceTypeRecords.json")
POLICY_VALUES = {"enabled": None, "disabled": True, "disabledClaim": "claim"}
SECTIONS = ("builtin", "typeFile")
LOCK_FORMAT_VERSION = 1
SOURCE_FORMAT_VERSION = 1
PROVENANCE_FORMAT_VERSION = 1   # a type file's _sources
PAGES_FORMAT_VERSION = 1        # the published catalogue (--pages)
SOURCE_MANIFEST = "raftdevtypes.json"


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
    merged = {"sources": OrderedDict(), "messages": [], "sections": {s: {"include": [], "exclude": [], "policy": OrderedDict(),
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
            if "builtin" in spec and "bundled" not in spec:
                # The older name for "bundled"
                spec = OrderedDict((("bundled" if k == "builtin" else k), v) for k, v in spec.items())
                merged["messages"].append(f"{os.path.basename(path)}: source \"{name}\": \"builtin\": true is now "
                                          f"\"bundled\": true")
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
                if section != "typeFile":
                    raise AssembleError(f"{path}: fsImage belongs in the typeFile section")
                target["fsImage"] = bool(spec["fsImage"])
    return merged


################################################################################################################
# Git sources
################################################################################################################

class GitOptions:
    """Where git sources are cached, the lock file, the local override folder, and whether to resolve refs again"""
    def __init__(self, cache_dir=None, lock_path=None, override_dir=None, update=False):
        self.cache_dir = cache_dir
        self.lock_path = lock_path
        self.override_dir = override_dir
        self.update = update


def _git(args, cwd=None):
    run = subprocess.run(["git"] + args, cwd=cwd, capture_output=True, text=True)
    return run.returncode == 0, (run.stdout.strip() if run.returncode == 0 else run.stderr.strip())


def _is_commit_hash(ref):
    return re.fullmatch(r"[0-9a-f]{40}", ref or "") is not None


def _fetch_into(tmp, name, url, ref, subpath, use_filter):
    """Fetch ref into a new repository at tmp and check it out (only subpath and the manifest where sparse checkout is
    available). Returns (ok, commit or message)"""
    shutil.rmtree(tmp, ignore_errors=True)
    ok, msg = _git(["init", "-q", tmp])
    if ok:
        ok, msg = _git(["remote", "add", "origin", url], tmp)
    if not ok:
        raise AssembleError(f"source \"{name}\": git: {msg}")
    # Shallow, and (with use_filter) without file contents until checkout, so only the source's path is downloaded.
    # The server may not allow the filter, or a shallow fetch of a commit by hash, so fall back step by step
    variants = ([["--depth", "1", "--filter=blob:none"]] if use_filter else []) + [["--depth", "1"], []]
    for extra in variants:
        ok, msg = _git(["fetch", "-q"] + extra + ["origin", ref], tmp)
        if ok:
            break
    if not ok:
        return False, msg
    ok, commit = _git(["rev-parse", "FETCH_HEAD^{commit}"], tmp)
    if not ok:
        return False, commit
    sparse_ok, _ = _git(["sparse-checkout", "set", "--no-cone", "/" + subpath.strip("/"), "/" + SOURCE_MANIFEST], tmp)
    ok, msg = _git(["checkout", "-q", "--detach", "FETCH_HEAD"], tmp)
    if not ok and sparse_ok:
        _git(["sparse-checkout", "disable"], tmp)
        ok, msg = _git(["checkout", "-q", "--detach", "FETCH_HEAD"], tmp)
    if ok:
        # Older git (e.g. 2.34) exits 0 from a checkout whose file contents the server wouldn't give (by hash), leaving
        # the files missing
        ok_ls, missing = _git(["ls-files", "--deleted"], tmp)
        if not ok_ls or missing:
            ok, msg = False, missing or "files missing after checkout"
    return (True, commit) if ok else (False, f"checkout of {commit}: {msg}")


def _checkout(cache_dir, name, url, ref, subpath):
    """Fetch ref (a tag, branch or commit) of a repo into the cache. Returns (commit, checkout folder)"""
    url_key = hashlib.sha256(url.encode("utf-8")).hexdigest()[:10]
    base = os.path.join(cache_dir, f"{name}-{url_key}")
    if _is_commit_hash(ref) and os.path.isfile(os.path.join(base, ref, ".raft-complete")):
        return ref, os.path.join(base, ref)
    os.makedirs(base, exist_ok=True)
    tmp = os.path.join(base, f".fetch-{os.getpid()}")
    try:
        # Without file contents first; a server that can't give contents later (by hash) gets a plain fetch
        ok, result = _fetch_into(tmp, name, url, ref, subpath, use_filter=True)
        if not ok:
            ok, result = _fetch_into(tmp, name, url, ref, subpath, use_filter=False)
        if not ok:
            hint = "" if _is_commit_hash(ref) or not re.fullmatch(r"[0-9a-f]{7,39}", ref) else \
                " (a commit must be given as its full 40-character hash)"
            raise AssembleError(f"source \"{name}\": can't fetch {ref} from {url}{hint}: {result}")
        commit = result
        if _is_commit_hash(ref) and commit != ref:
            raise AssembleError(f"source \"{name}\": fetched {commit} for commit {ref}")
        target = os.path.join(base, commit)
        if not os.path.isfile(os.path.join(target, ".raft-complete")):
            shutil.rmtree(target, ignore_errors=True)
            os.rename(tmp, target)
            with open(os.path.join(target, ".raft-complete"), "w") as marker:
                marker.write(f"{url} {ref} {commit}\n")
        return commit, target
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def _version_tuple(version):
    return tuple(int(part) for part in re.findall(r"\d+", version or "")[:3])


def raftcore_version(raftcore_dir):
    try:
        with open(os.path.join(raftcore_dir, "library.json"), "r", encoding="utf-8") as f:
            return json.load(f).get("version")
    except (OSError, ValueError):
        return None


def _check_source_manifest(name, root, raftcore_dir):
    path = os.path.join(root, SOURCE_MANIFEST)
    if not os.path.isfile(path):
        return
    manifest = _read_json(path)
    fmt = manifest.get("formatVersion", 1)
    if fmt != SOURCE_FORMAT_VERSION:
        raise AssembleError(f"source \"{name}\": {SOURCE_MANIFEST} formatVersion {fmt} isn't supported by this "
                            f"RaftCore (supports {SOURCE_FORMAT_VERSION})")
    needed = manifest.get("minRaftCore")
    have = raftcore_version(raftcore_dir)
    if needed and have and _version_tuple(have) < _version_tuple(needed):
        raise AssembleError(f"source \"{name}\" needs RaftCore {needed} or later (this build uses {have})")


def check_decode_code(key, record):
    """Problems in a record's decode code (resp.c) and init code (initCode) - see PseudocodeHandler.check_code"""
    problems = []
    resp = record.get("devInfoJson", {}).get("resp", {}) if isinstance(record, dict) else {}
    custom = resp.get("c")
    if isinstance(custom, dict) and custom.get("c"):
        attrs = [attr.get("n", "") for attr in resp.get("a", [])]
        problems += PseudocodeHandler().check_code(custom["c"], attrs)
    init_code = record.get("initCode") if isinstance(record, dict) else None
    init_code = init_code.get("c", "") if isinstance(init_code, dict) else (init_code or "")
    if init_code:
        problems += ["initCode: " + p for p in PseudocodeHandler().check_code(init_code, [], code_kind="init")]
    return problems


def _init_value_names(record):
    """(names a record's initCode computes, value slot and guard names its initValues uses)"""
    import DeviceTypeGrammar
    init_code = record.get("initCode")
    code = init_code.get("c", "") if isinstance(init_code, dict) else (init_code or "")
    computed = set(re.findall(r"\binit\.([A-Za-z_][A-Za-z0-9_]*)", code))
    steps = DeviceTypeGrammar.compile(record.get("initValues", ""), "init").steps
    slots = set(name for step in steps for _, name in step.slots) | set(step.guard for step in steps if step.guard)
    return computed, slots


class Lock:
    """The lock file: the commit each git source resolved to, and the hash of each record from a git source"""
    def __init__(self, path):
        self.path = path
        self.sources = OrderedDict()
        self.records = OrderedDict()
        self.changed = False
        if path and os.path.isfile(path):
            content = _read_json(path)
            if content.get("formatVersion") != LOCK_FORMAT_VERSION:
                raise AssembleError(f"{path}: formatVersion {content.get('formatVersion')} isn't supported")
            self.sources = content.get("sources", OrderedDict())
            self.records = content.get("records", OrderedDict())
        self.used_sources = set()

    def text(self, records):
        return json.dumps({"formatVersion": LOCK_FORMAT_VERSION,
                           "sources": OrderedDict((n, self.sources[n]) for n in self.sources if n in self.used_sources),
                           "records": records}, indent=1) + "\n"


def _load_git_source(name, spec, raftcore_dir, git_options, lock, messages, inputs):
    url = spec["git"]
    ref = spec.get("ref")
    if not isinstance(ref, str) or not ref:
        raise AssembleError(f"source \"{name}\": \"ref\" (a tag, branch or commit) is needed")
    subpath = spec.get("path", "records")
    commit = None
    override = os.path.join(git_options.override_dir, name) if git_options.override_dir else None
    if override and os.path.islink(override) and not os.path.exists(override):
        raise AssembleError(f"source \"{name}\": {override} is a broken link - fix it or remove it")
    if override and os.path.isdir(override):
        root = override
        messages.append(f"source \"{name}\": using the local copy {override}")
        if name in lock.sources:
            lock.used_sources.add(name)
    else:
        if not git_options.cache_dir:
            raise AssembleError(f"source \"{name}\": git sources need a cache folder (--cache-dir)")
        locked = lock.sources.get(name)
        if locked and locked.get("git") == url and locked.get("ref") == ref and not git_options.update:
            try:
                commit, root = _checkout(git_options.cache_dir, name, url, locked["commit"], subpath)
            except AssembleError:
                # The server may not give a commit by hash: fetch the ref, which must still be the locked commit
                commit, root = _checkout(git_options.cache_dir, name, url, ref, subpath)
                if commit != locked["commit"]:
                    raise AssembleError(f"source \"{name}\": {ref} is now commit {commit} but the lock has "
                                        f"{locked['commit']} - update the lock (--update) to use the new commit")
        else:
            commit, root = _checkout(git_options.cache_dir, name, url, ref, subpath)
            if not locked or locked.get("commit") != commit or locked.get("git") != url or locked.get("ref") != ref:
                lock.changed = True
                messages.append(f"source \"{name}\": {ref} is commit {commit} (lock updated)")
            lock.sources[name] = OrderedDict([("git", url), ("ref", ref), ("commit", commit)])
        lock.used_sources.add(name)
    _check_source_manifest(name, root, raftcore_dir)
    path = os.path.join(root, subpath)
    if not os.path.exists(path):
        raise AssembleError(f"source \"{name}\": {subpath} not found in the repo")
    records, files = load_catalogue(path)
    for key, record in records.items():
        problems = check_decode_code(key, record)
        if problems:
            raise AssembleError(f"\"{key}\" from source \"{name}\": its decode code (resp.c) isn't allowed: "
                                f"{'; '.join(problems)}")
    inputs.append(path)
    return records, files, commit


def raftcore_sets_path(raftcore_dir):
    return os.path.join(raftcore_dir, SETS_SUBDIR)


def load_sets(raftcore_dir):
    """RaftCore's named sets of records (devtypes/sets/<name>.json, {"records": [keys]}): OrderedDict name -> keys"""
    folder = raftcore_sets_path(raftcore_dir)
    sets = OrderedDict()
    if not os.path.isdir(folder):
        return sets
    for file_name in sorted(n for n in os.listdir(folder) if n.endswith(".json")):
        path = os.path.join(folder, file_name)
        content = _read_json(path)
        keys = content.get("records") if isinstance(content, dict) else None
        if not isinstance(keys, list) or not all(isinstance(k, str) for k in keys):
            raise AssembleError(f"{path}: a set holds \"records\": a list of record keys")
        sets[file_name[:-5]] = keys
    return sets


def _load_source(name, spec, base_dir, raftcore_dir):
    """(records, files, is_local, input paths)"""
    if spec.get("bundled"):
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
    raise AssembleError(f"source \"{name}\": needs \"bundled\", \"path\" or \"git\"")


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
        self.commits = OrderedDict()     # source name -> commit (git sources)
        self.lock = None
        self.lock_text = None            # the lock file's new content, if it changed
        self.policy_only = OrderedDict() # typeFile: policies for types not in the file (key -> policy name)
        self.replaced = OrderedDict()    # key -> [(source name, record)] replaced by a path source's record
        self.messages = []
        self.inputs = []                 # files and folders read
        self.fs_image = False
        self.provenance = None           # what the records were selected from (build_type_file writes it as _sources)


def assemble(selection_paths, raftcore_dir, section="builtin", git_options=None):
    """Resolve a selection into records (sorted by key)"""
    git_options = git_options or GitOptions()
    selection = read_selection(selection_paths)
    result = Assembly()
    result.inputs.extend(os.path.abspath(p) for p in selection_paths)
    result.messages.extend(selection["messages"])
    lock = Lock(git_options.lock_path)
    result.lock = lock
    if git_options.lock_path and os.path.isfile(git_options.lock_path):
        result.inputs.append(os.path.abspath(git_options.lock_path))
    sources = OrderedDict()
    for name, (spec, base_dir, _) in selection["sources"].items():
        if isinstance(spec, dict) and "git" in spec:
            records, files, commit = _load_git_source(name, spec, raftcore_dir, git_options, lock, result.messages,
                                                      result.inputs)
            sources[name] = (records, files, False)
            if commit:
                result.commits[name] = commit
            continue
        records, files, is_local, inputs = _load_source(name, spec, base_dir, raftcore_dir)
        sources[name] = (records, files, is_local)
        result.inputs.extend(inputs)
        if spec.get("bundled") and os.path.isdir(raftcore_sets_path(raftcore_dir)):
            result.inputs.append(raftcore_sets_path(raftcore_dir))
    names = list(sources)
    bundled = [n for n, (spec, _, _) in selection["sources"].items() if isinstance(spec, dict) and spec.get("bundled")]
    sets_cache = []

    def set_keys(pattern, source, key, list_name):
        """The keys of a named set ("<source>:@<set>", RaftCore's records only)"""
        if source is None or source not in bundled:
            raise AssembleError(f"{section}.{list_name} \"{pattern}\": named sets (@...) are only in RaftCore's records - "
                                f"use \"<the bundled source>:{key}\"")
        if not sets_cache:
            sets_cache.append(load_sets(raftcore_dir))
        keys = sets_cache[0].get(key[1:])
        if keys is None:
            raise AssembleError(f"{section}.{list_name} \"{pattern}\": no set \"{key[1:]}\" in RaftCore's records "
                                f"(sets: {', '.join(sets_cache[0]) or 'none'})")
        missing = [k for k in keys if k not in sources[source][0]]
        if missing:
            raise AssembleError(f"set \"{key[1:]}\" names {', '.join(missing)}, not in RaftCore's records")
        return keys
    chosen = selection["sections"][section]
    result.fs_image = chosen["fsImage"]
    # builtin includes everything unless it says otherwise; a type file includes nothing unless it says otherwise
    includes = chosen["include"] if chosen["hasInclude"] else (["*"] if section == "builtin" else [])

    # Candidates for each key: the sources it was selected from, and whether a source was named for it
    candidates = OrderedDict()
    for pattern in includes:
        source, key = _parse_pattern(pattern, names)
        matched = False
        for name in ([source] if source else names):
            records = sources[name][0]
            if key.startswith("@"):
                keys = set_keys(pattern, source, key, "include")
            else:
                keys = list(records) if key == "*" else ([key] if key in records else [])
            for k in keys:
                entry = candidates.setdefault(k, {"named": OrderedDict(), "any": OrderedDict()})
                (entry["named"] if (source and key != "*" and not key.startswith("@")) else entry["any"])[name] = True
                matched = True
        if not matched and key != "*" and not key.startswith("@"):
            where = f"source \"{source}\"" if source else "any source"
            raise AssembleError(f"{section}.include \"{pattern}\": no record \"{key}\" in {where}")
    for pattern in chosen["exclude"]:
        source, key = _parse_pattern(pattern, names)
        excluded = set(set_keys(pattern, source, key, "exclude")) if key.startswith("@") else None
        for k in list(candidates):
            if key == "*" or k == key or (excluded is not None and k in excluded):
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
                    result.replaced[key] = [(n, sources[n][0][key]) for n in replaced]
            elif len(options) > 1:
                raise AssembleError(f"\"{key}\" is in sources {', '.join(options)} - name the source to use in "
                                    f"{section}.include (e.g. \"{options[0]}:{key}\")")
            else:
                source = options[0]
        records, files, _ = sources[source]
        result.records[key] = json.loads(json.dumps(records[key]), object_pairs_hook=OrderedDict)
        result.origin[key] = (source, files[key])

    # Records from git sources: as locked (a locked commit can't change, so a difference means the cache or the
    # repo's history was altered), and recorded in the lock
    lock_records = OrderedDict()
    for key, (source, path) in result.origin.items():
        if source in result.commits:
            digest = _sha256_record(path, key)
            locked = lock.records.get(key)
            if locked and locked.get("source") == source and not lock.changed and not git_options.update \
                    and locked.get("sha256") != digest:
                raise AssembleError(f"\"{key}\" from source \"{source}\" differs from the lock file's "
                                    f"(sha256 {digest}, locked {locked.get('sha256')})")
            lock_records[key] = OrderedDict([("source", source), ("commit", result.commits[source]), ("sha256", digest)])
    if git_options.lock_path:
        text = lock.text(lock_records)
        old = None
        if os.path.isfile(git_options.lock_path):
            with open(git_options.lock_path, "r", encoding="utf-8") as f:
                old = f.read()
        if (old != text) and (lock.used_sources or old is not None):
            result.lock_text = text

    # Policies
    for key, policy in chosen["policy"].items():
        if key not in result.records:
            # In a type file, a policy for a type that isn't in the file applies to a compiled record
            if section == "typeFile":
                result.policy_only[key] = policy
                continue
            raise AssembleError(f"{section}.policy \"{key}\": not a selected record")
        value = POLICY_VALUES[policy]
        if value is None:
            result.records[key].pop("disable", None)
        else:
            result.records[key]["disable"] = value
    result.provenance = _provenance(selection, section, chosen, result, raftcore_dir)
    return result


def _provenance(selection, section, chosen, result, raftcore_dir):
    """What a selection's records came from, so a type file can say how it was made (its _sources): the sources that
    supplied a record - a git source with the commit its ref resolved to, RaftCore's records with RaftCore's version,
    a path source with its path (the records themselves are only in the file) - and the section's selection"""
    used = set(source for source, _ in result.origin.values())
    sources = OrderedDict()
    for name, (spec, _, _) in selection["sources"].items():
        if name not in used:
            continue
        if "git" in spec:
            entry = OrderedDict([("git", spec["git"])])
            for field in ("ref", "path"):
                if field in spec:
                    entry[field] = spec[field]
            if name in result.commits:
                entry["commit"] = result.commits[name]
        elif spec.get("bundled"):
            entry = OrderedDict([("bundled", True)])
            version = raftcore_version(raftcore_dir)
            if version:
                entry["raftCore"] = version
        else:
            entry = OrderedDict([("path", spec.get("path", ""))])
        sources[name] = entry
    chosen_out = OrderedDict()
    if chosen["hasInclude"]:
        chosen_out["include"] = list(chosen["include"])
    if chosen["exclude"]:
        chosen_out["exclude"] = list(chosen["exclude"])
    if chosen["policy"]:
        chosen_out["policy"] = OrderedDict(chosen["policy"])
    return OrderedDict([("formatVersion", PROVENANCE_FORMAT_VERSION), ("sources", sources), (section, chosen_out)])


################################################################################################################
# Output
################################################################################################################

def _sha256(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def _sha256_record(path, key):
    """Hash of a record: of its file, or of the record's JSON for a record from a catalogue file"""
    if os.path.basename(path) == key + ".json":
        return _sha256(path)
    records, _ = load_catalogue(path)
    return hashlib.sha256(json.dumps(records.get(key), sort_keys=True).encode("utf-8")).hexdigest()


def catalogue_text(records):
    return json.dumps({"devTypes": records}, indent=1) + "\n"


def manifest_text(result, section):
    records = OrderedDict()
    for key, (source, path) in result.origin.items():
        records[key] = OrderedDict([("source", source), ("file", os.path.abspath(path)), ("sha256", _sha256_record(path, key))])
        if source in result.commits:
            records[key]["commit"] = result.commits[source]
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
# Runtime type files (devlib.json)
################################################################################################################

# The device's default size limit for its type file (DeviceTypeFileLoader::MAX_FILE_BYTES_DEFAULT and _PSRAM): the file
# is read into one buffer, so boards with PSRAM take larger files. DevMan typeFileLimits.maxFileBytes sets another
TYPE_FILE_MAX_BYTES_DEFAULT = 65536
TYPE_FILE_MAX_BYTES_DEFAULT_PSRAM = 262144
TYPE_FILE_PATH_DEFAULT = "/local/devlib.json"


def merge_compiled(paths):
    """The records the generator compiles from its input files (ProcessDevTypeJsonToC.py): catalogue files or folders
    of records, in order; a later file's record replaces an earlier one's, and a policy-only entry ({"disable": ...})
    sets the policy of an earlier record"""
    merged = OrderedDict()
    for path in paths:
        records, _ = load_catalogue(path, record_file=False)
        for key, entry in records.items():
            if isinstance(entry, dict) and "disable" in entry and set(entry) <= {"disable", "deviceType", "_notes"}:
                if key in merged:
                    merged[key]["disable"] = entry["disable"]
                continue
            merged[key] = json.loads(json.dumps(entry), object_pairs_hook=OrderedDict)
    return merged


def _sdkconfig_psram(path):
    """CONFIG_SPIRAM in an sdkconfig file: True, False, or None if the file doesn't set it (the last setting wins)"""
    setting = None
    try:
        with open(path, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line.startswith("CONFIG_SPIRAM="):
                    setting = line == "CONFIG_SPIRAM=y"
                elif line == "# CONFIG_SPIRAM is not set":
                    setting = False
    except OSError:
        pass
    return setting


def sdkconfig_has_psram(*paths):
    """Whether sdkconfig files (in order - a later one's setting wins, as ESP-IDF merges sdkconfig.defaults) enable
    PSRAM (CONFIG_SPIRAM=y; ESP-IDF's default is no PSRAM)"""
    has_psram = False
    for path in paths:
        setting = _sdkconfig_psram(path)
        if setting is not None:
            has_psram = setting
    return has_psram


def type_file_max_bytes_default(psram):
    return TYPE_FILE_MAX_BYTES_DEFAULT_PSRAM if psram else TYPE_FILE_MAX_BYTES_DEFAULT


def selection_max_bytes_default(selection_paths):
    """The type file size limit for the SysType of a selection: from the sdkconfig.defaults next to its
    DeviceTypeSources.json files (Common's, then the SysType's)"""
    return type_file_max_bytes_default(sdkconfig_has_psram(
        *[os.path.join(os.path.dirname(os.path.abspath(p)), "sdkconfig.defaults") for p in selection_paths]))


def type_file_location(systypes_path, psram=False):
    """Where the device loads its type file, and the size limit, from a (merged) SysTypes.json (psram: whether the
    board has PSRAM, for the default limit): returns (path on the device, max bytes)"""
    config = _read_json(systypes_path) if systypes_path and os.path.isfile(systypes_path) else {}
    devman = config.get("DevMan", {}) if isinstance(config, dict) else {}
    if not isinstance(devman, dict):
        devman = {}
    if "typeFile" in devman:
        path = devman["typeFile"]
    else:
        path = config.get("DeviceLibrary", {}).get("devLibFile", TYPE_FILE_PATH_DEFAULT) \
            if isinstance(config.get("DeviceLibrary"), dict) else TYPE_FILE_PATH_DEFAULT
    default = type_file_max_bytes_default(psram)
    max_bytes = devman.get("typeFileLimits", {}).get("maxFileBytes", default) \
        if isinstance(devman.get("typeFileLimits"), dict) else default
    return path, int(max_bytes)


def fs_image_relative_path(device_path):
    """The path in the file system image of a file on the device's /local file system"""
    if not isinstance(device_path, str) or not device_path:
        raise AssembleError("typeFile.fsImage is set but the SysType has no type file (DevMan typeFile is empty)")
    parts = device_path.strip("/").split("/", 1)
    if len(parts) != 2 or parts[0] != "local" or not parts[1]:
        raise AssembleError(f"typeFile.fsImage is set but the type file {device_path} isn't on /local/, which is "
                            f"the file system image's")
    return parts[1]


def _without_notes(record):
    record = json.loads(json.dumps(record), object_pairs_hook=OrderedDict)
    record.pop("_notes", None)
    return record


def _comparable(record):
    record = _without_notes(record)
    record.pop("deviceType", None)
    return json.dumps(record, sort_keys=True)


def _response_layout(record):
    """The response layout as the firmware compares it (DeviceTypeRecords::sameResponseLayout): resp as compact JSON"""
    resp = record.get("devInfoJson", {}).get("resp")
    return None if resp is None else json.dumps(resp, separators=(",", ":"))


def build_type_file(result, compiled, max_bytes=TYPE_FILE_MAX_BYTES_DEFAULT, provenance=None):
    """A device type file from an assembled typeFile selection
    result: the Assembly of the typeFile section; compiled: the records compiled into the firmware (key -> record)
    provenance: what the file is made from, written as its top-level _sources (the device ignores it); default the
    Assembly's, False for none
    Returns (file text, entries) - entries: [{key, kind: new | override | unchanged | policy, noDecoder, decoder}].
    noDecoder: the record has no compiled decoder (a new key, or an override whose response layout changed); decoder
    then says what decodes it on the device: "runtime" (from its attributes, in firmware built with
    RAFT_RUNTIME_DECODE - the default) or "none" (custom decode code: only firmware with the code interpreter,
    RAFT_RUNTIME_DECODE_CODE, decodes it). initCode (records with their own init code): "runtime" (only firmware with
    the code interpreter can run it) or "runtime or compiled" (firmware without it uses the compiled record's). Unchanged
    records (identical to the compiled one) are left out of the file"""
    entries = []
    dev_types = OrderedDict()
    for key, record in result.records.items():
        compiled_record = compiled.get(key)
        # Init values ({name} value slots, {?name} guards) need init code: the record's own initCode (run by the code
        # interpreter in firmware built with RAFT_RUNTIME_DECODE_CODE), or a compiled record of the same key whose
        # initCode computes them (firmware without the interpreter uses that)
        own_computes, slots = _init_value_names(record)
        has_own_code = bool(record.get("initCode"))
        compiled_computes = _init_value_names(compiled_record)[0] if compiled_record else set()
        if has_own_code:
            missing = sorted(slots - own_computes)
            if missing:
                raise AssembleError(f"typeFile \"{key}\": initValues uses {', '.join(missing)}, which its initCode doesn't compute")
        elif slots:
            missing = sorted(slots - compiled_computes)
            if missing or not compiled_computes:
                raise AssembleError(f"typeFile \"{key}\": init values ({{name}} value slots, {{?name}} guards) need init code - "
                                    f"initCode in the record (needs firmware with RAFT_RUNTIME_DECODE_CODE) or a compiled {key} "
                                    f"whose initCode computes {', '.join(missing) if missing else 'them'}")
        init_code_runtime = has_own_code and not (compiled_computes and not (slots - compiled_computes))
        has_data = record.get("devInfoJson", {}).get("resp", {}).get("b", 0) > 0
        if compiled_record is not None and _comparable(record) == _comparable(compiled_record):
            entries.append(OrderedDict([("key", key), ("kind", "unchanged")]))
            continue
        if compiled_record is not None:
            no_decoder = has_data and _response_layout(record) != _response_layout(compiled_record)
            kind = "override"
        else:
            no_decoder = has_data
            kind = "new"
        entry = OrderedDict([("key", key), ("kind", kind)])
        if has_own_code:
            # The record's initCode runs in firmware with the code interpreter; "runtime": only there (no compiled
            # init code to fall back on)
            entry["initCode"] = "runtime" if init_code_runtime else "runtime or compiled"
        if no_decoder:
            entry["noDecoder"] = True
            entry["decoder"] = "none" if "c" in record.get("devInfoJson", {}).get("resp", {}) else "runtime"
        entries.append(entry)
        dev_types[key] = _without_notes(record)
    for key, policy in result.policy_only.items():
        if key not in compiled:
            raise AssembleError(f"typeFile.policy \"{key}\": not a selected record or a compiled one")
        value = POLICY_VALUES[policy]
        if value is None:
            raise AssembleError(f"typeFile.policy \"{key}\": a type file can't enable a compiled type (only disable it)")
        dev_types[key] = OrderedDict([("disable", value)])
        entries.append(OrderedDict([("key", key), ("kind", "policy"), ("policy", policy)]))
    content = OrderedDict([("devTypes", OrderedDict((k, dev_types[k]) for k in sorted(dev_types, key=lambda k: k.encode("utf-8"))))])
    if provenance is None:
        provenance = getattr(result, "provenance", None)
    if provenance:
        content["_sources"] = provenance
    text = json.dumps(content, separators=(",", ":"))
    if len(text) > max_bytes:
        raise AssembleError(f"the type file is {len(text)} bytes, more than the {max_bytes} byte limit")
    return text, entries


################################################################################################################
# The catalogue published on RaftCore's GitHub Pages site (devtypes/), read by raftjs and the Device Types app
################################################################################################################

def _record_summary(record):
    """What a catalogue lists of a record, for searching without loading every record"""
    info = record.get("devInfoJson", {}) if isinstance(record.get("devInfoJson"), dict) else {}
    resp = info.get("resp", {}) if isinstance(info.get("resp"), dict) else {}
    summary = OrderedDict()
    for field in ("name", "manu", "type", "clas"):
        if info.get(field) not in (None, "", []):
            summary[field] = info[field]
    for field in ("addresses", "hwTypeId"):
        if record.get(field):
            summary[field] = record[field]
    summary["hasData"] = isinstance(resp.get("b"), (int, float)) and resp.get("b", 0) > 0
    summary["customCode"] = "c" in resp
    summary["initCode"] = bool(record.get("initCode"))
    return summary


def build_pages(raftcore_dir, out_dir, label):
    """<out_dir>/<label>/: records/<Key>.json (each record file as it is, so its SHA-256 is the manifest's),
    catalogue.json (a summary of each record, and RaftCore's named sets) and DeviceTypeRecords.json (every record, one
    file)"""
    if not re.fullmatch(r"[A-Za-z0-9._-]+", label or ""):
        raise AssembleError(f"--label \"{label}\": letters, digits, '.', '_' and '-' only")
    records, files = load_catalogue(raftcore_records_path(raftcore_dir))
    folder = os.path.join(out_dir, label)
    records_dir = os.path.join(folder, "records")
    if os.path.isdir(folder):
        shutil.rmtree(folder)
    os.makedirs(records_dir)
    keys = sorted(records, key=lambda k: k.encode("utf-8"))
    entries = []
    for key in keys:
        path = os.path.join(records_dir, key + ".json")
        if os.path.basename(files[key]) == key + ".json":
            shutil.copyfile(files[key], path)
        else:
            with open(path, "w", encoding="utf-8") as f:
                f.write(json.dumps(records[key], indent=4) + "\n")
        entries.append(OrderedDict([("key", key), ("file", "records/" + key + ".json"), ("sha256", _sha256(path)),
                                    ("summary", _record_summary(records[key]))]))
    catalogue = OrderedDict([("formatVersion", PAGES_FORMAT_VERSION), ("label", label),
                             ("raftCore", raftcore_version(raftcore_dir)), ("recordFormat", SOURCE_FORMAT_VERSION),
                             ("count", len(entries)), ("records", entries), ("sets", load_sets(raftcore_dir))])
    with open(os.path.join(folder, "catalogue.json"), "w", encoding="utf-8") as f:
        f.write(json.dumps(catalogue, indent=1) + "\n")
    with open(os.path.join(folder, "DeviceTypeRecords.json"), "w", encoding="utf-8") as f:
        f.write(catalogue_text(OrderedDict((k, records[k]) for k in keys)))
    return len(entries)


def write_pages_index(out_dir):
    """<out_dir>/index.json: the versions published (folders with a catalogue.json) - "main" first, then releases,
    newest first - and the latest release"""
    versions = []
    for name in os.listdir(out_dir):
        path = os.path.join(out_dir, name, "catalogue.json")
        if os.path.isfile(path):
            catalogue = _read_json(path)
            versions.append(OrderedDict([("label", name), ("raftCore", catalogue.get("raftCore")),
                                         ("count", catalogue.get("count"))]))
    versions.sort(key=lambda v: (v["label"] != "main", [-n for n in _version_tuple(v["label"])], v["label"]))
    releases = [v["label"] for v in versions if re.fullmatch(r"v?\d+\.\d+\.\d+", v["label"])]
    index = OrderedDict([("formatVersion", PAGES_FORMAT_VERSION), ("latest", releases[0] if releases else None),
                         ("versions", versions)])
    with open(os.path.join(out_dir, "index.json"), "w", encoding="utf-8") as f:
        f.write(json.dumps(index, indent=1) + "\n")
    return index


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

def _build_fs_image(args):
    """--fs-image: the type file for the SysType's file system image, if typeFile.fsImage is set"""
    if not args.raftcore or not args.select:
        raise AssembleError("--fs-image needs --raftcore and --select")
    paths = [p for p in args.select.split(",") if p.strip()]
    git_options = GitOptions(args.cache_dir, args.lock, args.override_dir, False)
    result = assemble(paths, args.raftcore, "typeFile", git_options)
    if os.path.isdir(args.fs_image):
        shutil.rmtree(args.fs_image)
    inputs = list(result.inputs)
    compiled_paths = [p for p in (args.compiled or "").split(",") if p.strip()]
    compiled = merge_compiled(compiled_paths) if compiled_paths else None
    if args.compiled_out and compiled is not None:
        write_if_changed(args.compiled_out, catalogue_text(compiled))
    if result.fs_image:
        device_path, max_bytes = type_file_location(args.systypes, sdkconfig_has_psram(args.sdkconfig) if args.sdkconfig else False)
        relative = fs_image_relative_path(device_path)
        if compiled is None:
            compiled = assemble(paths, args.raftcore, "builtin", git_options).records
        text, entries = build_type_file(result, compiled, max_bytes)
        out = os.path.join(args.fs_image, relative)
        write_if_changed(out, text)
        for message in result.messages:
            print(f"DeviceTypeSources: {message}", file=sys.stderr)
        counts = ", ".join(f"{sum(1 for e in entries if e['kind'] == k)} {k}" for k in ("new", "override", "policy"))
        print(f"DeviceTypeSources: type file {device_path} in the file system image: {len(text)} bytes; {counts}",
              file=sys.stderr)
        print("fsimage:" + os.path.abspath(out))
        if args.systypes:
            inputs.append(os.path.abspath(args.systypes))
        inputs.extend(os.path.abspath(p) for p in compiled_paths)
    if args.list_inputs:
        for path in inputs:
            print(("dir:" if os.path.isdir(path) else "file:") + os.path.abspath(path))
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description="Assemble device type records (DeviceTypeSources.json)")
    parser.add_argument("--raftcore", help="RaftCore folder")
    parser.add_argument("--select", help="DeviceTypeSources.json files, comma separated (Common first)")
    parser.add_argument("--section", default="builtin", choices=SECTIONS)
    parser.add_argument("--all", action="store_true", help="every RaftCore record")
    parser.add_argument("--out", help="catalogue file to write")
    parser.add_argument("--manifest", help="manifest file to write (where each record came from)")
    parser.add_argument("--list-inputs", action="store_true", help="print the files and folders read")
    parser.add_argument("--cache-dir", help="folder for fetched git sources")
    parser.add_argument("--lock", help="lock file (written when a git source's commit changes)")
    parser.add_argument("--override-dir", help="folder of local copies that replace git sources (by source name)")
    parser.add_argument("--update", action="store_true", help="resolve git refs again and update the lock file")
    parser.add_argument("--typefile", action="store_true",
                        help="build a device type file (devlib.json) from the typeFile selection into --out")
    parser.add_argument("--compiled", help="the records compiled into the firmware, as a catalogue file (default: the "
                        "builtin selection of the same DeviceTypeSources.json)")
    parser.add_argument("--max-bytes", type=int, help="type file size limit (default: the device's for the SysType - "
                        f"{TYPE_FILE_MAX_BYTES_DEFAULT_PSRAM} if its sdkconfig.defaults enables PSRAM, else {TYPE_FILE_MAX_BYTES_DEFAULT})")
    parser.add_argument("--sdkconfig", help="with --fs-image: the SysType's (merged) sdkconfig.defaults (whether it "
                        "has PSRAM, for the default size limit)")
    parser.add_argument("--report", help="write what the type file does with each record, as JSON")
    parser.add_argument("--fs-image", help="for the build: folder to write the type file into if typeFile.fsImage "
                        "is set (at the device's type file path)")
    parser.add_argument("--systypes", help="the SysType's (merged) SysTypes.json, for --fs-image")
    parser.add_argument("--compiled-out", help="with --fs-image: write the compiled records (the --compiled files "
                        "merged) as one catalogue file, for the raft CLI")
    parser.add_argument("--split", help="catalogue file to split into one file per record")
    parser.add_argument("--out-dir", help="folder for --split")
    parser.add_argument("--pages", help="folder of the published catalogue: write RaftCore's records into "
                        "<folder>/<label>/ (with --raftcore and --label)")
    parser.add_argument("--label", help="the version folder for --pages, e.g. v1.62.0 or main")
    parser.add_argument("--pages-index", help="folder of the published catalogue: write its index.json")
    args = parser.parse_args(argv)
    try:
        if args.pages:
            if not args.raftcore:
                raise AssembleError("--pages needs --raftcore")
            count = build_pages(args.raftcore, args.pages, args.label)
            print(f"DeviceTypeSources: {count} records written to {os.path.join(args.pages, args.label)}")
            return 0
        if args.pages_index:
            index = write_pages_index(args.pages_index)
            print(f"DeviceTypeSources: index of {len(index['versions'])} versions, latest {index['latest']}")
            return 0
        if args.split:
            if not args.out_dir:
                raise AssembleError("--split needs --out-dir")
            keys = split_catalogue(args.split, args.out_dir)
            print(f"DeviceTypeSources: {len(keys)} records written to {args.out_dir}")
            return 0
        if args.fs_image:
            return _build_fs_image(args)
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
        git_options = GitOptions(args.cache_dir, args.lock, args.override_dir, args.update)
        if args.typefile:
            if args.max_bytes is None:
                args.max_bytes = selection_max_bytes_default(paths)
            result = assemble(paths, args.raftcore, "typeFile", git_options)
            if args.compiled:
                compiled, _ = load_catalogue(args.compiled, record_file=False)
            else:
                compiled = assemble(paths, args.raftcore, "builtin", git_options).records
            text, entries = build_type_file(result, compiled, args.max_bytes)
            for message in result.messages:
                print(f"DeviceTypeSources: {message}", file=sys.stderr)
            write_if_changed(args.out, text)
            if result.lock_text is not None:
                write_if_changed(args.lock, result.lock_text)
            counts = OrderedDict((kind, sum(1 for e in entries if e["kind"] == kind))
                                 for kind in ("new", "override", "unchanged", "policy"))
            runtime_decoded = [e["key"] for e in entries if e.get("decoder") == "runtime"]
            no_decoder = [e["key"] for e in entries if e.get("decoder") == "none"]
            print(f"Type file {args.out}: {len(text)} bytes (limit {args.max_bytes}); " +
                  ", ".join(f"{n} {kind}" for kind, n in counts.items()) +
                  (" (unchanged records are left out)" if counts["unchanged"] else ""))
            for entry in entries:
                note = {"runtime": " - decoded at runtime", "none": " - custom decode code (code interpreter)"}.get(entry.get("decoder"), "")
                print(f"  {entry['key']:28} {entry['kind']}{note}")
            if runtime_decoded:
                print("No compiled decoder - decoded on the device from their attributes if the firmware has runtime "
                      "decoding (RAFT_RUNTIME_DECODE, the default): " + ", ".join(runtime_decoded))
            if no_decoder:
                print("Custom decode code - decoded on the device only if the firmware has the code interpreter "
                      "(RAFT_RUNTIME_DECODE_CODE); otherwise only raftjs and the WebUI decode them: " + ", ".join(no_decoder))
            init_runtime = [e["key"] for e in entries if e.get("initCode") == "runtime"]
            if init_runtime:
                print("initCode run by the code interpreter - these records need firmware with RAFT_RUNTIME_DECODE_CODE: " +
                      ", ".join(init_runtime))
            if args.report:
                write_if_changed(args.report, json.dumps(OrderedDict([("file", os.path.abspath(args.out)),
                                 ("bytes", len(text)), ("maxBytes", args.max_bytes), ("entries", entries)]), indent=1) + "\n")
            return 0
        result = assemble(paths, args.raftcore, args.section, git_options)
        for message in result.messages:
            print(f"DeviceTypeSources: {message}", file=sys.stderr)
        write_if_changed(args.out, catalogue_text(result.records))
        if result.lock_text is not None:
            write_if_changed(args.lock, result.lock_text)
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
