#!/usr/bin/env python3
"""
Compare the C++ engine's output against an independent reference implementation.

Round-trip error only proves the pricer and the solver agree with each other --
a shared bug cancels out. Agreement with a separate library is the real
correctness claim, and it is the number worth putting on a resume.

Usage:
    pip install py_vollib
    python vol_surface/compare_reference.py engine_output.csv
"""
import csv
import sys

try:
    from vollib.black_scholes import black_scholes
    from vollib.black_scholes.implied_volatility import implied_volatility
except ImportError:
    try:
        from py_vollib.black_scholes import black_scholes
        from py_vollib.black_scholes.implied_volatility import implied_volatility
    except ImportError:
        sys.exit("pip install vollib   (or py_vollib)")


def percentile(values, p):
    if not values:
        return 0.0
    s = sorted(values)
    return s[min(int(p * (len(s) - 1)), len(s) - 1)]


def main(path):
    price_diffs, iv_diffs = [], []
    tickers, expiries, strikes = set(), set(), set()
    skipped = 0

    with open(path) as fh:
        for row in csv.DictReader(fh):
            flag = row["type"].lower()          # py_vollib wants 'c' / 'p'
            S, K = float(row["S"]), float(row["K"])
            T, r = float(row["T"]), float(row["r"])
            mid = float(row["mid"])
            ours_iv = float(row["solved_iv"])
            ours_price = float(row["reprice"])

            # 1. Same IV in, same price out?
            try:
                ref_price = black_scholes(flag, S, K, T, r, ours_iv)
                price_diffs.append(abs(ref_price - ours_price))
            except Exception:
                skipped += 1
                continue

            # 2. Independently solved IV -- does the solver land in the same place?
            try:
                ref_iv = implied_volatility(mid, S, K, T, r, flag)
                iv_diffs.append(abs(ref_iv - ours_iv))
            except Exception:
                pass    # reference solver fails on some deep/illiquid strikes

            tickers.add(row["ticker"])
            expiries.add((row["ticker"], row["expiry"]))
            strikes.add((row["ticker"], row["expiry"], row["K"]))

    if not price_diffs:
        sys.exit("No comparable rows. Check that engine_output.csv is populated.")

    print(f"\n=== vs py_vollib reference ({len(price_diffs)} contracts) ===")
    print(f"  price  max |diff| : {max(price_diffs):.3e}")
    print(f"  price  p99 |diff| : {percentile(price_diffs, 0.99):.3e}")
    print(f"  price  med |diff| : {percentile(price_diffs, 0.50):.3e}")
    if iv_diffs:
        print(f"  IV     max |diff| : {max(iv_diffs):.3e}  ({len(iv_diffs)} solved by both)")
        print(f"  IV     med |diff| : {percentile(iv_diffs, 0.50):.3e}")
    if skipped:
        print(f"  skipped           : {skipped}")

    print(f"\n=== coverage ===")
    print(f"  {len(price_diffs)} contracts | {len(tickers)} tickers | "
          f"{len(expiries)} ticker-expiries | {len(strikes)} strikes")

    worst = max(price_diffs)
    print("\n--- resume line ---")
    print(f"  Validated against an independent reference implementation across")
    print(f"  {len(price_diffs)} live contracts spanning {len(tickers)} tickers and")
    print(f"  {len(expiries)} expiries, agreeing to within {worst:.0e} per contract.")
    if worst > 1e-6:
        print("\n  [!] Worst diff is larger than 1e-6. Investigate before quoting this:")
        print("      check day-count (365 vs 252), the rate convention, and dividend handling.")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "engine_output.csv")
