// tests/parity_test.cpp
// Put-call parity and boundary condition tests.
// Compile: g++ -std=c++20 -I.. parity_test.cpp ../black_scholes.cpp -o parity_test

#include "../include/black_scholes.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

// ── test harness ─────────────────────────────────────────────────────────────

static int passed = 0;
static int failed = 0;

static void check(bool condition, const std::string& name) {
    if (condition) {
        std::cout << "  PASS  " << name << "\n";
        ++passed;
    } else {
        std::cout << "  FAIL  " << name << "\n";
        ++failed;
    }
}

// ── put-call parity ───────────────────────────────────────────────────────────
// C - P = S - K*e^(-rT) must hold to floating-point precision

static void testPutCallParity() {
    std::cout << "\n[Put-Call Parity]\n";
    constexpr double TOL = 1e-10;

    struct Case { double S, K, T, r, sigma; std::string label; };
    std::vector<Case> cases = {
        {100, 100, 1.0,  0.05, 0.20, "ATM 1yr"},
        {100,  80, 0.5,  0.05, 0.30, "ITM call"},
        {100, 120, 0.5,  0.05, 0.30, "OTM call"},
        {200, 150, 2.0,  0.03, 0.15, "deep ITM long expiry"},
        {100, 100, 0.02, 0.05, 0.50, "very short expiry high vol"},
        {100, 100, 1.0,  0.0,  0.20, "zero rates"},
        {100, 100, 1.0, -0.01, 0.20, "negative rates"},  // real world!
        {50,  200, 1.0,  0.05, 0.40, "deep OTM call"},
    };

    for (const auto& c : cases) {
        auto res = bs::blackScholes(c.S, c.K, c.T, c.r, c.sigma);
        double lhs = res.call - res.put;
        double rhs = c.S - c.K * std::exp(-c.r * c.T);
        check(std::abs(lhs - rhs) < TOL, "Parity: " + c.label);
    }
}

// ── boundary conditions ───────────────────────────────────────────────────────

static void testBoundaryConditions() {
    std::cout << "\n[Boundary Conditions]\n";

    // Call price >= max(S - K*e^(-rT), 0)  (no-arbitrage lower bound)
    {
        auto res     = bs::blackScholes(100, 90, 1.0, 0.05, 0.20);
        double bound = std::max(100.0 - 90.0 * std::exp(-0.05), 0.0);
        check(res.call >= bound - 1e-10, "Call >= intrinsic lower bound");
    }

    // Put price >= max(K*e^(-rT) - S, 0)
    {
        auto res     = bs::blackScholes(90, 100, 1.0, 0.05, 0.20);
        double bound = std::max(100.0 * std::exp(-0.05) - 90.0, 0.0);
        check(res.put >= bound - 1e-10, "Put >= intrinsic lower bound");
    }

    // Call <= S  (can never be worth more than the stock)
    {
        auto res = bs::blackScholes(100, 50, 1.0, 0.05, 0.20);
        check(res.call <= 100.0 + 1e-10, "Call <= S");
    }

    // Put <= K*e^(-rT)  (can never be worth more than PV of strike)
    {
        auto res    = bs::blackScholes(100, 120, 1.0, 0.05, 0.20);
        double maxP = 120.0 * std::exp(-0.05);
        check(res.put <= maxP + 1e-10, "Put <= K*e^(-rT)");
    }

    // Both prices must be non-negative
    {
        auto res = bs::blackScholes(100, 100, 1.0, 0.05, 0.20);
        check(res.call >= 0.0, "Call >= 0");
        check(res.put  >= 0.0, "Put  >= 0");
    }
}

// ── greeks sign checks ────────────────────────────────────────────────────────

static void testGreekSigns() {
    std::cout << "\n[Greek Signs]\n";
    auto res = bs::blackScholes(100, 100, 1.0, 0.05, 0.20);

    check(res.delta_call >  0.0 && res.delta_call < 1.0, "Call delta in (0,1)");
    check(res.delta_put  < 0.0 && res.delta_put  > -1.0, "Put delta in (-1,0)");
    check(res.gamma      >  0.0,                          "Gamma > 0");
    check(res.vega       >  0.0,                          "Vega > 0");
    check(res.theta_call <  0.0,                          "Call theta < 0 (time decay)");
    check(res.theta_put  <  0.0,                          "Put theta < 0");
    check(res.rho_call   >  0.0,                          "Call rho > 0");
    check(res.rho_put    <  0.0,                          "Put rho < 0");

    // Delta call - delta put = 1 (follows from put-call parity)
    check(std::abs((res.delta_call - res.delta_put) - 1.0) < 1e-10,
          "Delta_call - Delta_put = 1");
}

// ── greek monotonicity ────────────────────────────────────────────────────────
// Call delta increases as spot increases — basic sanity

static void testDeltaMonotonicity() {
    std::cout << "\n[Monotonicity]\n";
    double prevDelta = -1.0;
    bool   mono      = true;

    for (double S = 80; S <= 120; S += 5) {
        auto   res = bs::blackScholes(S, 100, 1.0, 0.05, 0.20);
        if (res.delta_call <= prevDelta) { mono = false; break; }
        prevDelta = res.delta_call;
    }
    check(mono, "Call delta increases with spot");
}

// ── input validation ──────────────────────────────────────────────────────────

static void testInputValidation() {
    std::cout << "\n[Input Validation]\n";
    auto throws = [](auto fn) -> bool {
        try { fn(); return false; }
        catch (const std::invalid_argument&) { return true; }
    };

    check(throws([] { bs::blackScholes(100, 100,  0.0, 0.05, 0.20); }), "T=0 throws");
    check(throws([] { bs::blackScholes(100, 100, -1.0, 0.05, 0.20); }), "T<0 throws");
    check(throws([] { bs::blackScholes(100, 100,  1.0, 0.05,  0.0); }), "sigma=0 throws");
    check(throws([] { bs::blackScholes(  0, 100,  1.0, 0.05, 0.20); }), "S=0 throws");
    check(throws([] { bs::blackScholes(100,   0,  1.0, 0.05, 0.20); }), "K=0 throws");
}

static void testImpliedVol() {
    std::cout << "\n[Implied Volatility Solver]\n";
    constexpr double TOL = 1e-4; // IV within 0.01%

    struct Case {
        double S, K, T, r, sigma;
        std::string label;
    };

    std::vector<Case> cases = {
        {100, 100, 1.0,  0.05, 0.20, "ATM"},
        {100,  90, 1.0,  0.05, 0.25, "ITM call"},
        {100, 110, 1.0,  0.05, 0.30, "OTM call"},
        {100, 100, 0.1,  0.05, 0.20, "short expiry"},
        {100, 100, 2.0,  0.05, 0.15, "long expiry"},
        {100, 100, 1.0,  0.05, 0.60, "high vol"},
        {100, 100, 1.0, -0.01, 0.20, "negative rates"},
    };

    for (const auto& c : cases) {
        // Price with known sigma
        auto ref      = bs::blackScholes(c.S, c.K, c.T, c.r, c.sigma);

        // Recover sigma from that price
        auto callResult = bs::impliedVol(ref.call, c.S, c.K, c.T, c.r,
                                         bs::OptionType::Call);
        auto putResult  = bs::impliedVol(ref.put,  c.S, c.K, c.T, c.r,
                                         bs::OptionType::Put);

        bool callOK = callResult.converged &&
                      std::abs(callResult.iv - c.sigma) < TOL;
        bool putOK  = putResult.converged  &&
                      std::abs(putResult.iv  - c.sigma) < TOL;

        check(callOK, "IV call: " + c.label +
              " (" + std::to_string(callResult.iterations) + " iters)");
        check(putOK,  "IV put:  " + c.label +
              " (" + std::to_string(putResult.iterations) + " iters)");
    }
}

// ── main ─────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "Running Black-Scholes test suite\n";
    std::cout << std::string(50, '=') << "\n";

    testPutCallParity();
    testBoundaryConditions();
    testGreekSigns();
    testDeltaMonotonicity();
    testInputValidation();
    testImpliedVol();
    std::cout << "\n" << std::string(50, '=') << "\n";
    std::cout << "Results: " << passed << " passed, " << failed << " failed\n";

    return failed > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}

