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
- include/exclude: "*", "<source>:*", "<source>:<key>" or "<key>" (a key only one source has). builtin's include
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
    AssembleDeviceTypes.py --raftcore <dir> --select <file>[,<file>] --typefile --out <devlib.json>
                           [--compiled <catalogue.json>] [--max-bytes <n>] [--report <report.json>]
        A device type file for the device's file system from the typeFile selection: minified, without _notes; each
        record is new, an override of a compiled record or unchanged (left out); typeFile policies for compiled types
        become {"disable": ...} entries. Records with no on-device decoder are listed. The compiled records are the
        builtin selection of the same files unless --compiled gives them

    AssembleDeviceTypes.py --raftcore <dir> --select <file>[,<file>] --fs-image <dir> --systypes <SysTypes.json>
                           --compiled <file or folder>[,...] [--compiled-out <catalogue.json>]
                           [--cache-dir ...] [--lock ...] [--override-dir ...]
        For the build: if typeFile.fsImage is true, the type file is written into <dir> at the path the device loads it
        from (DevMan typeFile, else DeviceLibrary devLibFile, else /local/devlib.json, which must be on /local/), within
        DevMan typeFileLimits.maxFileBytes; the compiled records are the generator's input files, merged as it merges
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
LEGACY_CATALOGUE = os.path.join("devtypes", "DeviceTypeRecords.json")
POLICY_VALUES = {"enabled": None, "disabled": True, "disabledClaim": "claim"}
SECTIONS = ("builtin", "typeFile")
LOCK_FORMAT_VERSION = 1
SOURCE_FORMAT_VERSION = 1
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
    """Problems in a record's decode code (resp.c) - see PseudocodeHandler.check_code"""
    resp = record.get("devInfoJson", {}).get("resp", {}) if isinstance(record, dict) else {}
    custom = resp.get("c")
    if not isinstance(custom, dict) or not custom.get("c"):
        return []
    attrs = [attr.get("n", "") for attr in resp.get("a", [])]
    return PseudocodeHandler().check_code(custom["c"], attrs)


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
    names = list(sources)
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
    return result


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

TYPE_FILE_MAX_BYTES_DEFAULT = 65536
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


def type_file_location(systypes_path):
    """Where the device loads its type file, and the size limit, from a (merged) SysTypes.json:
    returns (path on the device, max bytes)"""
    config = _read_json(systypes_path) if systypes_path and os.path.isfile(systypes_path) else {}
    devman = config.get("DevMan", {}) if isinstance(config, dict) else {}
    if not isinstance(devman, dict):
        devman = {}
    if "typeFile" in devman:
        path = devman["typeFile"]
    else:
        path = config.get("DeviceLibrary", {}).get("devLibFile", TYPE_FILE_PATH_DEFAULT) \
            if isinstance(config.get("DeviceLibrary"), dict) else TYPE_FILE_PATH_DEFAULT
    max_bytes = devman.get("typeFileLimits", {}).get("maxFileBytes", TYPE_FILE_MAX_BYTES_DEFAULT) \
        if isinstance(devman.get("typeFileLimits"), dict) else TYPE_FILE_MAX_BYTES_DEFAULT
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


def build_type_file(result, compiled, max_bytes=TYPE_FILE_MAX_BYTES_DEFAULT):
    """A device type file from an assembled typeFile selection
    result: the Assembly of the typeFile section; compiled: the records compiled into the firmware (key -> record)
    Returns (file text, entries) - entries: [{key, kind: new | override | unchanged | policy, noDecoder}]. Unchanged
    records (identical to the compiled one) are left out of the file"""
    entries = []
    dev_types = OrderedDict()
    for key, record in result.records.items():
        compiled_record = compiled.get(key)
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
        if no_decoder:
            entry["noDecoder"] = True
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
    text = json.dumps({"devTypes": OrderedDict((k, dev_types[k]) for k in sorted(dev_types, key=lambda k: k.encode("utf-8")))},
                      separators=(",", ":"))
    if len(text) > max_bytes:
        raise AssembleError(f"the type file is {len(text)} bytes, more than the {max_bytes} byte limit")
    return text, entries


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
        device_path, max_bytes = type_file_location(args.systypes)
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
    parser.add_argument("--max-bytes", type=int, default=TYPE_FILE_MAX_BYTES_DEFAULT, help="type file size limit")
    parser.add_argument("--report", help="write what the type file does with each record, as JSON")
    parser.add_argument("--fs-image", help="for the build: folder to write the type file into if typeFile.fsImage "
                        "is set (at the device's type file path)")
    parser.add_argument("--systypes", help="the SysType's (merged) SysTypes.json, for --fs-image")
    parser.add_argument("--compiled-out", help="with --fs-image: write the compiled records (the --compiled files "
                        "merged) as one catalogue file, for the raft CLI")
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
            no_decoder = [e["key"] for e in entries if e.get("noDecoder")]
            print(f"Type file {args.out}: {len(text)} bytes (limit {args.max_bytes}); " +
                  ", ".join(f"{n} {kind}" for kind, n in counts.items()) +
                  (" (unchanged records are left out)" if counts["unchanged"] else ""))
            for entry in entries:
                print(f"  {entry['key']:28} {entry['kind']}{' - no on-device decoder' if entry.get('noDecoder') else ''}")
            if no_decoder:
                print("No on-device decoder (DataLogger, RaftROS and named values won't decode them; raftjs and the WebUI "
                      "will): " + ", ".join(no_decoder))
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
