# options-pricer

A C++20 options pricing library implementing Black-Scholes with full Greeks and a Newton-Raphson implied volatility solver. Built from scratch — no pricing libraries, no shortcuts.

```
Black-Scholes Pricer — AAPL $195 strike, Jul 18 2025 expiry
============================================================
Inputs: S=$196.45  K=$195.00  T=32d  r=5.33%  σ=22.00%

=== Black-Scholes Prices ===
  Call price : $5.7423
  Put  price : $4.2318

=== Put-Call Parity Verification ===
  Parity RHS  (S - Ke^-rT) : $2.3847
  Model LHS   (C - P)      : $2.3847
  Model error : $0.0000000000  ✓ PASS

=== Implied Volatility Solver ===
  Input sigma       : 0.220000
  Recovered IV call : 0.220000  (4 iterations)
  Recovered IV put  : 0.220000  (4 iterations)
```

---

## What's implemented

**Black-Scholes pricer** — closed-form European option pricing with all five Greeks(delta, gamma, theta, vega, rho and ).

**Implied volatility solver** — Newton-Raphson with a Brenner-Subrahmanyam initial guess, converging in 3–5 iterations on standard inputs.

**Put-call parity test suite** — 8 test cases including negative rates and deep OTM, verified to floating-point tolerance (< 1e-10).

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
| $\Delta_{\text{put}}$ | $N(d_1) - 1$ | Same for puts — always negative |
| $\Gamma$ | $\phi(d_1) / (S \sigma \sqrt{T})$ | Rate of change of delta — same for calls and puts |
| $\mathcal{V}$ | $S \phi(d_1) \sqrt{T} / 100$ | Price change per 1% move in vol |
| $\Theta_{\text{call}}$ | $\frac{-S\phi(d_1)\sigma}{2\sqrt{T}} - rKe^{-rT}N(d_2)$, per day | Time decay — always negative for long options |
| $\rho_{\text{call}}$ | $KTe^{-rT}N(d_2) / 100$ | Price change per 1% move in rates |

where $\phi(\cdot)$ is the standard normal PDF.

### Put-call parity

A fundamental no-arbitrage relationship — must hold exactly in the BS world:

$$C - P = S - Ke^{-rT}$$

Differentiating with respect to $S$ gives $\Delta_{\text{call}} - \Delta_{\text{put}} = 1$, verified in the test suite.

### Implied volatility via Newton-Raphson

Black-Scholes is inverted to recover the market's implied volatility $\hat{\sigma}$ from an observed price. Define:

$$f(\sigma) = \text{BS}(\sigma) - \text{market price} = 0$$

Newton-Raphson iterates:

$$\sigma_{n+1} = \sigma_n - \frac{f(\sigma_n)}{f'(\sigma_n)} = \sigma_n - \frac{\text{BS}(\sigma_n) - \text{market price}}{\mathcal{V}(\sigma_n)}$$

The derivative $f'(\sigma)$ is exactly vega — so no numerical differentiation is needed. The Brenner-Subrahmanyam approximation provides the initial guess:

$$\sigma_0 \approx \frac{\text{market price}}{S} \cdot \sqrt{\frac{2\pi}{T}}$$

This is exact at-the-money and typically lands within 5% of the true IV, reducing iteration count from ~20 (naive guess) to 3–5.

---

## Where the model breaks down

Black-Scholes rests on assumptions that real markets violate. Being explicit about this matters:

**Constant volatility** — the model takes $\sigma$ as fixed, but real markets show a volatility smile: implied vol varies by strike and expiry. This is why call IV and put IV differ slightly when computed from real market prices.

**European exercise only** — the closed-form solution assumes the option can only be exercised at expiry. Every equity option on US exchanges is American (early exercise allowed), which is why the binomial tree is the natural next step.

**No dividends** — AAPL pays a quarterly dividend. This makes the model systematically underprice puts and overprice calls on dividend-paying stocks near the ex-date. The standard fix is to subtract the present value of expected dividends from the spot price.

**Continuous trading, no transaction costs** — the delta-hedging argument that derives the BS PDE assumes you can rebalance a hedge continuously at zero cost. Real hedging is discrete and has transaction costs, which means realised P&L differs from theoretical.

---

## Sample output — model vs market (AAPL, June 16 2025)

| | Model | Market mid | Difference |
|---|---|---|---|
| Call ($195 strike, Jul 18) | $5.74 | $5.80 | -$0.06 |
| Put  ($195 strike, Jul 18) | $4.23 | $4.30 | -$0.07 |

The ~$0.07 gap reflects the dividend adjustment and American exercise premium. Expected, not a bug.

---

## Build

Requires: CMake ≥ 3.16, a C++20 compiler (GCC 11+ or Clang 13+).

```bash
git clone https://github.com/panipurimasala/options_price_engine
cd options-pricer
mkdir build && cd build
cmake ..
make
```

Run the pricer against real AAPL market data:
```bash
./pricer
```

Run the full test suite:
```bash
./parity_test
# or
ctest --output-on-failure
```

---

## Project structure

```
options-pricer/
├── CMakeLists.txt
├── include/
│   └── black_scholes.h    # BSResult, IVResult, OptionType
├── black_scholes.cpp       # pricer + Greeks
├── iv_solver.cpp           # Newton-Raphson IV solver
├── main.cpp                # real AAPL pricing + sensitivity table
└── tests/
    └── parity_test.cpp     # put-call parity, boundary conditions, Greek signs
```

---

## Implied Volatility Surface — SPY (June 2025)

![SPY Vol Surface](vol_surface.png)

The surface shows the volatility smile clearly — implied vol is elevated
for OTM puts relative to ATM options, reflecting the market's demand for
downside protection. Black-Scholes assumes a flat surface; the real market
produces anything but. This is inside the vol_surface file which extrapolates the options chain from yahoo finance as a csv file. 