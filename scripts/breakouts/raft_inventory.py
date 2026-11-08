#!/usr/bin/env python3
"""List the device types supported by RaftCore (devtypes/records/*.json) as CSV.

One row per record: key, manufacturer, chip type, name, I2C addresses, whether it has
detection values, which RaftCore sets it is in, and any bench tests found in its notes.

Usage:
    python3 raft_inventory.py [--raftcore <RaftCore>] [--out raft_devices.csv]
"""
import argparse
import csv
import json
import re
import sys
from pathlib import Path

BENCH_RE = re.compile(r"[Bb]ench[^.;]*?\((\w+)[, ]+(\d{4}-\d\d-\d\d)")


def load_sets(raftcore: Path) -> dict:
    """Map record key -> list of set names it belongs to."""
    membership = {}
    for set_file in sorted((raftcore / "devtypes" / "sets").glob("*.json")):
        for key in json.loads(set_file.read_text()).get("records", []):
            membership.setdefault(key, []).append(set_file.stem)
    return membership


def inventory(raftcore: Path) -> list:
    sets = load_sets(raftcore)
    rows = []
    for rec_file in sorted((raftcore / "devtypes" / "records").glob("*.json"), key=lambda p: p.stem.lower()):
        rec = json.loads(rec_file.read_text())
        info = rec.get("devInfoJson", {})
        notes = rec.get("_notes", "")
        if not isinstance(notes, str):
            notes = json.dumps(notes)
        evidence = notes + json.dumps(rec.get("hwProbeEffects", {}))
        bench = sorted({f"{who} {date}" for who, date in BENCH_RE.findall(evidence)})
        rows.append({
            "key": rec_file.stem,
            "manufacturer": info.get("manu", ""),
            "chip": info.get("type", ""),
            "name": info.get("name", ""),
            "addresses": rec.get("addresses", ""),
            "has_detection": "yes" if rec.get("detectionValues") else "no",
            "sets": " ".join(sets.get(rec_file.stem, [])),
            "bench_tests": "; ".join(bench),
        })
    return rows


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--raftcore", type=Path, default=Path(__file__).resolve().parents[2])
    ap.add_argument("--out", type=Path, help="CSV file to write (default: stdout)")
    args = ap.parse_args()

    if not (args.raftcore / "devtypes" / "records").is_dir():
        print(f"No devtypes/records under {args.raftcore}", file=sys.stderr)
        return 1
    rows = inventory(args.raftcore)
    out = open(args.out, "w", newline="") if args.out else sys.stdout
    writer = csv.DictWriter(out, fieldnames=list(rows[0].keys()))
    writer.writeheader()
    writer.writerows(rows)
    if args.out:
        out.close()
        print(f"{len(rows)} records written to {args.out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
