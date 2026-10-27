"""sdkconfig.defaults merging in MergeSysTypeConfigs.py

Common base, then sdkconfig fragments (RAFT_SDKCONFIG_FRAGMENTS in features.cmake) in order, then the SysType's
own sdkconfig.defaults - a later layer overrides an earlier one by CONFIG key. The output for Common + SysType
(no fragments) is unchanged from before fragments existed.
"""

import pathlib
import subprocess
import sys
import tempfile
import unittest


SCRIPTS_DIR = pathlib.Path(__file__).resolve().parents[1]
MERGE_SCRIPT = SCRIPTS_DIR / "MergeSysTypeConfigs.py"
FRAGMENTS_DIR = SCRIPTS_DIR.parent / "sdkconfig-fragments"
sys.path.insert(0, str(SCRIPTS_DIR))
import MergeSysTypeConfigs  # noqa: E402


class MergeSdkconfigTests(unittest.TestCase):

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.dir = pathlib.Path(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def write(self, name, text):
        path = self.dir / name
        path.write_text(text)
        return str(path)

    def merge(self, common=None, systype=None, fragments=None):
        out = self.dir / "merged"
        MergeSysTypeConfigs.merge_sdkconfig_defaults(common, systype, str(out), fragments)
        return out.read_text()

    def test_common_and_systype_unchanged_format(self):
        common = self.write("common", "# c\nCONFIG_A=y\nCONFIG_B=1\n")
        systype = self.write("systype", "CONFIG_B=2\n")
        self.assertEqual(self.merge(common, systype),
                         "#\n# Base configuration from Common/sdkconfig.defaults\n#\n\n"
                         "# c\nCONFIG_A=y\n"
                         "\n#\n# SysType-specific overrides\n#\n\n"
                         "CONFIG_B=2\n")

    def test_systype_only_has_no_heading(self):
        systype = self.write("systype", "CONFIG_B=2\n")
        self.assertEqual(self.merge(None, systype), "CONFIG_B=2\n")

    def test_fragments_between_common_and_systype(self):
        common = self.write("common", "CONFIG_A=1\nCONFIG_B=1\nCONFIG_C=1\n")
        frag1 = self.write("frag1.defaults", "CONFIG_B=2\nCONFIG_C=2\n# CONFIG_D is not set\n")
        frag2 = self.write("frag2.defaults", "CONFIG_C=3\n")
        systype = self.write("systype", "CONFIG_D=y\n")
        merged = self.merge(common, systype, [frag1, frag2])
        values = MergeSysTypeConfigs.parse_sdkconfig(str(self.dir / "merged"))[0]
        self.assertEqual(values, {"CONFIG_A": "1", "CONFIG_B": "2", "CONFIG_C": "3", "CONFIG_D": "y"})
        # Each key is set once, by the last layer that sets it
        self.assertEqual(merged.count("CONFIG_C="), 1)
        self.assertNotIn("CONFIG_D is not set", merged)
        self.assertIn("# sdkconfig fragment frag1.defaults", merged)
        self.assertLess(merged.index("frag1.defaults"), merged.index("frag2.defaults"))
        self.assertLess(merged.index("frag2.defaults"), merged.index("SysType-specific overrides"))

    def test_fragment_only(self):
        frag = self.write("frag.defaults", "CONFIG_X=y\n")
        self.assertIn("CONFIG_X=y", self.merge(None, None, [frag]))

    def test_raftcore_fragments_parse(self):
        for fragment in FRAGMENTS_DIR.glob("*.defaults"):
            values = MergeSysTypeConfigs.parse_sdkconfig(str(fragment))[0]
            self.assertTrue(values, f"{fragment.name} sets nothing")

    def test_missing_fragment_is_an_error(self):
        result = subprocess.run([sys.executable, str(MERGE_SCRIPT), "--mode", "sdkconfig",
                                 "--fragment", str(self.dir / "missing.defaults"),
                                 "--output", str(self.dir / "merged")],
                                capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not found", result.stderr)


if __name__ == "__main__":
    unittest.main()
