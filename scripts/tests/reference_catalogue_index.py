"""Reference address map and scan lists for a device type catalogue

This is the algorithm ProcessDevTypeJsonToC.py used to derive baseDevTypeIndexByAddr and the scan priority lists
at build time, before the catalogue index was built at runtime (Stage 3 of the device identification plan). It
writes them as JSON so linux_unit_tests/test_catalogue_index can check that the runtime index gives the same
result for base records.

Usage: reference_catalogue_index.py <catalogue.json>[,<catalogue.json>...] <out.json>
"""

import json
import re
import sys

MIN_VALID_ADDR = 0x04
MAX_VALID_ADDR = 0x77
NUM_PRIORITY_LEVELS = 3
SCAN_PRIORITY_VALUES = {"high": 0, "medium": 1, "low": 2}


def parse_addresses(addresses):
    addr_list = []
    addresses = addresses.strip()
    if addresses == "":
        return addr_list
    for addr_range in addresses.split(","):
        addr_range = addr_range.strip()
        if re.match(r'^0x[0-9a-fA-F]{2}-0x[0-9a-fA-F]{2}$', addr_range):
            start, end = [int(x, 16) for x in addr_range.split("-")]
            addr_list.extend(range(start, end + 1))
        elif re.match(r'^0x[0-9a-fA-F]{2}$', addr_range):
            addr_list.append(int(addr_range, 16))
        else:
            raise ValueError(f"Invalid address range {addr_range}")
    return addr_list


def reference_index(dev_types):
    """Address -> record indexes (in record order) and the scan priority lists, as the generator built them"""
    addr_map = {}
    scan_sets = [set() for _ in range(NUM_PRIORITY_LEVELS)]
    for record_index, dev_type in enumerate(dev_types.values()):
        addr_list = parse_addresses(dev_type.get("addresses", ""))
        exclude = bool(dev_type.get("excludeFromAddrMap", False))
        if "scanPriority" in dev_type and not exclude and len(addr_list) > 0:
            scan_priority = dev_type["scanPriority"]
            if isinstance(scan_priority, int):
                level = scan_priority - 1
            elif isinstance(scan_priority, str):
                level = SCAN_PRIORITY_VALUES[scan_priority]
            else:
                level = NUM_PRIORITY_LEVELS - 1
            level = min(max(level, 0), NUM_PRIORITY_LEVELS - 1)
            if level == 0:
                scan_sets[0].add(addr_list[0])
                for addr in addr_list[1:]:
                    scan_sets[1].add(addr)
            else:
                for addr in addr_list:
                    scan_sets[level].add(addr)
        if not exclude:
            for addr in addr_list:
                addr_map.setdefault(addr, []).append(record_index)
    scan_lists = []
    for i in range(NUM_PRIORITY_LEVELS):
        if i == NUM_PRIORITY_LEVELS - 1:
            scan_sets[i] = set(range(MIN_VALID_ADDR, MAX_VALID_ADDR + 1))
        for j in range(i):
            scan_sets[i] = scan_sets[i] - scan_sets[j]
        scan_lists.append(sorted(scan_sets[i]))
    return {
        "addrMap": {str(addr): indexes for addr, indexes in sorted(addr_map.items()) if addr <= MAX_VALID_ADDR},
        "scanLists": scan_lists,
    }


def main():
    catalogues = [p for p in sys.argv[1].strip("[]").split(",") if p.strip() != ""]
    dev_types = {}
    for path in catalogues:
        with open(path.strip()) as f:
            dev_types.update(json.load(f).get("devTypes", {}))
    with open(sys.argv[2], "w") as f:
        json.dump(reference_index(dev_types), f)


if __name__ == "__main__":
    main()
