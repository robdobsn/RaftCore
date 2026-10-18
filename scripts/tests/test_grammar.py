"""Device type record command grammar (scripts/DeviceTypeGrammar.py)

- The shared test vectors (grammar_vectors.json), which the C++ DeviceTypeGrammar is also tested against.
- Fuzzing against the C++ implementation: the same random strings compile to the same steps and issues (when
  the C++ test program has been built, as "make check" in linux_unit_tests does).
- The shipped RaftCore catalogue compiles without errors.
"""

import json
import pathlib
import random
import subprocess
import sys
import unittest

SCRIPTS_DIR = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS_DIR))
import DeviceTypeGrammar  # noqa: E402

VECTORS = pathlib.Path(__file__).resolve().parent / "grammar_vectors.json"
CPP_TEST = SCRIPTS_DIR.parent / "linux_unit_tests" / "test_device_type_grammar"
RAFTCORE_CATALOGUE = SCRIPTS_DIR.parent / "devtypes" / "records"
import AssembleDeviceTypes  # noqa: E402


def canonical(result):
    """The same one-line form as test_device_type_grammar --dump"""
    out = ""
    for step in result.steps:
        out += f"[w{step.write.hex()} r{step.read} p{step.pause}" + (" crc" if step.crc8w else "") + \
               (" expr" if step.expr else "")
        for mask, value in step.checks:
            out += f" {mask.hex()}:{value.hex()}"
        for offset, name in step.slots:
            out += f" slot{offset}:{name}"
        out += "]"
    out += " |"
    for code, pos in result.issues:
        out += f" {code}@{pos}"
    return out


class GrammarTests(unittest.TestCase):

    def test_vectors(self):
        vectors = json.loads(VECTORS.read_text())["vectors"]
        self.assertGreater(len(vectors), 20)
        for vec in vectors:
            with self.subTest(vec["name"]):
                result = DeviceTypeGrammar.compile(vec["str"], vec["field"])
                self.assertEqual([code for code, _ in result.issues], vec["issues"])
                expected = [dict(step, checks=step.get("checks", []) or [], crc8w=step.get("crc8w", False),
                                 expr=step.get("expr", False), slots=step.get("slots", [])) for step in vec["steps"]]
                got = [{"write": s.write.hex(), "read": s.read, "pause": s.pause,
                        "checks": [m.hex() + ":" + v.hex() for m, v in s.checks], "crc8w": s.crc8w, "expr": s.expr,
                        "slots": [f"{offset}:{name}" for offset, name in s.slots]}
                       for s in result.steps]
                for e in expected:
                    e.setdefault("checks", [])
                self.assertEqual(got, expected)

    def test_crc(self):
        self.assertEqual(DeviceTypeGrammar.crc8(b"\xbe\xef"), 0x92)

    def test_raftcore_catalogue_has_no_errors(self):
        catalogue, _ = AssembleDeviceTypes.load_catalogue(str(RAFTCORE_CATALOGUE))
        errors = []
        for key, dev_type in catalogue.items():
            for field, result in DeviceTypeGrammar.check_record(dev_type):
                errors += [f"{key} {field} {code}@{pos}" for code, pos in result.issues
                           if DeviceTypeGrammar.is_error(code)]
        self.assertEqual(errors, [])

    @unittest.skipUnless(CPP_TEST.exists(), "C++ grammar test not built (make -C linux_unit_tests)")
    def test_fuzz_against_cpp(self):
        rng = random.Random(4321)
        alphabet = "0123456789abcdefABCDEFxXbBpPrR=&;, {}$.:wmask*"
        tokens = ["0x", "0b", "=", "&", ";", ",", "p10", "r2", "0x0f", "0b1x0x", "=p25", "r{$0.w12:max8}", ":crc8w",
                  " ", "r{$1[2].W8:mask0F*2+1:max64}", "p70000", "r99999"]
        cases = []
        for _ in range(30000):
            s = ""
            for _ in range(rng.randrange(24)):
                s += rng.choice(tokens) if rng.randrange(3) == 0 else rng.choice(alphabet)
            cases.append((rng.choice(DeviceTypeGrammar.FIELDS), s))
        stdin = "".join(f"{field}\t{s}\n" for field, s in cases)
        out = subprocess.run([str(CPP_TEST), "--dump"], input=stdin, capture_output=True, text=True, check=True)
        cpp_lines = out.stdout.split("\n")
        mismatches = []
        for (field, s), cpp_line in zip(cases, cpp_lines):
            py_line = canonical(DeviceTypeGrammar.compile(s, field))
            if py_line != cpp_line:
                mismatches.append(f"{field} {s!r}\n  C++    {cpp_line}\n  Python {py_line}")
        self.assertEqual(len(cpp_lines) - 1, len(cases))
        self.assertEqual(mismatches[:5], [])


if __name__ == "__main__":
    unittest.main()
