"""Device type key handling in the device type record generator (ProcessDevTypeJsonToC.py)

The key of each record in "devTypes" is the device type key. "deviceType" may be omitted; if present it must
equal the key. devInfoJson "type" is a copy kept for clients: a new mismatch with the key gives a warning.
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
RAFTCORE_CATALOGUE = SCRIPTS_DIR.parent / "devtypes" / "records"
sys.path.insert(0, str(SCRIPTS_DIR))
import AssembleDeviceTypes  # noqa: E402


def make_record(dev_info_type=None, device_type=None):
    """A minimal valid record (a 1-byte poll), with optional deviceType and devInfoJson type"""
    dev_info = {"name": "Test", "desc": "Test", "manu": "Test",
                "resp": {"b": 1, "a": [{"n": "v", "t": "B", "u": "", "r": [0, 255], "f": "d", "o": "uint8"}]}}
    if dev_info_type is not None:
        dev_info["type"] = dev_info_type
    record = {"addresses": "0x10", "detectionValues": "0x00=0x01", "initValues": "",
              "pollInfo": {"c": "0x00=r1", "i": 100, "s": 1}, "devInfoJson": dev_info}
    if device_type is not None:
        record["deviceType"] = device_type
    return record


class DeviceTypeKeyTests(unittest.TestCase):

    def run_generator(self, catalogue_paths):
        with tempfile.TemporaryDirectory() as out_dir:
            out = pathlib.Path(out_dir)
            result = subprocess.run(
                [sys.executable, str(GENERATOR), ",".join(str(p) for p in catalogue_paths),
                 str(out / "DeviceTypeRecords_generated.h"), str(out / "DevicePollRecords_generated.h")],
                capture_output=True, text=True)
            header = ""
            if (out / "DeviceTypeRecords_generated.h").exists():
                header = (out / "DeviceTypeRecords_generated.h").read_text()
            return result, header

    def run_with_records(self, records):
        with tempfile.TemporaryDirectory() as cat_dir:
            path = pathlib.Path(cat_dir) / "catalogue.json"
            path.write_text(json.dumps({"devTypes": records}))
            return self.run_generator([path])

    @staticmethod
    def record_names(header):
        """Device type keys emitted as the first string of each generated record"""
        return re.findall(r'\{\n        R"JSON\(([^)]*)\)JSON"', header)

    def test_device_type_omitted_uses_key(self):
        result, header = self.run_with_records({"MySensor": make_record(dev_info_type="MySensor")})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.record_names(header), ["MySensor"])

    def test_device_type_equal_to_key_accepted(self):
        result, header = self.run_with_records({"MySensor": make_record(dev_info_type="MySensor", device_type="MySensor")})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.record_names(header), ["MySensor"])

    def test_device_type_different_from_key_rejected(self):
        result, _ = self.run_with_records({"MySensor": make_record(device_type="OtherName")})
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("MySensor", result.stderr)
        self.assertIn("OtherName", result.stderr)

    def test_dev_info_type_mismatch_warns(self):
        result, _ = self.run_with_records({"MySensor": make_record(dev_info_type="Different")})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("WARNING", result.stdout)
        self.assertIn("MySensor", result.stdout)

    def test_known_dev_info_type_mismatches_do_not_warn(self):
        result, _ = self.run_with_records({
            "AdafruitGamepad": make_record(dev_info_type="Gamepad"),
            "RoboticalThermocoupleAmp": make_record(dev_info_type="Thermocouple")})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("WARNING", result.stdout)

    def test_raftcore_catalogue(self):
        """Every RaftCore record is named by its key, with no deviceType in the file and no key warnings"""
        catalogue, _ = AssembleDeviceTypes.load_catalogue(str(RAFTCORE_CATALOGUE))
        self.assertFalse([k for k, v in catalogue.items() if "deviceType" in v],
                         "deviceType is filled in from the key, so the RaftCore catalogue omits it")
        result, header = self.run_generator([RAFTCORE_CATALOGUE])
        self.assertEqual(result.returncode, 0, result.stderr)
        key_warnings = [line for line in result.stdout.splitlines()
                        if line.startswith("WARNING") and "has no detection values" not in line]
        self.assertEqual(key_warnings, [])
        self.assertEqual(self.record_names(header), list(catalogue.keys()))

    def test_raftcore_catalogue_shadowing(self):
        """The records in the RaftCore catalogue that have no detection values and are matched before another
        record at the same address (so the other is never identified there). Since records are tried in order of
        detection strength (Stage 7) there are none; a new one fails this test"""
        result, _ = self.run_generator([RAFTCORE_CATALOGUE])
        self.assertEqual(result.returncode, 0, result.stderr)
        shadowing = [line for line in result.stdout.splitlines() if "has no detection values" in line]
        self.assertEqual(shadowing, [])


if __name__ == "__main__":
    unittest.main()
