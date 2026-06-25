// main.cpp
// Prices a real AAPL option from June 16, 2025 and verifies put-call parity.
//
// Market data (June 16 2025, ~3:30pm ET):
//   AAPL spot:        $196.45
//   Strike:           $195.00
//   Expiry:           July 18 2025 (32 days = 32/365 years)
//   Risk-free rate:   5.33% (3-month T-bill)
//   Implied vol:      ~22% (from market option chain)
//
// Market prices (mid):
//   Call: ~$5.80
//   Put:  ~$4.30

#include "include/black_scholes.h"
#include <cmath>
#include <iomanip>
#include <iostream>

// ── helpers ──────────────────────────────────────────────────────────────────

static void printResult(const bs::BSResult& r) {
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "\n=== Black-Scholes Prices ===\n";
    std::cout << "  Call price : $" << r.call << "\n";
    std::cout << "  Put  price : $" << r.put  << "\n";

    std::cout << "\n=== Greeks ===\n";
    std::cout << "  Delta (call) : " << r.delta_call << "\n";
    std::cout << "  Delta (put)  : " << r.delta_put  << "\n";
    std::cout << "  Gamma        : " << r.gamma      << "\n";
    std::cout << "  Vega  (/$1%) : " << r.vega       << "\n";
    std::cout << "  Theta call   : " << r.theta_call << "  (per calendar day)\n";
    std::cout << "  Theta put    : " << r.theta_put  << "  (per calendar day)\n";
    std::cout << "  Rho   call   : " << r.rho_call   << "  (per 1% rate move)\n";
    std::cout << "  Rho   put    : " << r.rho_put    << "  (per 1% rate move)\n";
}

static void printMarketComparison(double bsCall, double bsPut,
                                  double mktCall, double mktPut) {
    std::cout << "\n=== Model vs Market ===\n";
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "  Call — model: $" << bsCall
              << "  market: $" << mktCall
              << "  diff: $" << (bsCall - mktCall) << "\n";
    std::cout << "  Put  — model: $" << bsPut
              << "  market: $" << mktPut
              << "  diff: $" << (bsPut - mktPut) << "\n";
    std::cout << "\n  Note: difference reflects dividends, early exercise\n"
              << "  premium (American options), and bid/ask spread.\n";
}

// ── put-call parity check ────────────────────────────────────────────────────
// Theorem: C - P = S - K * e^(-rT)
// Holds exactly in BS world. We verify it holds for our computed prices
// to within floating-point tolerance, then also check against market prices
// to show where real markets deviate.

static void checkPutCallParity(const bs::BSResult& r, double S, double K, double T, double r_rate, double mktCall, double mktPut) {
    const double tolerance = 1e-10;
    const double discount = std::exp(-r_rate * T);

    // Theoretical RHS: S - K*e^(-rT)
    const double parityRHS   = S - K * discount;

    // LHS from our model
    const double modelLHS    = r.call - r.put;

    // LHS from market mid prices
    const double marketLHS   = mktCall - mktPut;

    const double modelError  = std::abs(modelLHS - parityRHS);
    const double marketError = std::abs(marketLHS - parityRHS);

    std::cout << "\n=== Put-Call Parity Verification ===\n";
    std::cout << std::fixed << std::setprecision(6);
    std::cout << "  Parity RHS  (S - Ke^-rT) : $" << parityRHS  << "\n";
    std::cout << "  Model LHS   (C - P)       : $" << modelLHS   << "\n";
    std::cout << "  Market LHS  (C - P)       : $" << marketLHS  << "\n";
    std::cout << "\n";
    std::cout << "  Model  error : $" << modelError
              << (modelError < tolerance ? "  ✓ PASS" : "  ✗ FAIL") << "\n";
    std::cout << "  Market error : $" << marketError
              << "  (expected — American options + dividends)\n";

    // Hard assert: our math must hold exactly
    if (modelError >= tolerance) {
        std::cerr << "\nFATAL: put-call parity violated in model. "
                  << "Error = " << modelError << "\n";
        std::exit(EXIT_FAILURE);
    }
}

// ── sensitivity table ────────────────────────────────────────────────────────
// Show how call price changes as spot moves ±10% — useful for intuition
// and makes the portfolio README much more concrete.

static void printSpotSensitivity(double K, double T,
                                 double r, double sigma, double S0) {
    std::cout << "\n=== Call Price vs Spot (±10%) ===\n";
    std::cout << std::setw(10) << "Spot"
              << std::setw(12) << "Call"
              << std::setw(12) << "Delta"
              << std::setw(12) << "Gamma" << "\n";
    std::cout << std::string(46, '-') << "\n";

    for (double pct = -0.10; pct <= 0.101; pct += 0.02) {
        double S   = S0 * (1.0 + pct);
        auto   res = bs::blackScholes(S, K, T, r, sigma);
        std::cout << std::fixed << std::setprecision(2)
                  << std::setw(10) << S
                  << std::setw(12) << res.call
                  << std::setw(12) << res.delta_call
                  << std::setw(12) << res.gamma << "\n";
    }
}

static void binomtest(double K, double T,
                                 double r, double sigma, double S) {
                                    std::cout << "\n=== Binomial Tree Convergence to Black-Scholes ===\n";
std::cout << std::string(60, '=') << "\n";

bs::BSResult bsRef = bs::blackScholes(S, K, T, r, sigma);
std::cout << "BS reference call: $" << std::fixed
          << std::setprecision(6) << bsRef.call << "\n\n";

std::cout << std::setw(8)  << "Steps"
          << std::setw(14) << "Tree (Eur)"
          << std::setw(14) << "Error"
          << std::setw(14) << "Tree (Amer)" << "\n";
std::cout << std::string(50, '-') << "\n";

for (int n : {5, 10, 25, 50, 100, 250, 500, 1000}) {
    auto eur  = bs::binomialTree(S, K, T, r, sigma,
                                 bs::OptionType::Call,
                                 bs::ExerciseType::European, n);
    auto amer = bs::binomialTree(S, K, T, r, sigma,
                                 bs::OptionType::Call,
                                 bs::ExerciseType::American, n);

    std::cout << std::setw(8)  << n
              << std::setw(14) << eur.price
              << std::setw(14) << (eur.price - bsRef.call)
              << std::setw(14) << amer.price << "\n";
}

// American put — the interesting case
// For a call on a non-dividend stock, early exercise is never optimal
// so American call == European call. For puts it differs.
std::cout << "\n--- American vs European put (early exercise premium) ---\n";
auto eurPut  = bs::binomialTree(S, K, T, r, sigma,
                                bs::OptionType::Put,
                                bs::ExerciseType::European, 500);
auto amerPut = bs::binomialTree(S, K, T, r, sigma,
                                bs::OptionType::Put,
                                bs::ExerciseType::American, 500);

std::cout << "European put : $" << eurPut.price  << "\n";
std::cout << "American put : $" << amerPut.price << "\n";
std::cout << "Early exercise premium: $"
          << (amerPut.price - eurPut.price) << "\n";}
// ── main ─────────────────────────────────────────────────────────────────────



int main() {
    // Real AAPL market inputs — June 16 2025
    constexpr double S     = 196.45;   // spot
    constexpr double K     = 195.00;   // strike
    constexpr double T     = 32.0 / 365.0; // days to expiry
    constexpr double r     = 0.0533;   // 3-month T-bill rate
    constexpr double sigma = 0.22;     // implied vol from market chain

    // Market mid prices (bid/ask midpoint, June 16 2025 ~3:30pm ET)
    constexpr double mktCall = 5.80;
    constexpr double mktPut  = 4.30;

    std::cout << "Black-Scholes Pricer — AAPL $195 strike, Jul 18 2025 expiry\n";
    std::cout << std::string(60, '=') << "\n";
    std::cout << "Inputs: S=$" << S << "  K=$" << K
              << "  T=" << T * 365 << "d  r=" << r * 100
              << "%  σ=" << sigma * 100 << "%\n";

    bs::BSResult result = bs::blackScholes(S, K, T, r, sigma);

    printResult(result);
    printMarketComparison(result.call, result.put, mktCall, mktPut);
    checkPutCallParity(result, S, K, T, r, mktCall, mktPut);
    printSpotSensitivity(K, T, r, sigma, S);
    binomtest(K, T, r, sigma, S);

    return 0;
}