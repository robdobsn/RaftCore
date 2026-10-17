"""The decode code check (PseudocodeHandler.check_code): decode code (resp.c) is compiled into the firmware, so code
from another source may use only the decode language - buf, out.<attribute>, its own variables, the integer type
names, if/else/while/next/return and operators. RaftCore's records pass; the generator reports a problem as a warning
(records from git sources are rejected by AssembleDeviceTypes.py - test_assemble_git_sources)."""

import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

SCRIPTS_DIR = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS_DIR))
from PseudocodeHandler import PseudocodeHandler  # noqa: E402
import AssembleDeviceTypes  # noqa: E402


class DecodeCodeCheckTests(unittest.TestCase):

    def check(self, code, attrs=("v",)):
        return PseudocodeHandler().check_code(code, list(attrs))

    def test_decode_language_is_accepted(self):
        self.assertEqual(self.check("int k=0;int n=(buf[1]<<8)|buf[0];while(k<n){out.v=buf[k+2];k++;next;}"), [])
        self.assertEqual(self.check("out.v = buf[0] + (((uint16_t)buf[1])<<8); if(out.v>3){out.v=3;} else {out.v=0;}"), [])
        self.assertEqual(self.check("float f=1.5; out.v=f*buf[0];"), [])

    def test_other_code_is_reported(self):
        self.assertTrue(any("function calls" in p for p in self.check("out.v=buf[0];system(1);")))
        self.assertTrue(any("not declared" in p for p in self.check("out.v=secret;")))
        self.assertTrue(any("unexpected characters" in p for p in self.check('out.v=buf[0]; "x" #')))
        self.assertTrue(any("only out.<attribute>" in p for p in self.check("esp.restart=1;")))
        self.assertTrue(any("not an attribute" in p for p in self.check("out.other=1;")))
        self.assertTrue(any("function calls" in p for p in self.check("int f=0;out.v=f(1);")))

    def test_raftcore_records_pass(self):
        records, _ = AssembleDeviceTypes.load_catalogue(str(SCRIPTS_DIR.parent / "devtypes" / "records"))
        checked = 0
        for key, record in records.items():
            if AssembleDeviceTypes.check_decode_code(key, record) != []:
                self.fail(f"{key}: {AssembleDeviceTypes.check_decode_code(key, record)}")
            checked += "c" in record.get("devInfoJson", {}).get("resp", {})
        self.assertGreater(checked, 0)

    def test_generator_warns(self):
        record = {"addresses": "0x10", "detectionValues": "0x00=0x12", "pollInfo": {"c": "0x00=r1", "i": 100, "s": 1},
                  "devInfoJson": {"name": "T", "resp": {"b": 1, "a": [{"n": "v", "t": "B", "o": "uint8"}],
                                                       "c": {"n": "custom", "c": "out.v=buf[0];abort();"}}}}
        with tempfile.TemporaryDirectory() as tmp:
            cat = pathlib.Path(tmp) / "cat.json"
            cat.write_text(json.dumps({"devTypes": {"T": record}}))
            run = subprocess.run([sys.executable, str(SCRIPTS_DIR / "ProcessDevTypeJsonToC.py"), str(cat),
                                  str(pathlib.Path(tmp) / "t.h"), str(pathlib.Path(tmp) / "p.h")],
                                 capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertIn('DECODE CODE WARNING: record "T" resp.c: abort(...) - function calls are not allowed', run.stdout)


if __name__ == "__main__":
    unittest.main()
