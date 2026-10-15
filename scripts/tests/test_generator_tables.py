"""Tables emitted by the device type record generator (ProcessDevTypeJsonToC.py)

- The record array and the address index are const (so they are in flash, not RAM).
- baseDevTypeIndexByKey lists the record indexes sorted by key in byte order (as strcmp), for binary search.
- A first record with no addresses doesn't crash the generator (C6).
- An address above 0x77 is an error rather than silently dropped (C12).
- A 0x... read value in poll info counts its hex digits, not the 0x prefix, towards the poll size (C11).
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

    def run_with_records(self, records):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = pathlib.Path(tmp)
            cat = tmp_path / "catalogue.json"
            cat.write_text(json.dumps({"devTypes": records}))
            result = subprocess.run(
                [sys.executable, str(GENERATOR), str(cat),
                 str(tmp_path / "DeviceTypeRecords_generated.h"), str(tmp_path / "DevicePollRecords_generated.h")],
                capture_output=True, text=True)
            header_path = tmp_path / "DeviceTypeRecords_generated.h"
            header = header_path.read_text() if header_path.exists() else ""
            return result, header

    def test_tables_are_const(self):
        result, header = self.run_with_records({"A": make_record()})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("static const DeviceTypeRecord baseDevTypeRecords[]", header)
        self.assertIn("static const uint16_t baseDevTypeIndexByAddr_0x10[]", header)
        self.assertIn("static const uint16_t* const baseDevTypeIndexByAddr[]", header)
        self.assertNotRegex(header, r"static (uint16_t|DeviceTypeRecord)[ *]")

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

    def test_hex_read_value_size(self):
        # C11: "0x1234" is 2 bytes of data (it was counted as 3, including the prefix)
        size = DecodeGenerator({}).poll_data_bytes({"pollInfo": {"c": "0x00=0x1234&0x01=r1"}})
        self.assertEqual(size, 3)


if __name__ == "__main__":
    unittest.main()
