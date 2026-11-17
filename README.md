# options_price_engine

A C++20 options pricing library built from scratch, with no pricing libraries:
Black-Scholes with full Greeks, a Cox-Ross-Rubinstein binomial tree for European
and American exercise, and a safeguarded Newton-Raphson implied-volatility solver.
It is validated against an independent reference implementation (py_vollib) on
real option chains, and has an interactive browser front end.

## What's implemented

- **Black-Scholes pricer**: closed-form European pricing with all five Greeks (delta, gamma, theta, vega, rho).
- **Binomial tree pricer**: Cox-Ross-Rubinstein, European and American exercise.
- **Implied volatility solver**: Newton-Raphson with a Brenner-Subrahmanyam initial guess, safeguarded by a bracket and bisection fallback so it converges on out-of-the-money contracts too.
- **Test suite**: 43 checks, including 8 put-call parity cases (negative rates, deep OTM, very short expiry) held to < 1e-10.
- **Market validation harness**: pulls live chains from Yahoo Finance, solves IV for every contract, and compares against py_vollib.
- **Front end**: an interactive pricer, a step-by-step IV solver trace, the volatility smile from real chains, and the validation results.

## Validation results

Sample run on Friday 13 Nov 2026 closing quotes (8 tickers: SPY, QQQ, AAPL, MSFT, NVDA, AMZN, META, GOOGL):

| | |
|---|---|
| Contracts solved | 1,179 of 1,187 fetched (the other 8 quotes break no-arbitrage bounds) |
| Coverage | 8 tickers, 36 ticker-expiries, 858 (ticker, expiry, strike) combinations |
| Price agreement vs py_vollib | max 3.1e-9, p99 2.3e-9, median 4.5e-10 per contract |
| IV agreement vs py_vollib | max 2.4e-7, median 4.8e-11 |
| Solver convergence | 100% of valid quotes |

The engine and py_vollib get identical inputs (same S, K, T, r and σ), so this
measures whether the math is right. It does **not** claim the engine matches
market prices: constant-volatility Black-Scholes is not supposed to, and the gap
is the volatility smile. See [README_VALIDATION.md](README_VALIDATION.md) for the
pipeline and how to read its output.

---

## The math

### Black-Scholes formula

For a European call on a non-dividend-paying stock:

$$C = S \cdot N(d_1) - K e^{-rT} \cdot N(d_2)$$

$$P = K e^{-rT} \cdot N(-d_2) - S \cdot N(-d_1)$$

where:

$$d_1 = \frac{\ln(S/K) + (r + \frac{1}{2}\sigma^2)T}{\sigma\sqrt{T}}, \qquad d_2 = d_1 - \sigma\sqrt{T}$$

and $N(\cdot)$ is the standard normal CDF, implemented via `std::erfc` for numerical stability.

### Greeks

| Greek | Formula | Intuition |
|---|---|---|
| $\Delta_{\text{call}}$ | $N(d_1)$ | How much the call price moves per $1 move in spot |
| $\Delta_{\text{put}}$ | $N(d_1) - 1$ | Same for puts, always negative |
| $\Gamma$ | $\phi(d_1) / (S \sigma \sqrt{T})$ | Rate of change of delta, same for calls and puts |
| $\mathcal{V}$ | $S \phi(d_1) \sqrt{T} / 100$ | Price change per 1 point move in vol |
| $\Theta_{\text{call}}$ | $\frac{-S\phi(d_1)\sigma}{2\sqrt{T}} - rKe^{-rT}N(d_2)$, per day | Time decay, always negative for long options |
| $\rho_{\text{call}}$ | $KTe^{-rT}N(d_2) / 100$ | Price change per 1% move in rates |

where $\phi(\cdot)$ is the standard normal PDF.

### Put-call parity

A fundamental no-arbitrage relationship that must hold exactly in the Black-Scholes world:

$$C - P = S - Ke^{-rT}$$

Differentiating with respect to $S$ gives $\Delta_{\text{call}} - \Delta_{\text{put}} = 1$, verified in the test suite.

### Binomial tree (Cox-Ross-Rubinstein)

The tree splits time to expiry into $N$ steps of $\Delta t = T/N$. At each step the
stock moves up by $u$ or down by $d$:

$$u = e^{\sigma\sqrt{\Delta t}}, \qquad d = 1/u, \qquad p = \frac{e^{r\Delta t} - d}{u - d}$$

where $p$ is the risk-neutral probability of an up move. Option values start from
the payoff at expiry and are discounted back one step at a time. For American
exercise, each node takes the larger of the continuation value and the
immediate-exercise value, which is what gives the early-exercise premium. As $N$
grows, the European tree price converges to the Black-Scholes closed form.

### Implied volatility via Newton-Raphson

Black-Scholes is inverted to recover the market's implied volatility $\hat{\sigma}$ from an observed price. Define:

$$f(\sigma) = \text{BS}(\sigma) - \text{market price} = 0$$

Newton-Raphson iterates:

$$\sigma_{n+1} = \sigma_n - \frac{f(\sigma_n)}{f'(\sigma_n)} = \sigma_n - \frac{\text{BS}(\sigma_n) - \text{market price}}{\mathcal{V}(\sigma_n)}$$

The derivative $f'(\sigma)$ is exactly vega, so no numerical differentiation is
needed. The Brenner-Subrahmanyam approximation provides the initial guess, with
Corrado-Miller as a fallback when it lands outside [0.005, 5]:

$$\sigma_0 \approx \frac{\text{market price}}{S} \cdot \sqrt{\frac{2\pi}{T}}$$

**Why the solver needs a safeguard.** That guess is accurate at the money but far
too low out of the money (0.03 for an 8-day SPY call whose true IV is about 0.12).
There, price and vega are both close to zero, so a raw Newton step shoots to the
upper bound and the next one crashes to the lower bound, where vega underflows.
Plain Newton-Raphson failed on 384 of 1,179 real contracts this way.

The fix uses the fact that price is strictly increasing in σ. Every evaluation
tightens a bracket $[\sigma_{lo}, \sigma_{hi}]$ known to contain the answer, and
any Newton step that would leave the bracket (or any near-zero vega) is replaced
by a bisection step. When Newton behaves, the path is unchanged.

**Iteration counts** on the 1,179 contracts above (model evaluations, including the final convergence check):

| Starting guess | Median | p95 | Max | Mean |
|---|---|---|---|---|
| Brenner-Subrahmanyam (default) | 5 | 8 | 10 | 5.27 |
| Corrado-Miller | 4 | 8 | 10 | 4.60 |
| Flat 0.3 | 4 | 7 | 10 | 4.33 |

On this sample, which leans short-dated and out of the money, the
at-the-money-style guess does not beat a flat starting point.

---

## Where the model breaks down

Black-Scholes rests on assumptions that real markets violate. Being explicit about this matters:

**Constant volatility**: the model takes $\sigma$ as fixed, but real markets show a volatility smile: implied vol varies by strike and expiry. This is why call IV and put IV differ slightly when computed from real market prices.

**European exercise only**: the closed-form solution assumes the option can only be exercised at expiry. Every equity option on US exchanges is American (early exercise allowed), which is what the binomial tree's American mode handles.

**No dividends**: AAPL pays a quarterly dividend. This makes the model systematically underprice puts and overprice calls on dividend-paying stocks near the ex-date. The standard fix is to subtract the present value of expected dividends from the spot price, or to add a continuous dividend yield (Merton's extension).

**Continuous trading, no transaction costs**: the delta-hedging argument that derives the BS PDE assumes you can rebalance a hedge continuously at zero cost. Real hedging is discrete and has transaction costs, which means realised P&L differs from theoretical.

---

## Sample output: model vs market (AAPL, June 16 2025)

| | Model | Market mid | Difference |
|---|---|---|---|
| Call ($195 strike, Jul 18) | $5.74 | $5.80 | -$0.06 |
| Put  ($195 strike, Jul 18) | $4.23 | $4.30 | -$0.07 |

The ~$0.07 gap reflects the dividend adjustment and American exercise premium. Expected, not a bug.

---

## Build

Requires CMake ≥ 3.16 and a C++20 compiler (GCC 11+ or Clang 13+). All targets
build with `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror`.

```bash
git clone https://github.com/panipurimasala/options_price_engine
cd options_price_engine
cmake -S . -B build
cmake --build build
```

Run the test suite:

```bash
./build/parity_test
# or
ctest --test-dir build --output-on-failure
```

Run the pricer against AAPL market data:

```bash
./build/pricer
```

## Validate against real market data

Run during US market hours (weekdays, 9:30–16:00 ET). Outside them, quotes are
stale and the liquidity filters drop most contracts.

```bash
pip install yfinance vollib
python vol_surface/fetch_validation_data.py                     # -> validation_data.csv
./build/market_validation validation_data.csv engine_output.csv
python vol_surface/compare_reference.py engine_output.csv       # agreement vs py_vollib
```

## Front end

`web/` holds an interactive page: a pricer with Greeks and tree convergence,
an IV solver that shows every iteration, the volatility smile for each ticker,
and the validation results. It is static HTML and JavaScript with no backend.

```bash
python web/build_static.py
python -m http.server 8000 -d web/dist      # open http://localhost:8000
```

To refresh it with a new validation run:

```bash
python web/build_data.py validation_data.csv engine_output.csv --label "Live run, <date> <time> ET"
python web/build_static.py
```

The page runs `web/engine.js`, a line-for-line JavaScript port of the C++ engine.
The port is checked against the compiled C++: prices and Greeks agree within
2.3e-13, tree prices within 1.4e-14, and the IV solver takes identical iteration
counts on every validation contract. Re-run the check after changing the C++:

```bash
c++ -std=c++20 -O2 -Iinclude web/verify_port.cpp build/libbs_core.a -o build/verify_port
./build/verify_port validation_data.csv > build/port_vectors.jsonl
node web/verify_port.mjs build/port_vectors.jsonl
```

## Implied volatility surface

`functions/vol_surface.cpp` solves IV across a whole option chain and
`vol_surface/plot_surface.py` plots the result:

```bash
python3 vol_surface/fetch_data.py SPY        # writes SPY_options_chain.csv
./build/vol_surface SPY_options_chain.csv iv_surface.csv
python3 vol_surface/plot_surface.py          # reads iv_surface.csv, writes vol_surface.png
```

Implied vol is elevated for OTM puts relative to ATM options, reflecting the
market's demand for downside protection. Black-Scholes assumes a flat surface;
the real market produces anything but.

---

## Project structure

```
options_price_engine/
├── CMakeLists.txt
├── include/
│   └── black_scholes.h          # BSResult, IVResult, TreeResult, OptionType, ExerciseType
├── functions/
│   ├── black_scholes.cpp        # closed form + Greeks
│   ├── binomial_tree.cpp        # Cox-Ross-Rubinstein tree
│   ├── iv_solver.cpp            # safeguarded Newton-Raphson
│   └── vol_surface.cpp          # IV across a chain
├── tests/
│   ├── parity_test.cpp          # parity, bounds, Greek signs, IV recovery
│   └── market_validation.cpp    # engine over live chains: coverage + round trip
├── vol_surface/
│   ├── fetch_data.py
│   ├── fetch_validation_data.py # liquid chains across 8 tickers
│   ├── compare_reference.py     # agreement vs py_vollib
│   └── plot_surface.py
├── web/
│   ├── index.html               # front end
│   ├── engine.js                # JavaScript port of the engine
│   ├── data.js                  # packaged validation run
│   ├── build_data.py            # validation CSVs -> data.js
│   ├── build_static.py          # standalone site -> web/dist/
│   └── verify_port.cpp/.mjs     # port vs C++ check
├── main.cpp                     # AAPL pricing + sensitivity table
├── README.md
└── README_VALIDATION.md
```
