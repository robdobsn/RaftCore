#!/usr/bin/env python3
"""Maintain the I2C breakout list (devtypes/breakouts: breakouts.csv, tests.csv) against RaftCore's records.

  check   validate the CSV files and report how they compare to RaftCore's device type records
  bench   check a breakout attached to a Raft host (found, identified, polled) and record the test
  enrich  fill blank Adafruit fields (connector, voltage, status) from adafruit.com

Which boards are owned is personal, so owned.csv (id,qty,notes) is kept outside RaftCore and given with --owned
or the environment variable RAFT_BREAKOUTS_OWNED. See devtypes/breakouts/README.md.

Examples:
    python3 breakouts.py --owned ~/owned.csv check --out report.md
    python3 breakouts.py bench adafruit-4161 --ip 192.168.1.8                  # check, and show the row it would add
    python3 breakouts.py bench adafruit-4161 --ip 192.168.1.8 --record pass --checks "detect data" \\
        --notes "about 5 proximity with nothing near, 300-420 with a finger close"
    python3 breakouts.py enrich [--write]
"""
import argparse
import csv
import datetime
import json
import os
import re
import subprocess
import sys
import urllib.parse
import urllib.request
from pathlib import Path

import raft_inventory

RAFTCORE = Path(__file__).resolve().parents[2]
DATA = RAFTCORE / "devtypes" / "breakouts"
BREAKOUTS, TESTS = DATA / "breakouts.csv", DATA / "tests.csv"
OWNED_ENV = "RAFT_BREAKOUTS_OWNED"
CONNECTORS = {"stemma-qt", "qwiic", "stemma", "grove", "mikrobus", "none", ""}
STATUSES = {"active", "discontinued", "retired"}
PRIORITIES = {"high": 0, "medium": 1, "low": 2, "": 3}
RESULTS = {"pass", "partial", "fail"}
CHECKS = ("detect", "data", "alt-addr", "neighbours", "power-cycle", "rsao")
VALIDATED_CHECKS = {"detect", "data"}
BREAKOUT_FIELDS = ["id", "vendor", "product", "vendor_pid", "chip", "chip_manufacturer", "category", "raft_record",
                   "connector", "vcc_min", "vcc_max", "addresses", "status", "priority", "url", "datasheet", "notes"]
TEST_FIELDS = ["date", "raft_record", "breakout_id", "tester", "host", "raftcore_version", "checks", "result", "notes"]


def read_csv(path: Path) -> list:
    if not path.exists():
        return []
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def write_csv(path: Path, rows: list, fields: list) -> None:
    with open(path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields, lineterminator="\n")
        w.writeheader()
        w.writerows(rows)


def parse_addrs(spec: str) -> set:
    """'0x28-0x2b,0x53' -> {0x28, 0x29, 0x2a, 0x2b, 0x53}. Raises ValueError on bad input."""
    out = set()
    for part in filter(None, (p.strip() for p in (spec or "").split(","))):
        lo, _, hi = part.partition("-")
        try:
            lo_v, hi_v = int(lo, 16), int(hi or lo, 16)
        except ValueError:
            raise ValueError(part) from None
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


def records_of(b: dict) -> list:
    """The records a breakout uses: raft_record is one key, or several separated by spaces (a chip at two
    addresses, such as the MS8607's pressure/temperature and humidity parts)"""
    return b["raft_record"].split()


def checks_of(test: dict) -> set:
    return set(test.get("checks", "").split())


def is_validation(test: dict) -> bool:
    """A test that validates its breakout: a pass that checked at least detection and the data"""
    return test["result"] == "pass" and VALIDATED_CHECKS <= checks_of(test)


def owned_path(arg):
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
    """Problems found; those starting WARNING don't fail the check"""
    problems = []
    if breakouts and list(breakouts[0].keys()) != BREAKOUT_FIELDS:
        problems.append(f"breakouts.csv: columns {list(breakouts[0].keys())} should be {BREAKOUT_FIELDS}")
        return problems
    if tests and list(tests[0].keys()) != TEST_FIELDS:
        problems.append(f"tests.csv: columns {list(tests[0].keys())} should be {TEST_FIELDS}")
        return problems
    by_id = {}
    for i, b in enumerate(breakouts, start=2):
        where = f"breakouts.csv:{i} ({b['id']})"
        if not re.fullmatch(r"[a-z0-9]+(-[a-z0-9]+)+", b["id"]):
            problems.append(f"{where}: id should be lower case <vendor>-<product id or name>")
        if b["id"] in by_id:
            problems.append(f"{where}: duplicate id")
        by_id[b["id"]] = b
        for key in records_of(b):
            if key not in records:
                problems.append(f"{where}: raft_record '{key}' is not a RaftCore record")
        if b["connector"] not in CONNECTORS:
            problems.append(f"{where}: connector '{b['connector']}' not one of {sorted(CONNECTORS - {''})}")
        if b["status"] not in STATUSES:
            problems.append(f"{where}: status '{b['status']}' not one of {sorted(STATUSES)}")
        if b["priority"] not in PRIORITIES:
            problems.append(f"{where}: priority '{b['priority']}' not one of {sorted(set(PRIORITIES) - {''})} or blank")
        for f in ("vcc_min", "vcc_max"):
            if b[f] and not re.fullmatch(r"\d+(\.\d+)?", b[f]):
                problems.append(f"{where}: {f} '{b[f]}' is not a number")
        try:
            addrs = parse_addrs(b["addresses"])
        except ValueError as e:
            problems.append(f"{where}: bad address '{e}'")
            continue
        recs = [records[k] for k in records_of(b) if k in records]
        if recs and all(r["addresses"] for r in recs):
            extra = addrs - set().union(*(parse_addrs(r["addresses"]) for r in recs))
            if extra:
                problems.append(f"WARNING {where}: address {fmt_addrs(extra)} not in {b['raft_record']}'s "
                                f"{' '.join(r['addresses'] for r in recs)}")
    for i, o in enumerate(owned, start=2):
        if o["id"] not in by_id:
            problems.append(f"owned.csv:{i}: unknown breakout id '{o['id']}'")
        if not o["qty"].isdigit():
            problems.append(f"owned.csv:{i}: qty '{o['qty']}' is not a number")
    seen = set()
    for i, t in enumerate(tests, start=2):
        where = f"tests.csv:{i}"
        if t["raft_record"] not in records:
            problems.append(f"{where}: unknown raft_record '{t['raft_record']}'")
        b = by_id.get(t["breakout_id"])
        if t["breakout_id"] and not b:
            problems.append(f"{where}: unknown breakout_id '{t['breakout_id']}'")
        elif b and t["raft_record"] not in records_of(b):
            problems.append(f"WARNING {where}: breakout {b['id']} is listed with record '{b['raft_record']}', "
                            f"the test is of '{t['raft_record']}'")
        if t["result"] not in RESULTS:
            problems.append(f"{where}: result '{t['result']}' not one of {sorted(RESULTS)}")
        bad = checks_of(t) - set(CHECKS)
        if bad or not checks_of(t):
            problems.append(f"{where}: checks '{t['checks']}' should be one or more of {' '.join(CHECKS)}")
        try:
            datetime.date.fromisoformat(t["date"])
        except ValueError:
            problems.append(f"{where}: date '{t['date']}' is not YYYY-MM-DD")
        key = (t["date"], t["raft_record"], t["breakout_id"], t["checks"])
        if key in seen:
            problems.append(f"WARNING {where}: same date, record, breakout and checks as an earlier row")
        seen.add(key)
    return problems


def report(breakouts, owned, tests, records) -> str:
    qty = {o["id"]: int(o["qty"]) if o["qty"].isdigit() else 0 for o in owned}
    have = [b for b in breakouts if qty.get(b["id"], 0) > 0]
    by_addr = {}
    for key, rec in records.items():
        for a in parse_addrs(rec["addresses"] or ""):
            by_addr.setdefault(a, []).append(key)
    tests_of = {}
    for t in tests:
        tests_of.setdefault(t["breakout_id"], []).append(t)
    validated = {bid for bid, ts in tests_of.items() if bid and any(is_validation(t) for t in ts)}
    out = []

    def table(head, rows):
        out.append("| " + " | ".join(head) + " |")
        out.append("|" + "---|" * len(head))
        out.extend("| " + " | ".join(str(c) for c in r) + " |" for r in rows)
        out.append("")

    def clashes(b):
        try:
            addrs = parse_addrs(b["addresses"])
        except ValueError:
            addrs = set()
        return ", ".join(sorted({k for a in addrs for k in by_addr.get(a, [])} - set(records_of(b))))

    def prio(b):
        return (PRIORITIES.get(b["priority"], 3), b["category"], b["chip"], b["id"])

    # boards owned with no record - the candidates for new records
    rows = [[b["priority"], b["chip"], b["vendor"], b["product"], b["id"], b["category"], b["addresses"] or "?",
             five_v_ok(b), clashes(b)]
            for b in sorted((b for b in have if not b["raft_record"]), key=prio)]
    out.append(f"## Owned, no RaftCore record ({len(rows)})\n")
    table(["Priority", "Chip", "Vendor", "Product", "Breakout", "Category", "Addresses", "5V ok",
           "Shares address with"], rows)

    # supported and owned but not validated - the test backlog
    rows = []
    for b in sorted((b for b in have if b["raft_record"] and b["id"] not in validated), key=prio):
        done = "; ".join(f"{t['date']} {t['checks']} {t['result']}" for t in tests_of.get(b["id"], []))
        loose = sorted({t["date"] for t in tests_of.get("", []) if t["raft_record"] in records_of(b)})
        if loose:
            done += ("; " if done else "") + "record tested, board not given: " + ", ".join(loose)
        rows.append([b["priority"], b["raft_record"], b["id"], b["product"], clashes(b), done or "none"])
    out.append(f"## Owned and supported, not validated ({len(rows)})\n")
    out.append("Validated: a `pass` in tests.csv for the breakout that checked at least `detect` and `data`.\n")
    table(["Priority", "Record", "Breakout", "Product", "Shares address with", "Tests so far"], rows)

    # validated
    rows = []
    for b in sorted((b for b in breakouts if b["id"] in validated), key=lambda b: (b["raft_record"], b["id"])):
        last = max((t for t in tests_of[b["id"]] if is_validation(t)), key=lambda t: t["date"])
        rows.append([b["raft_record"], b["id"], b["product"], last["date"], last["host"], last["checks"]])
    out.append(f"## Validated ({len(rows)})\n")
    table(["Record", "Breakout", "Product", "Last validated", "Host", "Checks"], rows)

    # not owned - the list to obtain
    rows = []
    for b in sorted((b for b in breakouts if qty.get(b["id"], 0) == 0), key=prio):
        rows.append([b["priority"], b["chip"], b["vendor"], b["product"], b["id"], b["category"],
                     b["raft_record"] or "none", b["status"], b["notes"]])
    out.append(f"## Not owned ({len(rows)})\n")
    table(["Priority", "Chip", "Vendor", "Product", "Breakout", "Category", "Record", "Status", "Notes"], rows)

    # records whose board isn't owned (or isn't listed)
    owned_recs = {k for b in have for k in records_of(b)}
    listed_recs = {k for b in breakouts for k in records_of(b)}
    tested_recs = {t["raft_record"] for t in tests}
    rows = [[k, r["manufacturer"], r["name"], "listed, not owned" if k in listed_recs else "not listed",
             "yes" if k in tested_recs else ""]
            for k, r in sorted(records.items()) if k not in owned_recs]
    out.append(f"## RaftCore records with no owned board ({len(rows)})\n")
    table(["Record", "Manufacturer", "Name", "Breakout list", "Has a test"], rows)

    # tests not tied to a breakout
    loose = tests_of.get("", [])
    if loose:
        rows = []
        for t in loose:
            cands = [b["id"] for b in have if t["raft_record"] in records_of(b)]
            rows.append([t["date"], t["raft_record"], t["checks"], t["host"], ", ".join(cands) or "none owned"])
        out.append(f"## Tests with no breakout_id ({len(loose)})\n")
        table(["Date", "Record", "Checks", "Host", "Owned candidates"], rows)

    n_rec = sum(1 for b in breakouts if b["raft_record"])
    out.insert(0, f"# Breakout report\n\n{len(breakouts)} breakouts listed, {len(have)} owned, {n_rec} map to a "
                  f"RaftCore record, {len(validated)} validated; {len(records)} records, "
                  f"{len(tested_recs)} with a test.\n")
    return "\n".join(out)


def cmd_check(args) -> int:
    breakouts, owned, tests, records = load(args.raftcore, owned_path(args.owned))
    problems = validate(breakouts, owned, tests, records)
    errors = [p for p in problems if not p.startswith("WARNING")]
    for p in problems:
        print(p if p.startswith("WARNING") else f"ERROR {p}", file=sys.stderr)
    if errors:
        return 1
    text = report(breakouts, owned, tests, records)
    if args.out:
        args.out.write_text(text)
        print(f"report written to {args.out}", file=sys.stderr)
    else:
        print(text)
    return 0


# ---------------------------------------------------------------------------------------------------------------
# bench: check a breakout on a host over its REST API
# ---------------------------------------------------------------------------------------------------------------

def api_get(ip: str, path: str) -> dict:
    with urllib.request.urlopen(f"http://{ip}/api/{path}", timeout=10) as resp:
        return json.loads(resp.read().decode("utf-8", errors="replace"))


def split_device_id(device_id: str):
    """'1_76a' -> (1, 0x6a, 7): bus number, I2C address, slot (0 for the main bus). None if not a bus device"""
    m = re.fullmatch(r"(\d+)_([0-9a-fA-F]+)", device_id)
    if not m or m.group(1) == "0":
        return None
    v = int(m.group(2), 16)
    return int(m.group(1)), v & 0x7f, v >> 8


def bench_observe(ip: str, addrs: set) -> dict:
    """What the host reports for devices at the given addresses"""
    info = api_get(ip, "v")
    found = []
    bus_names, bus_status = {}, api_get(ip, "devman/busstatus").get("buses", {})
    for dev in api_get(ip, "devman/listdevs").get("devices", []):
        parts = split_device_id(dev.get("deviceid", ""))
        if not parts or parts[1] not in addrs:
            continue
        bus_num, addr, slot = parts
        if bus_num not in bus_names:
            bus_names[bus_num] = api_get(ip, f"devman/busname?busnum={bus_num}").get("busName", "")
        ident = api_get(ip, "devman/identinfo?deviceid=" + urllib.parse.quote(dev["deviceid"])).get("ident", {})
        key = next((c["key"] for c in ident.get("candidates", []) if c.get("dtIdx") == ident.get("dtIdx")), "")
        timing = bus_status.get(bus_names[bus_num], {}).get("pollTiming", {}).get(dev["deviceid"].split("_", 1)[1], {})
        found.append({"deviceid": dev["deviceid"], "addr": addr, "slot": slot, "key": key,
                      "state": ident.get("state", ""), "online": ident.get("online", ""),
                      "polled": ident.get("polled", False), "polls": timing.get("n", 0),
                      "missed": timing.get("missed", 0), "rateHz": timing.get("rateHz", 0)})
    return {"host": info.get("SystemName", ""), "version": info.get("SystemVersion", ""), "devices": found}


def capture_samples(args, device_ids: list) -> dict:
    """Decoded samples from the host for the given devices (capture-samples.js, run with Node and raftjs)"""
    raftjs = Path(args.raftjs).expanduser()
    if not (raftjs / "dist" / "web" / "main.js").exists():
        raise SystemExit(f"no built raftjs at {raftjs} (--raftjs or RAFTJS)")
    cmd = ["node", str(Path(__file__).resolve().parent / "capture-samples.js"), "--ip", args.ip,
           "--raftjs", str(raftjs), "--duration", str(args.samples), "--devices", ",".join(device_ids)]
    res = subprocess.run(cmd, capture_output=True, text=True, timeout=args.samples + 60)
    if res.returncode:
        raise SystemExit(f"capture failed: {res.stderr.strip()}")
    return json.loads(res.stdout)["devices"]


def fmt_num(v) -> str:
    return "-" if v is None else f"{v:.4g}"


def print_samples(dev: dict) -> list:
    """Print a device's captured attributes; returns the names of those with values outside their range"""
    out_of_range = []
    for name, a in dev["attributes"].items():
        rng = a.get("range") or []
        flag = ""
        if len(rng) == 2 and a["min"] is not None and (a["min"] < rng[0] or a["max"] > rng[1]):
            flag = f"  OUTSIDE RANGE {rng[0]}..{rng[1]}"
            out_of_range.append(name)
        per = f" x{a['elemsPerSample']}" if a["elemsPerSample"] > 1 else ""
        print(f"      {name:>10}{per}: n {a['n']:<5} min {fmt_num(a['min']):>9} mean {fmt_num(a['mean']):>9} "
              f"max {fmt_num(a['max']):>9} {a['units'] or ''}{flag}")
    return out_of_range


def cmd_bench(args) -> int:
    breakouts = {b["id"]: b for b in read_csv(BREAKOUTS)}
    unknown = [i for i in args.breakout_ids if i not in breakouts]
    if unknown:
        print(f"not in {BREAKOUTS.name}: {' '.join(unknown)}", file=sys.stderr)
        return 1
    if args.record and len(args.breakout_ids) > 1:
        print("--record takes one breakout at a time", file=sys.stderr)
        return 1
    plans = []
    for bid in args.breakout_ids:
        b = breakouts[bid]
        recs = args.raft_record.split() if args.raft_record else records_of(b)
        if not recs:
            print(f"{bid} has no raft_record: give the record under test with --raft-record", file=sys.stderr)
            return 1
        plans.append((b, recs, parse_addrs(args.addr or b["addresses"])))
    try:
        obs = bench_observe(args.ip, set().union(*(p[2] for p in plans)))
    except OSError as e:
        print(f"host {args.ip}: {e}", file=sys.stderr)
        return 1
    print(f"host {obs['host']} {obs['version']}")
    results = []
    for b, recs, addrs in plans:
        print(f"\n{b['id']} ({b['product']}), {' '.join(recs)} at {fmt_addrs(addrs)}")
        ok = {}
        here = [d for d in obs["devices"] if d["addr"] in addrs]
        for d in here:
            good = d["key"] in recs and d["state"] == "identified" and d["polled"] and d["polls"] > 0
            print(f"  {'ok' if good else '--'} {d['deviceid']:>7} addr 0x{d['addr']:02x} slot {d['slot']}: "
                  f"{d['key'] or '(none)'} {d['state']} {d['online']}, polled {d['polls']} times, "
                  f"{d['missed']} missed, {d['rateHz']} Hz")
            if good:
                ok.setdefault(d["key"], []).append(d)
        if not here:
            print("  nothing found at those addresses")
        missing = [k for k in recs if k not in ok]
        print(f"  detect: {'pass' if not missing else 'FAIL (' + ' '.join(missing) + ' not found)'}")
        results.append((b, recs, ok, missing))
    if args.samples:
        ids = [d["deviceid"] for _, _, ok, _ in results for ds in ok.values() for d in ds]
        captured = capture_samples(args, ids) if ids else {}
        print(f"\nsamples over {args.samples:g} s:")
        for b, recs, ok, _ in results:
            for key, ds in ok.items():
                for d in ds:
                    print(f"  {b['id']} {key} {d['deviceid']}:")
                    if d["deviceid"] in captured:
                        print_samples(captured[d["deviceid"]])
                    else:
                        print("      no samples received")
    if not args.record:
        if not args.samples:
            print("\n(--samples <s> shows the data; --record pass|partial|fail with --checks and --notes adds "
                  "the tests.csv row)")
        return 0
    b, recs, ok, missing = results[0]
    if missing and args.record == "pass" and not args.force:
        print("not recorded: detection failed, so it can't be a pass (--force to record it anyway)", file=sys.stderr)
        return 1
    checks = args.checks or ("detect" if not missing else "")
    bad = set(checks.split()) - set(CHECKS)
    if not checks or bad:
        print(f"--checks should be one or more of {' '.join(CHECKS)}", file=sys.stderr)
        return 1
    tests = read_csv(TESTS)
    for key in recs:
        ds = ok.get(key, [])
        slots = ", ".join(sorted({f"slot {d['slot']}" if d["slot"] else "main bus" for d in ds}))
        notes = "; ".join(filter(None, [args.notes, slots, f"firmware {obs['version']}" if obs["version"] else ""]))
        row = {"date": args.date, "raft_record": key, "breakout_id": b["id"], "tester": args.tester,
               "host": args.host or obs["host"], "raftcore_version": args.raftcore_version, "checks": checks,
               "result": args.record, "notes": notes}
        tests.append(row)
        print("added: " + ",".join(row[f] for f in TEST_FIELDS))
    write_csv(TESTS, tests, TEST_FIELDS)
    return 0


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
                print(f"{b['id']}: vcc_max {b['vcc_max']} but page says 3-5V: {facts['vcc_evidence'][:120]}")
        elif facts["vcc"] == "3.3V" and b["vcc_max"] and float(b["vcc_max"]) >= 5:
            print(f"{b['id']}: vcc_max {b['vcc_max']} but page suggests 3.3V only: {facts['vcc_evidence'][:120]}")
        page_addrs = set(parse_addrs(facts["addresses"]))
        if page_addrs and not page_addrs & parse_addrs(b["addresses"]):
            print(f"{b['id']}: addresses {b['addresses'] or '(none)'} but page mentions {facts['addresses']}")
        if upd:
            changes += 1
            print(f"{b['id']}: " + ", ".join(f"{k} '{b[k]}' -> '{v}'" for k, v in upd.items()))
            b.update(upd)
    if args.write and changes:
        write_csv(BREAKOUTS, breakouts, BREAKOUT_FIELDS)
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
    bn = sub.add_parser("bench", help="check a breakout on a host and record the test")
    bn.add_argument("breakout_ids", nargs="+", metavar="breakout_id")
    bn.add_argument("--ip", required=True, help="the host's IP address (its REST API)")
    bn.add_argument("--raft-record", help="the record under test (default: the breakout's raft_record)")
    bn.add_argument("--addr", help="the addresses to look at (default: the breakout's), e.g. 0x29 for a jumpered one")
    bn.add_argument("--record", choices=sorted(RESULTS), help="add the row to tests.csv with this result")
    bn.add_argument("--checks", help=f"what was checked, e.g. \"detect data\" (from {' '.join(CHECKS)})")
    bn.add_argument("--notes", default="")
    bn.add_argument("--tester", default="")
    bn.add_argument("--host", default="", help="host name for the row (default: the host's SystemName)")
    bn.add_argument("--raftcore-version", default="", help="the RaftCore version in the host's firmware")
    bn.add_argument("--date", default=datetime.date.today().isoformat())
    bn.add_argument("--force", action="store_true", help="record a pass even though detection failed")
    bn.add_argument("--samples", type=float, default=0, help="capture decoded samples for this many seconds")
    bn.add_argument("--raftjs", default=os.environ.get("RAFTJS", str(Path.home() / "rdev" / "raft" / "raftjs")),
                    help="built raftjs, for --samples (default: $RAFTJS or ~/rdev/raft/raftjs)")
    e = sub.add_parser("enrich", help="fill blank Adafruit fields from adafruit.com")
    e.add_argument("--write", action="store_true", help="save the changes to breakouts.csv")
    args = ap.parse_args()
    return {"check": cmd_check, "bench": cmd_bench, "enrich": cmd_enrich}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
