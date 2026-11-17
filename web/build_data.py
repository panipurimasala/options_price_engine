#!/usr/bin/env python3
"""
Package a validation run into web/data.js for the front end.

Inputs (produced by the pipeline in README_VALIDATION.md):
    validation_data.csv   -- fetched chains (fetch_validation_data.py)
    engine_output.csv     -- C++ engine results (build/market_validation)

For every contract the engine solved, this re-prices it with py_vollib at the
engine's IV and independently solves IV with py_vollib, so the page can show
per-contract agreement. It also runs build/parity_test and records the result.

Usage:
    python web/build_data.py validation_data.csv engine_output.csv \
        --label "Live run, 2026-11-16 12:30 ET"
"""
import argparse
import csv
import datetime as dt
import json
import re
import subprocess
from pathlib import Path

try:
    from vollib.black_scholes import black_scholes
    from vollib.black_scholes.implied_volatility import implied_volatility
except ImportError:
    from py_vollib.black_scholes import black_scholes
    from py_vollib.black_scholes.implied_volatility import implied_volatility

ROOT = Path(__file__).resolve().parent.parent


def key(row):
    return (row["ticker"], row["expiry"], row["type"], round(float(row["K"]), 6))


def parity_suite():
    exe = ROOT / "build" / "parity_test"
    try:
        out = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60).stdout
    except (OSError, subprocess.TimeoutExpired):
        return None
    m = re.search(r"Results: (\d+) passed, (\d+) failed", out)
    parity_cases = len(re.findall(r"Parity: ", out))
    if not m:
        return None
    return {"passed": int(m.group(1)), "failed": int(m.group(2)),
            "parity_cases": parity_cases, "parity_tol": 1e-10}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("validation_csv")
    ap.add_argument("engine_csv")
    ap.add_argument("--label", required=True, help="where/when the quotes came from")
    ap.add_argument("--out", default=str(ROOT / "web" / "data.js"))
    a = ap.parse_args()

    engine = {}
    with open(a.engine_csv) as fh:
        for row in csv.DictReader(fh):
            engine[key(row)] = row

    rows = []
    with open(a.validation_csv, newline="") as fh:
        for row in csv.DictReader(fh):
            row = {k.strip(): v.strip() for k, v in row.items()}   # tolerate \r\n
            S, K, T, r = (float(row[c]) for c in ("S", "K", "T", "r"))
            mid, bid, ask = float(row["mid"]), float(row["bid"]), float(row["ask"])
            flag = row["type"].lower()
            e = engine.get(key(row))
            iv = dprice = div = None
            if e is not None:
                iv = float(e["solved_iv"])
                dprice = abs(black_scholes(flag, S, K, T, r, iv) - float(e["reprice"]))
                try:
                    div = abs(implied_volatility(mid, S, K, T, r, flag) - iv)
                except Exception:
                    div = None
            rows.append([row["ticker"], row["expiry"], row["type"], S, K, T, r,
                         bid, ask, mid, iv, dprice, div])

    data = {
        "label": a.label,
        "generated": dt.datetime.now().isoformat(timespec="minutes"),
        "columns": ["ticker", "expiry", "type", "S", "K", "T", "r",
                    "bid", "ask", "mid", "engine_iv", "price_diff_vs_vollib",
                    "iv_diff_vs_vollib"],
        "rows": rows,
        "parity_suite": parity_suite(),
    }
    Path(a.out).write_text("window.VALIDATION = " + json.dumps(data, separators=(",", ":")) + ";\n")
    solved = sum(1 for r_ in rows if r_[10] is not None)
    print(f"wrote {a.out}: {len(rows)} contracts, {solved} solved by the engine")


if __name__ == "__main__":
    main()
