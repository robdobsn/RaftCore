"""Split a device type catalogue into a reduced base catalogue and a device type file with the rest

Records at even positions go to the reduced catalogue (to be compiled), those at odd positions to the file, so
records sharing an address are split between the two - except records with initCode, which must be compiled in
(a type file can't carry compiled init code), so they always go to the reduced catalogue. Used by linux_unit_tests/test_reduced_catalogue: the
firmware built with the reduced catalogue and given the file identifies devices as the full catalogue does.

Usage: split_catalogue.py <catalogue.json> <reduced.json> <file.json>
"""

import json
import sys


def main():
    with open(sys.argv[1]) as catalogue_file:
        dev_types = json.load(catalogue_file)["devTypes"]
    reduced = {}
    rest = {}
    for pos, (key, record) in enumerate(dev_types.items()):
        (reduced if (pos % 2 == 0) or ("initCode" in record) else rest)[key] = record
    with open(sys.argv[2], "w") as reduced_file:
        json.dump({"devTypes": reduced}, reduced_file, indent=1)
    with open(sys.argv[3], "w") as rest_file:
        json.dump({"devTypes": rest}, rest_file, indent=1)


if __name__ == "__main__":
    main()
