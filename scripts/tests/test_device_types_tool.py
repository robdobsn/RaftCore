"""The device type tool behind `raft devtypes` (DeviceTypesTool.py)

- init creates a selection of every RaftCore record, and won't replace an existing file.
- add/remove change builtin or typeFile include; removing a record selected by a wildcard excludes it; an add that
  doesn't resolve (a key no source has) leaves the file as it was.
- new writes a record in the path source, and the record is then selected by "<source>:*".
- list reports both sections with sources, type file kinds and the no-decoder flag.
- check passes a good selection, reports a type file record's grammar error, and fails a type file that can't enable a
  compiled type.
- diff shows a local copy against the record it replaces, and a type file override against the compiled record.
- typefile writes the file the assembler builds, with the CRC the firmware reports.
- Warnings: add, list, check and typefile report a selected record's _warning (list marks it with !);
  acknowledge accepts it (the wildcard then selects the record and nothing is reported).
- --compiled (the build's DeviceTypeRecords.compiled.json) is what type files are built against when given.
"""

import io
import json
import pathlib
import shutil
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout

SCRIPTS_DIR = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS_DIR))
import DeviceTypesTool as T  # noqa: E402


def record(addresses="0x10", detection="0x00=0x12", name="Test", **extra):
    rec = {"addresses": addresses, "detectionValues": detection, "initValues": "",
           "pollInfo": {"c": "0x00=r1", "i": 100, "s": 1},
           "devInfoJson": {"name": name, "resp": {"b": 1, "a": [{"n": "v", "t": "B", "o": "uint8"}]}}}
    rec.update(extra)
    return rec


class DeviceTypesToolTests(unittest.TestCase):

    def setUp(self):
        self.tmp = pathlib.Path(tempfile.mkdtemp())
        self.raftcore = self.tmp / "RaftCore"
        self.write_records(self.raftcore / "devtypes" / "records",
                           {"Alpha": record("0x11", name="Alpha"), "Bravo": record("0x12", name="Bravo")})
        self.project = self.tmp / "project"
        (self.project / "systypes" / "Main").mkdir(parents=True)
        self.sources = self.project / "systypes" / "Main" / "DeviceTypeSources.json"

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def write_records(self, folder, records):
        folder.mkdir(parents=True, exist_ok=True)
        for key, rec in records.items():
            (folder / f"{key}.json").write_text(json.dumps(rec, indent=4))

    def write_sources(self, content):
        self.sources.write_text(json.dumps(content, indent=4))

    def read_sources(self):
        return json.loads(self.sources.read_text())

    def run_tool(self, *args, json_out=False):
        argv = ["--raftcore", str(self.raftcore), "--select", str(self.sources)] + (["--json"] if json_out else [])
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            code = T.main(argv + list(args))
        return code, out.getvalue(), err.getvalue()

    def with_local(self, builtin=None, type_file=None, local=None):
        self.write_records(self.project / "devtypes", local or {})
        content = {"sources": {"raftcore": {"bundled": True}, "local": {"path": "../../devtypes"}},
                   "builtin": builtin or {"include": ["raftcore:*"]}}
        if type_file is not None:
            content["typeFile"] = type_file
        self.write_sources(content)

    def test_init(self):
        code, out, _ = self.run_tool("init")
        self.assertEqual(code, 0, out)
        self.assertEqual(self.read_sources()["builtin"]["include"], ["raftcore:*"])
        code, _, err = self.run_tool("init")
        self.assertEqual(code, 1)
        self.assertIn("already exists", err)

    def test_add_remove(self):
        self.write_sources({"sources": {"raftcore": {"bundled": True}}, "builtin": {"include": ["raftcore:Alpha"]}})
        self.assertEqual(self.run_tool("add", "raftcore:Bravo")[0], 0)
        self.assertEqual(self.read_sources()["builtin"]["include"], ["raftcore:Alpha", "raftcore:Bravo"])
        self.assertEqual(self.run_tool("remove", "raftcore:Alpha")[0], 0)
        self.assertEqual(self.read_sources()["builtin"]["include"], ["raftcore:Bravo"])
        # typeFile include starts empty
        self.assertEqual(self.run_tool("add", "--typefile", "raftcore:Alpha")[0], 0)
        self.assertEqual(self.read_sources()["typeFile"]["include"], ["raftcore:Alpha"])

    def test_add_unknown_leaves_file(self):
        self.write_sources({"sources": {"raftcore": {"bundled": True}}, "builtin": {"include": ["raftcore:Alpha"]}})
        before = self.sources.read_text()
        code, _, err = self.run_tool("add", "raftcore:Nope")
        self.assertEqual(code, 1)
        self.assertIn("Nope", err)
        self.assertEqual(json.loads(self.sources.read_text()), json.loads(before))

    def test_remove_wildcard_excludes(self):
        self.write_sources({"sources": {"raftcore": {"bundled": True}}, "builtin": {"include": ["raftcore:*"]}})
        self.assertEqual(self.run_tool("remove", "raftcore:Bravo")[0], 0)
        self.assertEqual(self.read_sources()["builtin"]["exclude"], ["raftcore:Bravo"])
        code, out, _ = self.run_tool("list", json_out=True)
        self.assertEqual([r["key"] for r in json.loads(out)], ["Alpha"])

    def test_new(self):
        self.with_local(builtin={"include": ["raftcore:*", "local:*"]})
        code, out, _ = self.run_tool("new", "Gizmo", "--address", "0x44")
        self.assertEqual(code, 0, out)
        written = json.loads((self.project / "devtypes" / "Gizmo.json").read_text())
        self.assertEqual(written["addresses"], "0x44")
        self.assertEqual(written["devInfoJson"]["type"], "Gizmo")
        code, out, _ = self.run_tool("list", json_out=True)
        self.assertIn("Gizmo", [r["key"] for r in json.loads(out)])
        self.assertEqual(self.run_tool("new", "Gizmo")[0], 1)

    def test_list(self):
        self.with_local(type_file={"include": ["local:Charlie", "local:Alpha"], "policy": {"Bravo": "disabled"}},
                        local={"Charlie": record("0x13", name="Charlie"), "Alpha": record("0x11", name="Alpha local")})
        code, out, _ = self.run_tool("list", json_out=True)
        self.assertEqual(code, 0)
        rows = json.loads(out)
        builtin = [(r["key"], r["source"]) for r in rows if r["section"] == "builtin"]
        self.assertEqual(builtin, [("Alpha", "raftcore"), ("Bravo", "raftcore")])
        type_file = {r["key"]: r for r in rows if r["section"] == "typeFile"}
        self.assertEqual(type_file["Charlie"]["kind"], "new")
        self.assertTrue(type_file["Charlie"]["noDecoder"])
        self.assertEqual(type_file["Alpha"]["kind"], "override")
        self.assertFalse(type_file["Alpha"]["noDecoder"])
        self.assertEqual(type_file["Bravo"]["kind"], "policy")

    def test_check(self):
        self.with_local()
        code, out, _ = self.run_tool("check", json_out=True)
        self.assertEqual(code, 0, out)
        self.assertTrue(json.loads(out)["ok"])
        # A type file record with a grammar error
        self.with_local(type_file={"include": ["local:Bad"]}, local={"Bad": record("0x14", detection="0xZZ=0x12")})
        code, out, _ = self.run_tool("check", json_out=True)
        self.assertEqual(code, 1)
        self.assertTrue(any("Bad" in e for e in json.loads(out)["errors"]), out)
        # A type file can't enable a compiled type
        self.with_local(type_file={"policy": {"Alpha": "enabled"}})
        code, out, _ = self.run_tool("check", json_out=True)
        self.assertEqual(code, 1)
        self.assertTrue(any("enable" in e for e in json.loads(out)["errors"]), out)

    def test_diff(self):
        self.with_local(builtin={"include": ["raftcore:*", "local:*"]},
                        type_file={"include": ["local:Bravo"]},
                        local={"Alpha": record("0x11", name="Alpha local")})
        (self.project / "devtypes" / "Bravo.json").write_text(json.dumps(record("0x12", name="Bravo")))
        # Bravo is a local copy identical to RaftCore's, so only the builtin replacement of Alpha is shown... and
        # Bravo replaces raftcore:Bravo in builtin too, with no difference
        code, out, _ = self.run_tool("diff", "Alpha")
        self.assertEqual(code, 0)
        self.assertIn("local replaces raftcore", out)
        self.assertIn('-        "name": "Alpha"', out)
        self.assertIn('+        "name": "Alpha local"', out)
        # A type file override of a compiled record
        self.with_local(type_file={"include": ["local:Alpha"]}, local={"Alpha": record("0x11", name="Alpha local")})
        code, out, _ = self.run_tool("diff")
        self.assertIn("type file record overrides the compiled record", out)

    def test_typefile(self):
        self.with_local(type_file={"include": ["local:Charlie"], "policy": {"Bravo": "disabledClaim"}},
                        local={"Charlie": record("0x13", name="Charlie")})
        out_path = self.tmp / "devlib.json"
        code, out, _ = self.run_tool("typefile", "--out", str(out_path), json_out=True)
        self.assertEqual(code, 0, out)
        report = json.loads(out)
        text = out_path.read_text()
        self.assertEqual(json.loads(text)["devTypes"]["Bravo"], {"disable": "claim"})
        self.assertIn("Charlie", json.loads(text)["devTypes"])
        self.assertEqual(report["bytes"], len(text))
        self.assertEqual(report["crc"], "%04x" % T.crc16(text.encode()))
        self.assertEqual([e["kind"] for e in report["entries"]], ["new", "policy"])

    def test_compiled_records_from_the_build(self):
        # The build compiles a record the selection doesn't have (e.g. from DevTypes.json): with --compiled the type
        # file's identical copy is unchanged (left out), without it the record is new
        self.with_local(type_file={"include": ["local:Delta"]}, local={"Delta": record("0x14", name="Delta")})
        compiled = self.tmp / "DeviceTypeRecords.compiled.json"
        compiled.write_text(json.dumps({"devTypes": {"Alpha": record("0x11", name="Alpha"),
                                                     "Delta": record("0x14", name="Delta")}}))
        out_path = self.tmp / "devlib.json"
        code, out, _ = self.run_tool("typefile", "--out", str(out_path), json_out=True)
        self.assertEqual([e["kind"] for e in json.loads(out)["entries"]], ["new"])
        self.assertEqual(json.loads(out)["compiled"], "builtin")
        code, out, _ = self.run_tool("--compiled", str(compiled), "typefile", "--out", str(out_path), json_out=True)
        self.assertEqual(code, 0, out)
        self.assertEqual([e["kind"] for e in json.loads(out)["entries"]], ["unchanged"])
        self.assertEqual(json.loads(out)["compiled"], str(compiled.resolve()))
        code, out, _ = self.run_tool("--compiled", str(compiled), "list", json_out=True)
        self.assertEqual([r["kind"] for r in json.loads(out) if r["section"] == "typeFile"], ["unchanged"])

    def test_warnings_and_acknowledge(self):
        self.write_records(self.raftcore / "devtypes" / "records", {"Wake": record("0x0c", _warning="Sends a general call")})
        self.write_sources({"sources": {"raftcore": {"bundled": True}}, "builtin": {"include": ["raftcore:Alpha"]}})
        code, out, _ = self.run_tool("add", "raftcore:Wake")
        self.assertEqual(code, 0)
        self.assertIn("WARNING Wake: Sends a general call", out)
        code, out, _ = self.run_tool("list")
        self.assertIn("! Wake", out)
        self.assertIn("WARNING Wake: Sends a general call", out)
        _, out, _ = self.run_tool("list", json_out=True)
        self.assertEqual({r["key"]: r["warning"] for r in json.loads(out)}["Wake"], "Sends a general call")
        _, out, _ = self.run_tool("check", json_out=True)
        self.assertTrue(any("Wake: Sends a general call" in w for w in json.loads(out)["warnings"]))
        code, out, _ = self.run_tool("acknowledge", "Wake")
        self.assertEqual(code, 0)
        self.assertEqual(self.read_sources()["acknowledge"], ["Wake"])
        _, out, _ = self.run_tool("list")
        self.assertNotIn("WARNING", out)

    def test_crc16(self):
        # CRC16-CCITT (init 0xffff, poly 0x1021): the standard check value
        self.assertEqual(T.crc16(b"123456789"), 0x29B1)


if __name__ == "__main__":
    unittest.main()
