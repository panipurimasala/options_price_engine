# Market validation harness

Produces the numbers for the resume bullet: how closely the engine matches an
independent reference, and how many tickers/expiries the surface covers.

## Files

| File | Purpose |
|---|---|
| `vol_surface/fetch_validation_data.py` | Pulls liquid option chains → `validation_data.csv` |
| `tests/market_validation.cpp` | Runs the engine over them → `engine_output.csv` + coverage/round-trip stats |
| `vol_surface/compare_reference.py` | Compares engine output vs `vollib` → the headline accuracy number |

## Build

The `market_validation` target is already in `CMakeLists.txt`, linked against
`bs_core` with `include/` on the include path:

```bash
cmake -S . -B build && cmake --build build --target market_validation
```

## Running it

```bash
pip install yfinance vollib

python vol_surface/fetch_validation_data.py          # run while US markets are open
./build/market_validation validation_data.csv engine_output.csv
python vol_surface/compare_reference.py engine_output.csv
```

Run it **during market hours**. Outside them, Yahoo serves the previous close.
That is fine for testing the pipeline, but the result is not live data and
should not be quoted as such.

## Reading the output

- **Round-trip error** (C++ step) — solve IV from the mid, re-price, recover the
  mid. Bounded by the solver's stopping tolerance of 1e-6 on price, so expect a
  max just under 1e-6 and a median around 1e-10. This only proves the pricer and solver
  agree *with each other*; a shared bug cancels out, which is why the next step exists.
- **Reference agreement** (Python step) — the number worth quoting. Below ~1e-6
  per contract means the engine is correct. Larger means a convention mismatch:
  check day-count (365 vs 252), the rate convention, and dividend handling.
- **Parity residual on market quotes** — this reflects bid/ask frictions, dividends
  and American exercise, *not* engine error. Don't quote it as accuracy; the
  existing synthetic parity suite (8 cases, <1e-10) is the clean correctness claim.

## What goes on the resume

Use the reference-agreement figure and the coverage counts, e.g.:

> Validated against an independent reference implementation across N live
> contracts spanning M tickers and K expiries, agreeing to within 1e-X per contract.

Do **not** claim the engine "matches market prices." A constant-volatility
Black-Scholes model is not supposed to match market prices — the gap *is* the
smile. Matching a reference implementation on identical inputs is the defensible
correctness claim; matching the market is not.
