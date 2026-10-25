"""The vectors shared with raftjs are what RaftCore's Python gives now: typefile_vectors.json (build_type_file,
written by make_typefile_vectors.py), selection_vectors.json (assemble, make_selection_vectors.py) and
codecheck_vectors.json (the decode code check, make_codecheck_vectors.py). Copy them to raftjs testdata/ after a
change"""

import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import make_codecheck_vectors  # noqa: E402
import make_selection_vectors  # noqa: E402
import make_typefile_vectors  # noqa: E402


class TypeFileVectorTests(unittest.TestCase):

    def test_vectors_are_current(self):
        self.assertEqual(make_typefile_vectors.main(["--check"]), 0,
                         "typefile_vectors.json is out of date - run make_typefile_vectors.py")
        self.assertEqual(make_selection_vectors.main(["--check"]), 0,
                         "selection_vectors.json is out of date - run make_selection_vectors.py")
        self.assertEqual(make_codecheck_vectors.main(["--check"]), 0,
                         "codecheck_vectors.json is out of date - run make_codecheck_vectors.py")


if __name__ == "__main__":
    unittest.main()
