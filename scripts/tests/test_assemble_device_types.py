"""Assembling device type records from DeviceTypeSources.json (AssembleDeviceTypes.py)

- Records load from a folder (in key order), a record file or a catalogue file; a record file named differently from
  its deviceType, or a catalogue file in a folder of records, is an error.
- Selection: include defaults to everything; "<source>:<key>", "<source>:*", "*" and bare keys; exclude; a path
  source replaces another source's record (the later path source wins); the same key from two other sources is an
  error unless include names the source (M6); a named source wins over a local replacement.
- Policies are applied to selected records; errors for unknown keys, sources and policies.
- A SysType's file adds to Common's (sources by name, lists joined, its policies win), paths relative to each file.
- Output: records in key order, a manifest of where each came from; the file isn't rewritten if unchanged.
- Splitting a catalogue keeps each record as written; RaftCore's records assembled with "raftcore:*" generate the
  same headers as the records folder.
"""

import io
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stdout

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
        result = self.assemble(self.selection({"sources": {"raftcore": {"builtin": True}}}))
        self.assertEqual(list(result.records), ["Alpha", "Bravo", "Charlie"])

    def test_include_forms_and_exclude(self):
        sel = self.selection({"sources": {"raftcore": {"builtin": True}},
                              "builtin": {"include": ["raftcore:Bravo", "Charlie"], "exclude": []}})
        self.assertEqual(list(self.assemble(sel).records), ["Bravo", "Charlie"])
        sel = self.selection({"sources": {"raftcore": {"builtin": True}},
                              "builtin": {"include": ["raftcore:*"], "exclude": ["Alpha"]}})
        self.assertEqual(list(self.assemble(sel).records), ["Bravo", "Charlie"])
        sel = self.selection({"sources": {"raftcore": {"builtin": True}}, "builtin": {"include": []}})
        self.assertEqual(list(self.assemble(sel).records), [])

    def test_local_source_replaces(self):
        self.write_records(self.project / "devtypes", {"Bravo": record(name="Bravo local"), "Delta": record("0x13")})
        sel = self.selection({"sources": {"raftcore": {"builtin": True}, "local": {"path": "../../devtypes"}},
                              "builtin": {"include": ["*"]}})
        result = self.assemble(sel)
        self.assertEqual(list(result.records), ["Alpha", "Bravo", "Charlie", "Delta"])
        self.assertEqual(result.records["Bravo"]["devInfoJson"]["name"], "Bravo local")
        self.assertEqual(result.origin["Bravo"][0], "local")
        self.assertTrue(any("Bravo" in m and "replaces" in m for m in result.messages))
        # Naming the source wins over the local replacement
        sel = self.selection({"sources": {"raftcore": {"builtin": True}, "local": {"path": "../../devtypes"}},
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
        sel = self.selection({"sources": {"raftcore": {"builtin": True}, "again": {"builtin": True}}})
        self.assertError("name the source to use", sel)
        sel = self.selection({"sources": {"raftcore": {"builtin": True}, "again": {"builtin": True}},
                              "builtin": {"include": ["raftcore:*"]}})
        self.assertEqual(list(self.assemble(sel).records), ["Alpha", "Bravo", "Charlie"])
        sel = self.selection({"sources": {"raftcore": {"builtin": True}, "again": {"builtin": True}},
                              "builtin": {"include": ["Alpha"]}})
        self.assertError("name the source to use", sel)
        sel = self.selection({"sources": {"raftcore": {"builtin": True}, "again": {"builtin": True}},
                              "builtin": {"include": ["raftcore:Alpha", "again:Alpha"]}})
        self.assertError("named from more than one source", sel)

    def test_errors(self):
        self.assertError("no record \"Nope\"", self.selection({"sources": {"raftcore": {"builtin": True}},
                                                                "builtin": {"include": ["Nope"]}}))
        self.assertError("no source called \"other\"", self.selection({"sources": {"raftcore": {"builtin": True}},
                                                                        "builtin": {"include": ["other:Alpha"]}}))
        self.assertError("git sources are not supported yet",
                         self.selection({"sources": {"g": {"git": "https://example.com/x.git", "ref": "v1"}}}))
        self.assertError("not found", self.selection({"sources": {"p": {"path": "missing"}}}))
        self.assertError("unknown key", self.selection({"sources": {}, "builtins": {}}))

    def test_policies(self):
        sel = self.selection({"sources": {"raftcore": {"builtin": True}},
                              "builtin": {"policy": {"Alpha": "disabled", "Bravo": "disabledClaim"}}})
        result = self.assemble(sel)
        self.assertIs(result.records["Alpha"]["disable"], True)
        self.assertEqual(result.records["Bravo"]["disable"], "claim")
        self.assertNotIn("disable", result.records["Charlie"])
        self.write_records(self.project / "devtypes", {"Delta": record(disable=True)})
        sel = self.selection({"sources": {"local": {"path": "../../devtypes"}},
                              "builtin": {"policy": {"Delta": "enabled"}}})
        self.assertNotIn("disable", self.assemble(sel).records["Delta"])
        self.assertError("not a selected record", self.selection({"sources": {"raftcore": {"builtin": True}},
                                                                   "builtin": {"include": ["Alpha"], "policy": {"Bravo": "disabled"}}}))
        self.assertError("must be one of", self.selection({"sources": {"raftcore": {"builtin": True}},
                                                            "builtin": {"policy": {"Alpha": "off"}}}))

    def test_systype_adds_to_common(self):
        self.write_records(self.project / "systypes" / "Main" / "devtypes", {"Delta": record("0x13")})
        common = self.selection({"sources": {"raftcore": {"builtin": True}},
                                 "builtin": {"include": ["raftcore:Alpha"], "policy": {"Alpha": "disabled"}}})
        main = self.selection({"sources": {"main": {"path": "devtypes"}},
                               "builtin": {"include": ["main:Delta"], "policy": {"Alpha": "enabled"}}}, "Main")
        result = self.assemble(common, main)
        self.assertEqual(list(result.records), ["Alpha", "Delta"])
        self.assertNotIn("disable", result.records["Alpha"])

    def test_sections(self):
        sel = self.selection({"sources": {"raftcore": {"builtin": True}}, "builtin": {"include": ["Alpha"]},
                              "typeFile": {"include": ["Bravo"], "fsImage": True}})
        self.assertEqual(list(self.assemble(sel, section="typeFile").records), ["Bravo"])
        self.assertTrue(self.assemble(sel, section="typeFile").fs_image)
        self.assertEqual(list(self.assemble(sel).records), ["Alpha"])

    # Output

    def test_command_line_output_and_manifest(self):
        sel = self.selection({"sources": {"raftcore": {"builtin": True}}, "builtin": {"include": ["Charlie", "Alpha"]}})
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
        bad = self.selection({"sources": {"raftcore": {"builtin": True}}, "builtin": {"include": ["Nope"]}})
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

    def test_raftcore_records_assembled_generate_the_same_headers(self):
        sel = self.selection({"sources": {"raftcore": {"builtin": True}}, "builtin": {"include": ["raftcore:*"]}})
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
