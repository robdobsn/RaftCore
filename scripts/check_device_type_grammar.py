#!/usr/bin/env python3
"""Impact report: grammar errors and warnings for every record in device type catalogues

Usage: check_device_type_grammar.py <catalogue.json, record file or records folder> [...]

Reports, for each record, what the firmware's command grammar (DeviceTypeGrammar) finds in its detectionValues,
initValues and pollInfo.c. An error means the firmware doesn't use that detection string or drops that step;
a warning means the string does something other than it appears to (it is used as before). Exit code 1 if any
record has an error.
"""

import json
import sys
import pathlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import DeviceTypeGrammar  # noqa: E402
import AssembleDeviceTypes  # noqa: E402


def main(paths):
    totals = {}
    any_errors = False
    for path in paths:
        # A catalogue file, a record file or a folder of record files
        dev_types, _ = AssembleDeviceTypes.load_catalogue(path)
        print(f"== {path} ({len(dev_types)} records)")
        for key, dev_type in dev_types.items():
            for field, result in DeviceTypeGrammar.check_record(dev_type):
                for code, pos in result.issues:
                    kind = "ERROR" if DeviceTypeGrammar.is_error(code) else "warning"
                    any_errors = any_errors or kind == "ERROR"
                    totals[code] = totals.get(code, 0) + 1
                    source = dev_type.get("detectionValues", "") if field == "detection" else (
                        dev_type.get("initValues", "") if field == "init" else dev_type.get("pollInfo", {}).get("c", ""))
                    print(f"  {kind:7s} {key:24s} {DeviceTypeGrammar.FIELD_NAMES[field]:15s} {code} at {pos}: "
                          f"{source[max(0, pos - 12):pos + 12]!r}")
    print("== totals: " + (", ".join(f"{code} {n}" for code, n in sorted(totals.items())) or "none"))
    return 1 if any_errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
