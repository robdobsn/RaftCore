"""Assembling device type records from DeviceTypeSources.json (AssembleDeviceTypes.py)

- Records load from a folder (in key order), a record file or a catalogue file; a record file named differently from
  its deviceType, or a catalogue file in a folder of records, is an error.
- Selection: include defaults to everything; "<source>:<key>", "<source>:*", "*" and bare keys; exclude; a path
  source replaces another source's record (the later path source wins); the same key from two other sources is an
  error unless include names the source (M6); a named source wins over a local replacement.
- Policies are applied to selected records; errors for unknown keys, sources and policies.
- A SysType's file adds to Common's (sources by name, lists joined, its policies win), paths relative to each file.
- Output: records in key order, a manifest of where each came from; the file isn't rewritten if unchanged.
- Sources: "builtin": true is still read as "bundled": true, with a message.
- typeFile.fsImage: the build's --fs-image mode writes the type file at the device's path (from SysTypes.json) against
  the compiled files merged as the generator merges them, within the device's size limit, and writes the compiled
  records for the raft CLI; nothing is written (and an old file is removed) without fsImage.
- Splitting a catalogue keeps each record as written; RaftCore's records assembled with "raftcore:*" generate the
  same headers as the records folder.
"""

import io
import json
from collections import OrderedDict
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout

SCRIPTS_DIR = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS_DIR))
import AssembleDeviceTypes as A  # noqa: E402

RAFTCORE_DIR = SCRIPTS_DIR.parent


def record(addresses="0x10", detection="0x00=0x12", name="Test", **extra):
    rec = {"addresses": addresses, "detectionValues": detection, "initValues": "",
           "pollInfo": {"c": "0x00=r1", "i": 100, "s": 1},
           "devInfoJson": {"name": name, "resp": {"b": 1, "a": [{"n": "v", "t": "B", "o": "uint8"}]}}}
    rec.update(extra)
    return rec


class AssembleTests(unittest.TestCase):

    def setUp(self):
        self.tmp = pathlib.Path(tempfile.mkdtemp())
        # A RaftCore folder with three records
        self.raftcore = self.tmp / "RaftCore"
        self.write_records(self.raftcore / "devtypes" / "records",
                           {"Bravo": record(name="Bravo core"), "Alpha": record("0x11"), "Charlie": record("0x12")})
        self.project = self.tmp / "project"
        (self.project / "systypes" / "Common").mkdir(parents=True)
        (self.project / "systypes" / "Main").mkdir(parents=True)

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def write_records(self, folder, records):
        folder.mkdir(parents=True, exist_ok=True)
        for key, rec in records.items():
            (folder / f"{key}.json").write_text(json.dumps(rec, indent=4))

    def selection(self, content, where="Common"):
        path = self.project / "systypes" / where / "DeviceTypeSources.json"
        path.write_text(json.dumps(content))
        return str(path)

    def assemble(self, *paths, section="builtin"):
        return A.assemble(list(paths), str(self.raftcore), section)

    def assertError(self, text, *paths):
        with self.assertRaises(A.AssembleError) as ctx:
            self.assemble(*paths)
        self.assertIn(text, str(ctx.exception))

    # Loading

    def test_folder_in_key_order(self):
        records, files = A.load_catalogue(str(self.raftcore / "devtypes" / "records"))
        self.assertEqual(list(records), ["Alpha", "Bravo", "Charlie"])
        self.assertTrue(files["Alpha"].endswith("Alpha.json"))

    def test_record_and_catalogue_files(self):
        path = self.tmp / "Delta.json"
        path.write_text(json.dumps(record()))
        self.assertEqual(list(A.load_catalogue(str(path))[0]), ["Delta"])
        self.assertEqual(list(A.load_catalogue(str(path), record_file=False)[0]), [])
        cat = self.tmp / "cat.json"
        cat.write_text(json.dumps({"devTypes": {"Zed": record(), "Ann": record()}}))
        self.assertEqual(list(A.load_catalogue(str(cat))[0]), ["Zed", "Ann"])

    def test_record_file_errors(self):
        folder = self.tmp / "bad"
        self.write_records(folder, {"Echo": record(deviceType="Other")})
        with self.assertRaises(A.AssembleError) as ctx:
            A.load_catalogue(str(folder))
        self.assertIn("differs from the file name", str(ctx.exception))
        (folder / "Echo.json").write_text(json.dumps({"devTypes": {}}))
        with self.assertRaises(A.AssembleError):
            A.load_catalogue(str(folder))

    # Selection

    def test_include_defaults_to_everything(self):
        result = self.assemble(self.selection({"sources": {"raftcore": {"bundled": True}}}))
        self.assertEqual(list(result.records), ["Alpha", "Bravo", "Charlie"])

    def test_include_forms_and_exclude(self):
        sel = self.selection({"sources": {"raftcore": {"bundled": True}},
                              "builtin": {"include": ["raftcore:Bravo", "Charlie"], "exclude": []}})
        self.assertEqual(list(self.assemble(sel).records), ["Bravo", "Charlie"])
        sel = self.selection({"sources": {"raftcore": {"bundled": True}},
                              "builtin": {"include": ["raftcore:*"], "exclude": ["Alpha"]}})
        self.assertEqual(list(self.assemble(sel).records), ["Bravo", "Charlie"])
        sel = self.selection({"sources": {"raftcore": {"bundled": True}}, "builtin": {"include": []}})
        self.assertEqual(list(self.assemble(sel).records), [])

    def test_local_source_replaces(self):
        self.write_records(self.project / "devtypes", {"Bravo": record(name="Bravo local"), "Delta": record("0x13")})
        sel = self.selection({"sources": {"raftcore": {"bundled": True}, "local": {"path": "../../devtypes"}},
                              "builtin": {"include": ["*"]}})
        result = self.assemble(sel)
        self.assertEqual(list(result.records), ["Alpha", "Bravo", "Charlie", "Delta"])
        self.assertEqual(result.records["Bravo"]["devInfoJson"]["name"], "Bravo local")
        self.assertEqual(result.origin["Bravo"][0], "local")
        self.assertTrue(any("Bravo" in m and "replaces" in m for m in result.messages))
        # Naming the source wins over the local replacement
        sel = self.selection({"sources": {"raftcore": {"bundled": True}, "local": {"path": "../../devtypes"}},
                              "builtin": {"include": ["raftcore:Bravo", "local:*"]}})
        result = self.assemble(sel)
        self.assertEqual(result.records["Bravo"]["devInfoJson"]["name"], "Bravo core")
        self.assertEqual(list(result.records), ["Bravo", "Delta"])

    def test_later_path_source_wins(self):
        self.write_records(self.project / "a", {"Delta": record(name="from a")})
        self.write_records(self.project / "b", {"Delta": record(name="from b")})
        sel = self.selection({"sources": {"a": {"path": "../../a"}, "b": {"path": "../../b"}}})
        self.assertEqual(self.assemble(sel).records["Delta"]["devInfoJson"]["name"], "from b")

    def test_same_key_from_two_non_local_sources(self):
        # M6: an error unless include names the source
        sel = self.selection({"sources": {"raftcore": {"bundled": True}, "again": {"bundled": True}}})
        self.assertError("name the source to use", sel)
        sel = self.selection({"sources": {"raftcore": {"bundled": True}, "again": {"bundled": True}},
                              "builtin": {"include": ["raftcore:*"]}})
        self.assertEqual(list(self.assemble(sel).records), ["Alpha", "Bravo", "Charlie"])
        sel = self.selection({"sources": {"raftcore": {"bundled": True}, "again": {"bundled": True}},
                              "builtin": {"include": ["Alpha"]}})
        self.assertError("name the source to use", sel)
        sel = self.selection({"sources": {"raftcore": {"bundled": True}, "again": {"bundled": True}},
                              "builtin": {"include": ["raftcore:Alpha", "again:Alpha"]}})
        self.assertError("named from more than one source", sel)

    def test_errors(self):
        self.assertError("no record \"Nope\"", self.selection({"sources": {"raftcore": {"bundled": True}},
                                                                "builtin": {"include": ["Nope"]}}))
        self.assertError("no source called \"other\"", self.selection({"sources": {"raftcore": {"bundled": True}},
                                                                        "builtin": {"include": ["other:Alpha"]}}))
        self.assertError("need a cache folder",
                         self.selection({"sources": {"g": {"git": "https://example.com/x.git", "ref": "v1"}}}))
        self.assertError("\"ref\"", self.selection({"sources": {"g": {"git": "https://example.com/x.git"}}}))
        self.assertError("not found", self.selection({"sources": {"p": {"path": "missing"}}}))
        self.assertError("unknown key", self.selection({"sources": {}, "builtins": {}}))

    def test_policies(self):
        sel = self.selection({"sources": {"raftcore": {"bundled": True}},
                              "builtin": {"policy": {"Alpha": "disabled", "Bravo": "disabledClaim"}}})
        result = self.assemble(sel)
        self.assertIs(result.records["Alpha"]["disable"], True)
        self.assertEqual(result.records["Bravo"]["disable"], "claim")
        self.assertNotIn("disable", result.records["Charlie"])
        self.write_records(self.project / "devtypes", {"Delta": record(disable=True)})
        sel = self.selection({"sources": {"local": {"path": "../../devtypes"}},
                              "builtin": {"policy": {"Delta": "enabled"}}})
        self.assertNotIn("disable", self.assemble(sel).records["Delta"])
        self.assertError("not a selected record", self.selection({"sources": {"raftcore": {"bundled": True}},
                                                                   "builtin": {"include": ["Alpha"], "policy": {"Bravo": "disabled"}}}))
        self.assertError("must be one of", self.selection({"sources": {"raftcore": {"bundled": True}},
                                                            "builtin": {"policy": {"Alpha": "off"}}}))

    def test_systype_adds_to_common(self):
        self.write_records(self.project / "systypes" / "Main" / "devtypes", {"Delta": record("0x13")})
        common = self.selection({"sources": {"raftcore": {"bundled": True}},
                                 "builtin": {"include": ["raftcore:Alpha"], "policy": {"Alpha": "disabled"}}})
        main = self.selection({"sources": {"main": {"path": "devtypes"}},
                               "builtin": {"include": ["main:Delta"], "policy": {"Alpha": "enabled"}}}, "Main")
        result = self.assemble(common, main)
        self.assertEqual(list(result.records), ["Alpha", "Delta"])
        self.assertNotIn("disable", result.records["Alpha"])

    def test_sections(self):
        sel = self.selection({"sources": {"raftcore": {"bundled": True}}, "builtin": {"include": ["Alpha"]},
                              "typeFile": {"include": ["Bravo"], "fsImage": True}})
        self.assertEqual(list(self.assemble(sel, section="typeFile").records), ["Bravo"])
        self.assertTrue(self.assemble(sel, section="typeFile").fs_image)
        self.assertEqual(list(self.assemble(sel).records), ["Alpha"])

    # Output

    def test_command_line_output_and_manifest(self):
        sel = self.selection({"sources": {"raftcore": {"bundled": True}}, "builtin": {"include": ["Charlie", "Alpha"]}})
        out = self.tmp / "out" / "DeviceTypeRecords.json"
        manifest = self.tmp / "out" / "manifest.json"
        args = ["--raftcore", str(self.raftcore), "--select", sel, "--out", str(out), "--manifest", str(manifest),
                "--list-inputs"]
        stdout = io.StringIO()
        with redirect_stdout(stdout):
            self.assertEqual(A.main(args), 0)
        self.assertEqual(list(json.loads(out.read_text())["devTypes"]), ["Alpha", "Charlie"])
        lines = stdout.getvalue().splitlines()
        self.assertIn("file:" + os.path.abspath(sel), lines)
        self.assertIn("dir:" + str(self.raftcore / "devtypes" / "records"), lines)
        info = json.loads(manifest.read_text())["records"]["Alpha"]
        self.assertEqual(info["source"], "raftcore")
        self.assertEqual(len(info["sha256"]), 64)
        # Not rewritten when unchanged
        mtime = out.stat().st_mtime_ns
        with redirect_stdout(io.StringIO()):
            A.main(args)
        self.assertEqual(out.stat().st_mtime_ns, mtime)
        # An error is reported and fails
        bad = self.selection({"sources": {"raftcore": {"bundled": True}}, "builtin": {"include": ["Nope"]}})
        self.assertEqual(A.main(["--raftcore", str(self.raftcore), "--select", bad, "--out", str(out)]), 1)

    def test_split_keeps_records_as_written(self):
        text = ('{\n    "devTypes": {\n        "Zed": {\n            "addresses": "0x10",\n            "pollInfo": {\n'
                '                "c": "0x00=r1",\n                "i": 100\n            },\n            "clas": ["A"]\n'
                '        },\n        "Ann": {"addresses": "0x11"}\n    }\n}\n')
        cat = self.tmp / "cat.json"
        cat.write_text(text)
        out = self.tmp / "split"
        self.assertEqual(A.split_catalogue(str(cat), str(out)), ["Zed", "Ann"])
        self.assertEqual((out / "Zed.json").read_text(), '{\n    "addresses": "0x10",\n    "pollInfo": {\n'
                         '        "c": "0x00=r1",\n        "i": 100\n    },\n    "clas": ["A"]\n}\n')
        self.assertEqual(json.loads((out / "Ann.json").read_text()), {"addresses": "0x11"})

    # Runtime type files

    def type_file_setup(self):
        local = {"Bravo": record(name="Bravo core", initValues="0x0102"),        # override, same response layout
                 "Charlie": record("0x12"),                                     # identical to RaftCore's: unchanged
                 "Echo": record("0x14", _notes="a note")}                       # new
        echo2 = record("0x15")
        echo2["devInfoJson"]["resp"]["a"][0]["n"] = "w"
        local["Alpha"] = record("0x11")                                           # identical: unchanged
        self.write_records(self.project / "devtypes", local)
        changed_layout = record("0x12")
        changed_layout["devInfoJson"]["resp"]["a"][0]["n"] = "other"
        self.write_records(self.project / "layout", {"Charlie": changed_layout})
        return self.selection({"sources": {"raftcore": {"bundled": True}, "local": {"path": "../../devtypes"},
                                           "layout": {"path": "../../layout"}},
                               "builtin": {"include": ["raftcore:*"]},
                               "typeFile": {"include": ["local:Alpha", "local:Bravo", "local:Echo"],
                                            "policy": {"Charlie": "disabledClaim"}}})

    def test_type_file(self):
        sel = self.type_file_setup()
        result = self.assemble(sel, section="typeFile")
        compiled = self.assemble(sel).records
        text, entries = A.build_type_file(result, compiled)
        kinds = {e["key"]: (e["kind"], e.get("noDecoder", False)) for e in entries}
        self.assertEqual(kinds, {"Alpha": ("unchanged", False), "Bravo": ("override", False), "Echo": ("new", True),
                                 "Charlie": ("policy", False)})
        self.assertNotIn(": ", text, "minified")
        self.assertNotIn(", ", text, "minified")
        dev_types = json.loads(text)["devTypes"]
        self.assertEqual(list(dev_types), ["Bravo", "Charlie", "Echo"], "unchanged left out, key order")
        self.assertNotIn("_notes", dev_types["Echo"])
        self.assertEqual(dev_types["Charlie"], {"disable": "claim"})
        self.assertEqual(dev_types["Bravo"]["initValues"], "0x0102")

    def test_type_file_override_with_new_layout_has_no_decoder(self):
        sel = self.type_file_setup()
        content = json.loads(pathlib.Path(sel).read_text())
        content["typeFile"] = {"include": ["layout:Charlie"]}
        pathlib.Path(sel).write_text(json.dumps(content))
        text, entries = A.build_type_file(self.assemble(sel, section="typeFile"), self.assemble(sel).records)
        self.assertEqual(entries, [{"key": "Charlie", "kind": "override", "noDecoder": True, "decoder": "runtime"}])

    def test_type_file_decoder_kinds(self):
        # A record with no compiled decoder: decoded at runtime from its attributes, unless it has custom decode code
        import types
        std = record("0x30", name="Std")
        code = record("0x31", name="Code")
        code["devInfoJson"]["resp"]["c"] = {"n": "f", "c": "out.v=buf[0];"}
        result = types.SimpleNamespace(records=OrderedDict([("Std", std), ("Code", code)]), policy_only=OrderedDict())
        _, entries = A.build_type_file(result, {})
        self.assertEqual({e["key"]: e.get("decoder") for e in entries}, {"Std": "runtime", "Code": "none"})

    def test_type_file_errors(self):
        sel = self.type_file_setup()
        result = self.assemble(sel, section="typeFile")
        with self.assertRaises(A.AssembleError) as ctx:
            A.build_type_file(result, self.assemble(sel).records, max_bytes=100)
        self.assertIn("byte limit", str(ctx.exception))
        content = json.loads(pathlib.Path(sel).read_text())
        content["typeFile"]["policy"] = {"Charlie": "enabled"}
        pathlib.Path(sel).write_text(json.dumps(content))
        with self.assertRaises(A.AssembleError) as ctx:
            A.build_type_file(self.assemble(sel, section="typeFile"), self.assemble(sel).records)
        self.assertIn("can't enable", str(ctx.exception))
        content["typeFile"]["policy"] = {"Zulu": "disabled"}
        pathlib.Path(sel).write_text(json.dumps(content))
        with self.assertRaises(A.AssembleError) as ctx:
            A.build_type_file(self.assemble(sel, section="typeFile"), self.assemble(sel).records)
        self.assertIn("not a selected record or a compiled one", str(ctx.exception))

    def test_type_file_command_line(self):
        sel = self.type_file_setup()
        out = self.tmp / "devlib.json"
        report = self.tmp / "report.json"
        stdout = io.StringIO()
        with redirect_stdout(stdout):
            self.assertEqual(A.main(["--raftcore", str(self.raftcore), "--select", sel, "--typefile", "--out", str(out),
                                     "--report", str(report)]), 0)
        self.assertEqual(list(json.loads(out.read_text())["devTypes"]), ["Bravo", "Charlie", "Echo"])
        self.assertIn("1 new, 1 override, 1 unchanged, 1 policy", stdout.getvalue())
        self.assertIn("No compiled decoder - decoded on the device from their attributes", stdout.getvalue())
        info = json.loads(report.read_text())
        self.assertEqual(info["bytes"], len(out.read_text()))
        # --compiled: the compiled records given as a catalogue file
        compiled = self.tmp / "compiled.json"
        compiled.write_text(json.dumps({"devTypes": {"Alpha": record("0x11")}}))
        with redirect_stdout(io.StringIO()):
            self.assertEqual(A.main(["--raftcore", str(self.raftcore), "--select", sel, "--typefile", "--out", str(out),
                                     "--compiled", str(compiled)]), 1, "Charlie's policy needs Charlie compiled")

    def test_builtin_is_the_older_name_for_bundled(self):
        sel = self.selection({"sources": {"raftcore": {"builtin": True}}, "builtin": {"include": ["raftcore:Alpha"]}})
        result = self.assemble(sel)
        self.assertEqual(list(result.records), ["Alpha"])
        self.assertTrue(any('"bundled": true' in m for m in result.messages), result.messages)

    def test_fs_image_only_in_type_file(self):
        self.assertError("fsImage belongs in the typeFile section", self.selection(
            {"sources": {"raftcore": {"bundled": True}}, "builtin": {"fsImage": True}}))

    def test_merge_compiled_as_the_generator_does(self):
        first = self.tmp / "first.json"
        first.write_text(json.dumps({"devTypes": {"Alpha": record("0x11"), "Bravo": record("0x12")}}))
        self.write_records(self.tmp / "folder", {"Bravo": record("0x13", name="Bravo later")})
        policy = self.tmp / "policy.json"
        policy.write_text(json.dumps({"devTypes": {"Alpha": {"disable": "claim"}, "Nobody": {"disable": True}}}))
        merged = A.merge_compiled([str(first), str(self.tmp / "folder"), str(policy)])
        self.assertEqual(list(merged), ["Alpha", "Bravo"])
        self.assertEqual(merged["Bravo"]["devInfoJson"]["name"], "Bravo later")
        self.assertEqual(merged["Alpha"]["disable"], "claim")

    def test_type_file_location(self):
        systypes = self.tmp / "SysTypes.json"
        systypes.write_text(json.dumps({"DevMan": {}}))
        self.assertEqual(A.type_file_location(str(systypes)), ("/local/devlib.json", 65536))
        self.assertEqual(A.type_file_location(None), ("/local/devlib.json", 65536))
        systypes.write_text(json.dumps({"DeviceLibrary": {"devLibFile": "/local/lib.json"}}))
        self.assertEqual(A.type_file_location(str(systypes))[0], "/local/lib.json")
        systypes.write_text(json.dumps({"DevMan": {"typeFile": "/local/types/t.json",
                                                   "typeFileLimits": {"maxFileBytes": 2000}},
                                        "DeviceLibrary": {"devLibFile": "/local/lib.json"}}))
        self.assertEqual(A.type_file_location(str(systypes)), ("/local/types/t.json", 2000))
        self.assertEqual(A.fs_image_relative_path("/local/types/t.json"), "types/t.json")
        for bad in ("", "/sd/devlib.json", "/local/", "devlib.json"):
            with self.assertRaises(A.AssembleError):
                A.fs_image_relative_path(bad)

    def run_fs_image(self, sel, systypes_content, compiled=None):
        systypes = self.tmp / "SysTypes.merged"
        systypes.write_text(json.dumps(systypes_content))
        out_dir = self.tmp / "fsimage"
        argv = ["--raftcore", str(self.raftcore), "--select", sel, "--fs-image", str(out_dir),
                "--systypes", str(systypes), "--list-inputs"]
        if compiled:
            argv += ["--compiled", ",".join(compiled), "--compiled-out", str(self.tmp / "compiled.out.json")]
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            code = A.main(argv)
        return code, out.getvalue(), err.getvalue(), out_dir

    def test_fs_image_build(self):
        sel = self.type_file_setup()
        # fsImage not set: nothing written, and a folder left by an earlier build is removed
        (self.tmp / "fsimage").mkdir()
        (self.tmp / "fsimage" / "devlib.json").write_text("old")
        code, out, err, out_dir = self.run_fs_image(sel, {})
        self.assertEqual(code, 0, err)
        self.assertNotIn("fsimage:", out)
        self.assertFalse(out_dir.exists())
        # fsImage set: written at the device's type file path, against the compiled records given
        content = json.loads(pathlib.Path(sel).read_text())
        content["typeFile"]["fsImage"] = True
        pathlib.Path(sel).write_text(json.dumps(content))
        catalogue = self.tmp / "compiled.json"
        compiled = self.assemble(sel).records
        catalogue.write_text(A.catalogue_text(compiled))
        code, out, err, out_dir = self.run_fs_image(sel, {"DevMan": {"typeFile": "/local/types/lib.json"}},
                                                    [str(catalogue)])
        self.assertEqual(code, 0, err)
        written = out_dir / "types" / "lib.json"
        self.assertIn("fsimage:" + str(written.resolve()), out.splitlines())
        expected, _ = A.build_type_file(self.assemble(sel, section="typeFile"), compiled)
        self.assertEqual(written.read_text(), expected)
        self.assertIn("file:" + str((self.tmp / "SysTypes.merged").resolve()), out.splitlines())
        # The compiled records are written for the raft CLI
        written_compiled, _ = A.load_catalogue(str(self.tmp / "compiled.out.json"), record_file=False)
        self.assertEqual(json.dumps(written_compiled), json.dumps(compiled))
        # The device's size limit applies
        code, _, err, _ = self.run_fs_image(sel, {"DevMan": {"typeFileLimits": {"maxFileBytes": 10}}})
        self.assertEqual(code, 1)
        self.assertIn("byte limit", err)
        # A type file that isn't on /local can't go in the image
        code, _, err, _ = self.run_fs_image(sel, {"DevMan": {"typeFile": ""}})
        self.assertEqual(code, 1)
        self.assertIn("no type file", err)

    def init_code_record(self, key="Heater", slots="{heat}", code="init.heat=buf[0]+1;"):
        rec = record("0x30", name=key)
        rec["initValues"] = "0x10=r1&0x5a" + slots
        if code is not None:
            rec["initCode"] = {"n": "h", "c": code}
        return rec

    def run_generator(self, records):
        catalogue = self.tmp / "gen.json"
        catalogue.write_text(json.dumps({"devTypes": records}))
        return subprocess.run([sys.executable, str(SCRIPTS_DIR / "ProcessDevTypeJsonToC.py"), str(catalogue),
                               str(self.tmp / "t.h"), str(self.tmp / "p.h")], capture_output=True, text=True)

    def test_generator_init_values(self):
        # initCode with value slots: the init function and the value names are generated
        run = self.run_generator({"Heater": self.init_code_record()})
        self.assertEqual(run.returncode, 0, run.stderr)
        header = (self.tmp / "t.h").read_text()
        self.assertIn("int32_t* pValues", header)
        self.assertIn("if ((readBufLen < 1) || (numValues < 1)) return false;", header)
        self.assertIn("pValues[0]=buf[0]+1;", header)
        self.assertIn('R"JSON(heat)JSON"', header)
        # Value slots with no initCode, or a slot initCode doesn't compute: errors
        run = self.run_generator({"Heater": self.init_code_record(code=None)})
        self.assertNotEqual(run.returncode, 0)
        self.assertIn("no initCode", run.stderr)
        run = self.run_generator({"Heater": self.init_code_record(slots="{other}")})
        self.assertNotEqual(run.returncode, 0)
        self.assertIn("doesn't assign init.other", run.stderr)
        # initCode that isn't the decode language: reported
        run = self.run_generator({"Heater": self.init_code_record(code="init.heat=foo(1);")})
        self.assertIn("INIT CODE WARNING", run.stdout)

    def test_generator_init_guards(self):
        # A {?name} guard uses init values as a slot does: the values are computed before the first guarded step
        rec = self.init_code_record(code="init.doIt=0;if(buf[0]!=2){init.doIt=1;}")
        rec["initValues"] = "0x10=r1&0x11=r2&{?doIt}0x1202&0x13=r1"
        run = self.run_generator({"Setter": rec})
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertNotIn("WARNING", run.stdout)
        header = (self.tmp / "t.h").read_text()
        self.assertIn("if ((readBufLen < 3) || (numValues < 1)) return false;", header)
        self.assertIn('R"JSON(doIt)JSON"', header)
        # A guard initCode doesn't compute, or with no initCode: errors
        rec["initValues"] = "0x10=r1&{?other}0x1202"
        run = self.run_generator({"Setter": rec})
        self.assertIn("doesn't assign init.other", run.stderr)
        del rec["initCode"]
        run = self.run_generator({"Setter": rec})
        self.assertIn("no initCode", run.stderr)

    def test_type_file_init_values(self):
        heater = self.init_code_record()
        self.write_records(self.raftcore / "devtypes" / "records", {"Heater": heater})
        self.write_records(self.project / "devtypes", {"Heater": dict(heater, pollInfo={"c": "0x00=r1", "i": 500, "s": 1}),
                                                       "NewHeater": self.init_code_record("NewHeater")})
        base = {"sources": {"raftcore": {"bundled": True}, "local": {"path": "../../devtypes"}},
                "builtin": {"include": ["raftcore:*"]}}
        # Overriding a compiled record whose initCode computes the slot: allowed
        sel = self.selection(dict(base, typeFile={"include": ["local:Heater"]}))
        text, entries = A.build_type_file(self.assemble(sel, section="typeFile"), self.assemble(sel).records)
        self.assertEqual([e["kind"] for e in entries], ["override"])
        # The override's own initCode: run by the interpreter, or the compiled code without it
        self.assertEqual(entries[0].get("initCode"), "runtime or compiled")
        # A new key with init code: only firmware with the code interpreter runs it
        sel = self.selection(dict(base, typeFile={"include": ["local:NewHeater"]}))
        text, entries = A.build_type_file(self.assemble(sel, section="typeFile"), self.assemble(sel).records)
        self.assertEqual([(e["kind"], e.get("initCode")) for e in entries], [("new", "runtime")])
        # A value slot its initCode doesn't compute, or a slot with no init code at all: errors
        bad = self.init_code_record("BadHeater", slots="{other}")
        no_code = self.init_code_record("NoCode", code=None)
        self.write_records(self.project / "devtypes", {"BadHeater": bad, "NoCode": no_code})
        for name, message in [("BadHeater", "which its initCode doesn't compute"), ("NoCode", "need init code")]:
            sel = self.selection(dict(base, typeFile={"include": [f"local:{name}"]}))
            with self.assertRaises(A.AssembleError) as ctx:
                A.build_type_file(self.assemble(sel, section="typeFile"), self.assemble(sel).records)
            self.assertIn(message, str(ctx.exception))

    def test_init_code_checked(self):
        self.assertEqual(A.check_decode_code("H", self.init_code_record()), [])
        problems = A.check_decode_code("H", self.init_code_record(code="init.heat=1;out.x=2;"))
        self.assertTrue(any(p.startswith("initCode:") for p in problems), problems)

    def test_raftcore_records_assembled_generate_the_same_headers(self):
        sel = self.selection({"sources": {"raftcore": {"bundled": True}}, "builtin": {"include": ["raftcore:*"]}})
        result = A.assemble([sel], str(RAFTCORE_DIR), "builtin")
        assembled = self.tmp / "assembled.json"
        assembled.write_text(A.catalogue_text(result.records))
        headers = []
        for name, source in [("assembled", assembled), ("folder", RAFTCORE_DIR / "devtypes" / "records")]:
            out = self.tmp / name
            out.mkdir()
            run = subprocess.run([sys.executable, str(SCRIPTS_DIR / "ProcessDevTypeJsonToC.py"), str(source),
                                  str(out / "t.h"), str(out / "p.h")], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            headers.append(((out / "t.h").read_text(), (out / "p.h").read_text()))
        self.assertEqual(headers[0], headers[1])


if __name__ == "__main__":
    unittest.main()
