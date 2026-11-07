"""The I2C breakout list (devtypes/breakouts) and its script (scripts/breakouts/breakouts.py)

breakouts.csv and tests.csv must be valid against RaftCore's records: known record keys, the columns in order,
known connectors, statuses, priorities, checks and results. owned.csv is kept outside RaftCore so isn't checked.
"""

import pathlib
import sys
import unittest

SCRIPTS_DIR = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS_DIR / "breakouts"))
import breakouts  # noqa: E402
import raft_inventory  # noqa: E402


class TestBreakoutData(unittest.TestCase):
    def test_csv_files_are_valid(self):
        records = {r["key"]: r for r in raft_inventory.inventory(breakouts.RAFTCORE)}
        problems = breakouts.validate(breakouts.read_csv(breakouts.BREAKOUTS), [],
                                      breakouts.read_csv(breakouts.TESTS), records)
        errors = [p for p in problems if not p.startswith("WARNING")]
        self.assertEqual(errors, [])

    def test_csv_files_use_lf(self):
        for path in (breakouts.BREAKOUTS, breakouts.TESTS):
            self.assertNotIn(b"\r", path.read_bytes(), path.name)


class TestBreakoutScript(unittest.TestCase):
    def test_parse_addrs(self):
        self.assertEqual(breakouts.parse_addrs("0x28-0x2b,0x53"), {0x28, 0x29, 0x2a, 0x2b, 0x53})
        self.assertEqual(breakouts.parse_addrs(""), set())
        for bad in ("0x80", "0x2b-0x28", "zz"):
            with self.assertRaises(ValueError):
                breakouts.parse_addrs(bad)

    def test_split_device_id(self):
        self.assertEqual(breakouts.split_device_id("1_76a"), (1, 0x6a, 7))
        self.assertEqual(breakouts.split_device_id("1_29"), (1, 0x29, 0))
        self.assertIsNone(breakouts.split_device_id("0_1"))
        self.assertIsNone(breakouts.split_device_id("LEDs"))

    def test_validation_needs_detect_and_data(self):
        row = {"result": "pass", "checks": "detect data rsao"}
        self.assertTrue(breakouts.is_validation(row))
        self.assertFalse(breakouts.is_validation({**row, "checks": "detect rsao"}))
        self.assertFalse(breakouts.is_validation({**row, "result": "partial"}))

    def test_bad_rows_are_reported(self):
        records = {"R1": {"key": "R1", "addresses": "0x10"}}
        b = dict.fromkeys(breakouts.BREAKOUT_FIELDS, "")
        b.update(id="acme-1", raft_record="R1", status="active", addresses="0x10")
        t = dict.fromkeys(breakouts.TEST_FIELDS, "")
        t.update(date="2026-11-07", raft_record="R1", breakout_id="acme-1", checks="detect data", result="pass")
        self.assertEqual(breakouts.validate([b], [], [t], records), [])
        cases = [({**b, "priority": "urgent"}, t, "priority"),
                 ({**b, "raft_record": "R2"}, t, "not a RaftCore record"),
                 (b, {**t, "checks": "detect looked"}, "checks"),
                 (b, {**t, "breakout_id": "acme-2"}, "unknown breakout_id"),
                 (b, {**t, "date": "7/11/2026"}, "date")]
        for bb, tt, expect in cases:
            problems = breakouts.validate([bb], [], [tt], records)
            self.assertTrue(any(expect in p for p in problems), (expect, problems))


if __name__ == "__main__":
    unittest.main()
