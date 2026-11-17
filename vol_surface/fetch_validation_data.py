#!/usr/bin/env python3
"""
Pull liquid option chains across several tickers and expiries into one CSV.

This is the input to the validation harness. Keeping the universe liquid
matters: wide/stale quotes produce garbage "errors" that say nothing about
whether the pricing engine is correct.

Usage:
    python vol_surface/fetch_validation_data.py
    python vol_surface/fetch_validation_data.py --tickers SPY QQQ AAPL --max-expiries 6

Output: validation_data.csv
"""
import argparse
import csv
import datetime as dt
import sys

try:
    import yfinance as yf
except ImportError:
    sys.exit("pip install yfinance")

DEFAULT_TICKERS = ["SPY", "QQQ", "AAPL", "MSFT", "NVDA", "AMZN", "META", "GOOGL"]

# Liquidity / sanity filters. Tighten rather than loosen -- a smaller clean
# sample gives a more defensible number than a large dirty one.
MIN_BID = 0.10          # drop near-worthless contracts (quantization dominates)
MAX_REL_SPREAD = 0.20   # (ask-bid)/mid -- wide markets have no reliable "price"
MIN_OPEN_INTEREST = 50
MONEYNESS_LO, MONEYNESS_HI = 0.80, 1.20   # K/S band
MIN_DAYS, MAX_DAYS = 7, 270               # avoid expiry-day and illiquid LEAPs


def risk_free_rate() -> float:
    """13-week T-bill yield as a decimal; falls back to a fixed value."""
    try:
        hist = yf.Ticker("^IRX").history(period="5d")
        if not hist.empty:
            return float(hist["Close"].iloc[-1]) / 100.0
    except Exception:
        pass
    print("  [warn] could not fetch ^IRX, defaulting r=0.04", file=sys.stderr)
    return 0.04


def spot_price(tk: yf.Ticker) -> float:
    hist = tk.history(period="1d")
    if hist.empty:
        raise ValueError("no spot data")
    return float(hist["Close"].iloc[-1])


def collect(tickers, max_expiries, out_path):
    r = risk_free_rate()
    today = dt.date.today()
    rows = []

    for symbol in tickers:
        try:
            tk = yf.Ticker(symbol)
            S = spot_price(tk)
            expiries = list(tk.options)[:max_expiries]
        except Exception as e:
            print(f"  [skip] {symbol}: {e}", file=sys.stderr)
            continue

        kept_for_symbol = 0
        for expiry in expiries:
            days = (dt.date.fromisoformat(expiry) - today).days
            if not (MIN_DAYS <= days <= MAX_DAYS):
                continue
            T = days / 365.0

            try:
                chain = tk.option_chain(expiry)
            except Exception as e:
                print(f"  [skip] {symbol} {expiry}: {e}", file=sys.stderr)
                continue

            for opt_type, frame in (("C", chain.calls), ("P", chain.puts)):
                for _, row in frame.iterrows():
                    bid, ask = float(row.bid or 0), float(row.ask or 0)
                    if bid < MIN_BID or ask <= bid:
                        continue
                    mid = 0.5 * (bid + ask)
                    if (ask - bid) / mid > MAX_REL_SPREAD:
                        continue
                    if float(row.openInterest or 0) < MIN_OPEN_INTEREST:
                        continue
                    K = float(row.strike)
                    if not (MONEYNESS_LO <= K / S <= MONEYNESS_HI):
                        continue

                    rows.append({
                        "ticker": symbol, "expiry": expiry, "type": opt_type,
                        "S": f"{S:.6f}", "K": f"{K:.6f}", "T": f"{T:.8f}",
                        "r": f"{r:.6f}", "bid": f"{bid:.4f}", "ask": f"{ask:.4f}",
                        "mid": f"{mid:.6f}",
                        "market_iv": f"{float(row.impliedVolatility or 0):.6f}",
                    })
                    kept_for_symbol += 1

        print(f"  {symbol}: {kept_for_symbol} contracts")

    if not rows:
        sys.exit("No contracts survived filtering. Markets may be closed, or loosen the filters.")

    with open(out_path, "w", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)

    tickers_seen = {r_["ticker"] for r_ in rows}
    expiries_seen = {(r_["ticker"], r_["expiry"]) for r_ in rows}
    strikes_seen = {(r_["ticker"], r_["expiry"], r_["K"]) for r_ in rows}

    print(f"\nWrote {out_path}")
    print(f"  {len(rows)} contracts | {len(tickers_seen)} tickers | "
          f"{len(expiries_seen)} ticker-expiries | {len(strikes_seen)} strikes")
    print(f"  risk-free rate used: {r:.4%}")


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--tickers", nargs="+", default=DEFAULT_TICKERS)
    p.add_argument("--max-expiries", type=int, default=8)
    p.add_argument("--out", default="validation_data.csv")
    a = p.parse_args()
    collect(a.tickers, a.max_expiries, a.out)
