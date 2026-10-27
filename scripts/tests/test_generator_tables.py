"""Tables emitted by the device type record generator (ProcessDevTypeJsonToC.py)

- The record array is const (so it is in flash, not RAM). The address map and scan lists are not generated: the
  firmware builds them at runtime from the records' fields (scanPriority, flags, identPriority, hwTypeId), which
  are emitted with each record.
- baseDevTypeIndexByKey lists the record indexes sorted by key in byte order (as strcmp), for binary search.
- A first record with no addresses doesn't crash the generator (C6).
- An address above 0x77 is an error rather than silently dropped (C12).
- A 0x... read value in poll info counts its hex digits, not the 0x prefix, towards the poll size (C11).
- A record's generalCall is emitted after its init fields; an invalid one is an error.
- Warnings: a key in more than one file; a record with no detection values ahead of another at its address; a
  hardware type ID used by more than one record (N2).
"""

import json
import pathlib
import re
import subprocess
import sys
import tempfile
import unittest


SCRIPTS_DIR = pathlib.Path(__file__).resolve().parents[1]
GENERATOR = SCRIPTS_DIR / "ProcessDevTypeJsonToC.py"
sys.path.insert(0, str(SCRIPTS_DIR))
from DecodeGenerator import DecodeGenerator  # noqa: E402


def make_record(addresses="0x10", poll_c="0x00=r1", resp_bytes=1):
    """A minimal valid record"""
    return {"addresses": addresses, "detectionValues": "0x00=0x01", "initValues": "",
            "pollInfo": {"c": poll_c, "i": 100, "s": 1},
            "devInfoJson": {"name": "Test", "desc": "Test", "manu": "Test",
                            "resp": {"b": resp_bytes,
                                     "a": [{"n": "v", "t": "B", "u": "", "r": [0, 255], "f": "d", "o": "uint8"}]}}}


class GeneratorTableTests(unittest.TestCase):

    def run_with_records(self, records, more_records=None):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = pathlib.Path(tmp)
            cat = tmp_path / "catalogue.json"
            cat.write_text(json.dumps({"devTypes": records}))
            cat_arg = str(cat)
            if more_records is not None:
                cat2 = tmp_path / "catalogue2.json"
                cat2.write_text(json.dumps({"devTypes": more_records}))
                cat_arg = f"[{cat},{cat2}]"
            result = subprocess.run(
                [sys.executable, str(GENERATOR), cat_arg,
                 str(tmp_path / "DeviceTypeRecords_generated.h"), str(tmp_path / "DevicePollRecords_generated.h")],
                capture_output=True, text=True)
            header_path = tmp_path / "DeviceTypeRecords_generated.h"
            header = header_path.read_text() if header_path.exists() else ""
            return result, header

    def test_tables_are_const(self):
        result, header = self.run_with_records({"A": make_record()})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("static const DeviceTypeRecord baseDevTypeRecords[]", header)
        self.assertNotRegex(header, r"static (uint16_t|DeviceTypeRecord)[ *]")

    def test_derived_tables_not_generated(self):
        result, header = self.run_with_records({"A": make_record()})
        self.assertEqual(result.returncode, 0, result.stderr)
        for name in ["baseDevTypeIndexByAddr", "baseDevTypeCountByAddr", "scanPriorityLists", "BASE_DEV_INDEX_BY_ARRAY"]:
            self.assertNotIn(name, header)

    @staticmethod
    def ident_fields(header):
        """The scanPriority, flags, identPriority, hwTypeId (a string, or None for nullptr) emitted at the end of each
        record (before the policy)"""
        return [(int(a), int(b), int(c), None if d == "nullptr" else json.loads(d)) for a, b, c, d, _ in
                re.findall(r',\n        (-?\d+), (\d+), (-?\d+), (nullptr|"(?:[^"\\]|\\.)*"), (\d+)\n    \},', header)]

    @staticmethod
    def policies(header):
        """The initial policy emitted at the end of each record"""
        return [int(p) for p in re.findall(r', (?:nullptr|"(?:[^"\\]|\\.)*"), (\d+)\n    \},', header)]

    def test_disable_entries(self):
        # A record can be disabled in its own body, or by a policy-only entry in a later file (D23)
        result, header = self.run_with_records(
            {"A": make_record(), "B": dict(make_record(), disable=True), "C": make_record(), "D": make_record()},
            {"C": {"disable": "claim"}, "D": {"disable": True}, "Missing": {"disable": True}})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.record_keys(header), ["A", "B", "C", "D"])
        self.assertEqual(self.policies(header), [0, 1, 2, 1])
        self.assertIn('"Missing" sets a policy but no earlier file has a record with that key', result.stdout)
        result, _ = self.run_with_records({"Bad": dict(make_record(), disable="yes")})
        self.assertNotEqual(result.returncode, 0)

    @staticmethod
    def record_keys(header):
        return re.findall(r'\{\n        R"JSON\(([^)]*)\)JSON"', header)

    def test_ident_fields(self):
        records = {
            "Plain": make_record(),
            "High": dict(make_record(), scanPriority="high"),
            "Low3": dict(make_record(), scanPriority=3),
            "Probe": dict(make_record(addresses=""), excludeFromAddrMap=True, scanPriority="high", hwTypeId="rsao:0x0084"),
            "ProbeNew": dict(make_record(), probeOnly=True),
            "Ident": dict(make_record(), identPriority=-5, hwTypeId='other:"x"\\y'),
            "NoScan": dict(make_record(), scanWhilePolled=False),
            "Scan": dict(make_record(), scanWhilePolled=True),
        }
        result, header = self.run_with_records(records)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.ident_fields(header), [
            (255, 0, 0, None), (0, 0, 0, None), (2, 0, 0, None), (0, 1, 0, "rsao:0x0084"), (255, 1, 0, None),
            (255, 0, -5, 'other:"x"\\y'), (255, 4, 0, None), (255, 0, 0, None)])

    def test_bad_ident_fields_are_errors(self):
        for bad in [{"hwTypeId": 132}, {"hwTypeId": "0x0084"}, {"hwTypeId": "rsao:"}, {"hwTypeId": "RSAO:0x0084"},
                    {"hwTypeId": "rsao:0x 84"}, {"hwTypeId": "rsao:" + "x" * 60}, {"identPriority": 200},
                    {"identPriority": "1"}, {"scanWhilePolled": "no"}, {"scanWhilePolled": 0}]:
            result, _ = self.run_with_records({"Bad": dict(make_record(), **bad)})
            self.assertNotEqual(result.returncode, 0, bad)

    def test_general_call(self):
        # Emitted after the init fields (nullptr, nullptr for a record without initCode); a record without one ends
        # at its policy as before
        result, header = self.run_with_records({"Plain": make_record(), "Wake": dict(make_record(), generalCall="0x04"),
                                                "Two": dict(make_record(), generalCall="0x04aB")})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(re.findall(r', (\d+),\n        nullptr, nullptr,\n        "([^"]*)"\n    \},', header),
                         [("0", "0x04"), ("0", "0x04aB")])
        self.assertEqual(self.policies(header), [0])
        # Invalid: not a string, no 0x, odd or no digits, over 4 bytes, not hex, or starting with 0x00 or 0x06 (reset)
        for bad in [4, "04", "0x4", "0x", "0x0404040404", "0x0g", "0x00", "0x0604"]:
            result, _ = self.run_with_records({"Bad": dict(make_record(), generalCall=bad)})
            self.assertNotEqual(result.returncode, 0, bad)
            self.assertIn("generalCall", result.stderr)

    def test_device_type_id_is_an_error_naming_hw_type_id(self):
        # The old field: its values had no scheme, so it is refused with a message naming the new field (M14)
        result, _ = self.run_with_records({"Old": dict(make_record(), deviceTypeId="0x0084")})
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('"Old" has deviceTypeId - it is now hwTypeId', result.stderr)

    def test_hw_probe_effects_checked_and_not_compiled_in(self):
        # Valid: accepted, any number of schemes, and nothing of it in the generated tables (only tooling reads it)
        good = dict(make_record(), hwProbeEffects={"rsao": {"effect": "readEffect", "basis": "source", "note": "pops a FIFO"},
                                                   "other-2": {"effect": "none"}})
        result, header = self.run_with_records({"Good": good})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("pops a FIFO", header)
        self.assertNotIn("hwProbeEffects", header)
        # Invalid: refused, naming the problem
        bad_cases = [
            ("none", "must be an object keyed by scheme"),
            ({"RSAO": {"effect": "none"}}, "scheme 'RSAO' must be"),
            ({"rsao": {"basis": "bench"}}, "rsao effect must be one of"),
            ({"rsao": {"effect": "fine"}}, "rsao effect must be one of"),
            ({"rsao": {"effect": "none", "basis": "guess"}}, "rsao basis must be one of"),
            ({"rsao": {"effect": "none", "note": 1}}, "rsao note must be a string"),
            ({"rsao": {"effect": "none", "efect": "x"}}, "rsao has unknown field(s) efect"),
        ]
        for value, expected in bad_cases:
            result, _ = self.run_with_records({"Bad": dict(make_record(), hwProbeEffects=value)})
            self.assertNotEqual(result.returncode, 0, value)
            self.assertIn('"Bad" hwProbeEffects ' + expected, result.stderr)

    def test_duplicate_key_across_files_warns(self):
        result, header = self.run_with_records({"Dup": make_record()}, {"Dup": make_record(addresses="0x11")})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('record "Dup" replaces a record with the same key', result.stdout)

    def test_duplicate_hw_type_id_warns(self):
        # Two records with one hardware type ID: lookups by the ID find only the first (N2)
        # The same value in two schemes, or written in two forms, is two IDs: matching is exact
        a = dict(make_record("0x10"), hwTypeId="rsao:0x0084")
        b = dict(make_record("0x11"), hwTypeId="rsao:0x0084")
        c = dict(make_record("0x12"), hwTypeId="rsao:0x0085")
        d = dict(make_record("0x13"), hwTypeId="other:0x0084")
        e = dict(make_record("0x14"), hwTypeId="rsao:0x84")
        result, _ = self.run_with_records({"A": a, "B": b, "C": c, "D": d, "E": e})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("hwTypeId rsao:0x0084 is used by A, B - lookups by hardware type ID return A", result.stderr)
        self.assertEqual(result.stderr.count("is used by"), 1, result.stderr)

    def test_record_bus_speed_warns(self):
        # The poll bus speed is a choice for the board (DevMan typeSettings), no longer part of a record
        rec = make_record()
        rec["pollInfo"]["busHz"] = 400000
        rec["pollInfo"]["busHzSlots"] = [1, 2]
        result, _ = self.run_with_records({"A": rec})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('record "A" pollInfo.busHz is no longer used', result.stdout)
        self.assertIn('record "A" pollInfo.busHzSlots is no longer used', result.stdout)

    def test_shadowing_warning(self):
        # A record that compares nothing is tried after records that compare something - unless its identPriority
        # is higher, or the other record also compares nothing
        records = {"NoDetect": dict(make_record(), detectionValues=""), "Later": make_record()}
        result, _ = self.run_with_records(records)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("has no detection values", result.stdout)
        records = {"NoDetect": dict(make_record(), detectionValues="", identPriority=1), "Later": make_record()}
        result, _ = self.run_with_records(records)
        self.assertIn('record "NoDetect" has no detection values, so at address 0x10 it is matched before Later',
                      result.stdout)
        records = {"NoDetect": dict(make_record(), detectionValues=""), "Later": dict(make_record(), detectionValues="")}
        result, _ = self.run_with_records(records)
        self.assertIn('record "NoDetect" has no detection values, so at address 0x10 it is matched before Later',
                      result.stdout)

    def test_key_index_sorted_in_byte_order(self):
        # Records in JSON order Zeta, alpha, Beta, Alpha: byte order puts upper case first (as strcmp)
        keys = ["Zeta", "alpha", "Beta", "Alpha"]
        result, header = self.run_with_records({k: make_record() for k in keys})
        self.assertEqual(result.returncode, 0, result.stderr)
        match = re.search(r"static const uint16_t baseDevTypeIndexByKey\[\] =\n\{\n\s*([0-9,]*)\n\};", header)
        self.assertIsNotNone(match)
        indexes = [int(x) for x in match.group(1).split(",") if x != ""]
        self.assertEqual([keys[i] for i in indexes], ["Alpha", "Beta", "Zeta", "alpha"])

    def test_first_record_without_addresses(self):
        # C6: this crashed the generator (UnboundLocalError) when the first record had no addresses
        result, header = self.run_with_records({"NoAddr": make_record(addresses=""), "WithAddr": make_record()})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('R"JSON(NoAddr)JSON"', header)

    def test_address_above_0x77_is_an_error(self):
        # C12: addresses above 0x77 were dropped silently
        result, _ = self.run_with_records({"High": make_record(addresses="0x76-0x79")})
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("0x78,0x79", result.stderr)

    def test_grammar_issues_reported(self):
        # An init value isn't written (only the bytes before "=" are) - reported, and the build still succeeds
        result, header = self.run_with_records({"Trap": dict(make_record(), initValues="0x01=0x0220")})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('GRAMMAR WARNING: record "Trap" initValues: InitValueNotWritten at 5', result.stdout)
        # A detection value the firmware can't use
        result, _ = self.run_with_records({"Bad": dict(make_record(), detectionValues="0x86=A005")})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('GRAMMAR ERROR: record "Bad" detectionValues: BadCheck', result.stdout)

    def test_hex_read_value_size(self):
        # C11: "0x1234" is 2 bytes of data (it was counted as 3, including the prefix)
        size = DecodeGenerator({}).poll_data_bytes({"pollInfo": {"c": "0x00=0x1234&0x01=r1"}})
        self.assertEqual(size, 3)


if __name__ == "__main__":
    unittest.main()
