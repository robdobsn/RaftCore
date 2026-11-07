#!/usr/bin/env python3
"""Maintain the I2C breakout list (breakouts.csv, owned.csv, tests.csv) against RaftCore.

  check   validate the CSV files and report how they compare to RaftCore's device type records
  enrich  fill blank Adafruit fields (connector, voltage, status) from adafruit.com

Examples:
    python3 breakouts.py check                       # validation problems and the report
    python3 breakouts.py check --out report.md
    python3 breakouts.py enrich                      # show what would change
    python3 breakouts.py enrich --write
"""
import argparse
import csv
import os
import re
import sys
from pathlib import Path

import raft_inventory

RAFTCORE = Path(__file__).resolve().parents[2]
DATA = RAFTCORE / "devtypes" / "breakouts"
BREAKOUTS, TESTS = DATA / "breakouts.csv", DATA / "tests.csv"
# owned.csv is personal, so it is kept outside RaftCore: --owned or RAFT_BREAKOUTS_OWNED
OWNED_ENV = "RAFT_BREAKOUTS_OWNED"
CONNECTORS = {"stemma-qt", "qwiic", "stemma", "grove", "none", ""}
STATUSES = {"active", "discontinued", "retired"}
RESULTS = {"pass", "partial", "fail"}


def read_csv(path: Path) -> list:
    if not path.exists():
        return []
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def write_csv(path: Path, rows: list) -> None:
    with open(path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)


def parse_addrs(spec: str) -> set:
    """'0x28-0x2b,0x53' -> {0x28, 0x29, 0x2a, 0x2b, 0x53}. Raises ValueError on bad input."""
    out = set()
    for part in filter(None, (p.strip() for p in (spec or "").split(","))):
        lo, _, hi = part.partition("-")
        lo_v, hi_v = int(lo, 16), int(hi or lo, 16)
        if not (0 <= lo_v <= hi_v <= 0x7f):
            raise ValueError(part)
        out.update(range(lo_v, hi_v + 1))
    return out


def fmt_addrs(addrs) -> str:
    return ",".join(f"0x{a:02x}" for a in sorted(addrs))


def five_v_ok(row: dict) -> str:
    try:
        vmax = float(row["vcc_max"]) if row["vcc_max"] else None
    except ValueError:
        return "?"
    if vmax is None:
        return "?"
    return "yes" if vmax >= 5 else "no"


def owned_path(arg) -> Path:
    path = arg or os.environ.get(OWNED_ENV)
    return Path(path).expanduser() if path else None


def load(raftcore: Path, owned_file):
    if owned_file and not owned_file.exists():
        raise SystemExit(f"owned file {owned_file} not found")
    if not owned_file:
        print(f"no owned.csv given (--owned or {OWNED_ENV}): the report treats nothing as owned", file=sys.stderr)
    breakouts, tests = read_csv(BREAKOUTS), read_csv(TESTS)
    owned = read_csv(owned_file) if owned_file else []
    records = {r["key"]: r for r in raft_inventory.inventory(raftcore)}
    return breakouts, owned, tests, records


def validate(breakouts, owned, tests, records) -> list:
    problems = []
    ids = set()
    for i, b in enumerate(breakouts, start=2):
        where = f"breakouts.csv:{i} ({b['id']})"
        if b["id"] in ids:
            problems.append(f"{where}: duplicate id")
        ids.add(b["id"])
        if b["raft_record"] and b["raft_record"] not in records:
            problems.append(f"{where}: raft_record '{b['raft_record']}' is not a RaftCore record")
        if b["connector"] not in CONNECTORS:
            problems.append(f"{where}: connector '{b['connector']}' not one of {sorted(CONNECTORS - {''})}")
        if b["status"] not in STATUSES:
            problems.append(f"{where}: status '{b['status']}' not one of {sorted(STATUSES)}")
        try:
            addrs = parse_addrs(b["addresses"])
        except ValueError as e:
            problems.append(f"{where}: bad address '{e}'")
            continue
        rec = records.get(b["raft_record"])
        if rec and rec["addresses"]:
            extra = addrs - parse_addrs(rec["addresses"])
            if extra:
                problems.append(f"WARNING {where}: address {fmt_addrs(extra)} not in {b['raft_record']}'s {rec['addresses']}")
    for i, o in enumerate(owned, start=2):
        if o["id"] not in ids:
            problems.append(f"owned.csv:{i}: unknown breakout id '{o['id']}'")
        if not o["qty"].isdigit():
            problems.append(f"owned.csv:{i}: qty '{o['qty']}' is not a number")
    for i, t in enumerate(tests, start=2):
        if t["raft_record"] not in records:
            problems.append(f"tests.csv:{i}: unknown raft_record '{t['raft_record']}'")
        if t["breakout_id"] and t["breakout_id"] not in ids:
            problems.append(f"tests.csv:{i}: unknown breakout_id '{t['breakout_id']}'")
        if t["result"] not in RESULTS:
            problems.append(f"tests.csv:{i}: result '{t['result']}' not one of {sorted(RESULTS)}")
        if not re.fullmatch(r"\d{4}-\d\d-\d\d", t["date"]):
            problems.append(f"tests.csv:{i}: date '{t['date']}' is not YYYY-MM-DD")
    return problems


def report(breakouts, owned, tests, records) -> str:
    qty = {o["id"]: int(o["qty"]) if o["qty"].isdigit() else 0 for o in owned}
    have = [b for b in breakouts if qty.get(b["id"], 0) > 0]
    by_addr = {}
    for key, rec in records.items():
        for a in parse_addrs(rec["addresses"] or ""):
            by_addr.setdefault(a, []).append(key)
    tested = {t["raft_record"] for t in tests}
    out = []

    def table(head, rows):
        out.append("| " + " | ".join(head) + " |")
        out.append("|" + "---|" * len(head))
        out.extend("| " + " | ".join(str(c) for c in r) + " |" for r in rows)
        out.append("")

    # 1. boards owned with no record - the candidates to add
    unsupported = sorted((b for b in have if not b["raft_record"] and b["chip"]), key=lambda b: (b["category"], b["chip"]))
    out.append(f"## Owned, no RaftCore record ({len(unsupported)})\n")
    rows = []
    for b in unsupported:
        try:
            addrs = parse_addrs(b["addresses"])
        except ValueError:
            addrs = set()
        clashes = sorted({k for a in addrs for k in by_addr.get(a, [])})
        rows.append([b["chip"], b["vendor"], b["product"], b["category"], b["addresses"] or "?",
                     five_v_ok(b), ", ".join(clashes)])
    table(["Chip", "Vendor", "Product", "Category", "Addresses", "5V ok", "Shares address with"], rows)

    # 2. records whose board isn't owned (or isn't listed)
    owned_recs = {b["raft_record"] for b in have if b["raft_record"]}
    listed_recs = {b["raft_record"] for b in breakouts if b["raft_record"]}
    rows = [[k, r["manufacturer"], r["name"], "listed, not owned" if k in listed_recs else "not listed"]
            for k, r in sorted(records.items()) if k not in owned_recs]
    out.append(f"## RaftCore records with no owned board ({len(rows)})\n")
    table(["Record", "Manufacturer", "Name", "Breakout list"], rows)

    # 3. supported and owned but no test recorded
    rows = sorted({(b["raft_record"], b["id"]) for b in have if b["raft_record"] and b["raft_record"] not in tested})
    out.append(f"## Owned and supported, no test recorded ({len(rows)})\n")
    table(["Record", "Breakout"], rows)

    # 4. tests not tied to a breakout
    loose = [t for t in tests if not t["breakout_id"]]
    if loose:
        rows = []
        for t in loose:
            cands = [b["id"] for b in have if b["raft_record"] == t["raft_record"]]
            rows.append([t["date"], t["raft_record"], t["host"], ", ".join(cands) or "none owned"])
        out.append(f"## Tests with no breakout_id ({len(loose)})\n")
        table(["Date", "Record", "Host", "Owned candidates"], rows)

    n_rec = sum(1 for b in breakouts if b["raft_record"])
    out.insert(0, f"# Breakout report\n\n{len(breakouts)} breakouts listed, {len(have)} owned, "
                  f"{n_rec} map to a RaftCore record; {len(records)} records, {len(tested)} with a test.\n")
    return "\n".join(out)


def cmd_check(args) -> int:
    breakouts, owned, tests, records = load(args.raftcore, owned_path(args.owned))
    problems = validate(breakouts, owned, tests, records)
    errors = [p for p in problems if not p.startswith("WARNING")]
    for p in problems:
        print(p if p.startswith("WARNING") else f"ERROR {p}", file=sys.stderr)
    text = report(breakouts, owned, tests, records)
    if args.out:
        args.out.write_text(text)
        print(f"report written to {args.out}", file=sys.stderr)
    else:
        print(text)
    return 1 if errors else 0


def cmd_enrich(args) -> int:
    import adafruit
    breakouts = read_csv(BREAKOUTS)
    cat = adafruit.products()
    changes = 0
    for b in breakouts:
        if b["vendor"] != "Adafruit" or not b["vendor_pid"]:
            continue
        pid = b["vendor_pid"]
        p = cat.get(pid)
        if not p:
            print(f"{b['id']}: product {pid} not in the Adafruit catalogue")
            continue
        facts = adafruit.page_facts(pid)
        upd = {}
        if p.get("discontinue_status") == "Discontinued" and b["status"] == "active":
            upd["status"] = "discontinued"
        if not b["connector"] and facts["connector"]:
            upd["connector"] = facts["connector"]
        if facts["vcc"] == "3-5V":
            if not b["vcc_min"]:
                upd["vcc_min"] = "3"
            if not b["vcc_max"]:
                upd["vcc_max"] = "5"
            elif float(b["vcc_max"]) < 5:
                print(f"{b['id']}: sheet vcc_max {b['vcc_max']} but page says 3-5V: {facts['vcc_evidence'][:120]}")
        elif facts["vcc"] == "3.3V" and b["vcc_max"] and float(b["vcc_max"]) >= 5:
            print(f"{b['id']}: sheet vcc_max {b['vcc_max']} but page suggests 3.3V only: {facts['vcc_evidence'][:120]}")
        page_addrs = set(parse_addrs(facts["addresses"]))
        if page_addrs and not page_addrs & parse_addrs(b["addresses"]):
            print(f"{b['id']}: addresses {b['addresses'] or '(none)'} but page mentions {facts['addresses']}")
        if upd:
            changes += 1
            print(f"{b['id']}: " + ", ".join(f"{k} '{b[k]}' -> '{v}'" for k, v in upd.items()))
            b.update(upd)
    if args.write and changes:
        write_csv(BREAKOUTS, breakouts)
        print(f"{changes} rows updated in {BREAKOUTS.name}")
    elif changes:
        print(f"{changes} rows would change; run with --write to save")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--raftcore", type=Path, default=RAFTCORE, help="RaftCore checkout (default: this one)")
    ap.add_argument("--owned", help=f"owned.csv (default: ${OWNED_ENV})")
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("check", help="validate and report")
    c.add_argument("--out", type=Path, help="write the report (markdown) here")
    e = sub.add_parser("enrich", help="fill blank Adafruit fields from adafruit.com")
    e.add_argument("--write", action="store_true", help="save the changes to breakouts.csv")
    args = ap.parse_args()
    return {"check": cmd_check, "enrich": cmd_enrich}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
