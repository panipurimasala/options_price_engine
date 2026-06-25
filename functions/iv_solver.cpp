// iv_solver.cpp
#include "../include/black_scholes.h"
#include <cmath>
#include <stdexcept>
#include <numbers>
#include <algorithm>

namespace bs {

IVResult impliedVol(double marketPrice,
                    double S, double K, double T, double r,
                    OptionType type,
                    double tol,
                    int    max_iter)
{
    // ── input validation ──────────────────────────────────────────────────
    if (S <= 0 || K <= 0)   throw std::invalid_argument("S and K must be > 0");
    if (T <= 0)             throw std::invalid_argument("T must be > 0");
    if (marketPrice <= 0)   throw std::invalid_argument("market price must be > 0");

    // ── arbitrage bounds check ────────────────────────────────────────────
    // If market price violates basic no-arbitrage bounds, IV is undefined.
    // For a call:  price must be in (max(S - Ke^-rT, 0),  S)
    // For a put:   price must be in (max(Ke^-rT - S, 0),  Ke^-rT)
    const double discount = std::exp(-r * T);
    if (type == OptionType::Call) {
        double lowerBound = std::max(S - K * discount, 0.0);
        if (marketPrice <= lowerBound || marketPrice >= S)
            throw std::invalid_argument("Call price outside no-arbitrage bounds");
    } else {
        double lowerBound = std::max(K * discount - S, 0.0);
        double upperBound = K * discount;
        if (marketPrice <= lowerBound || marketPrice >= upperBound)
            throw std::invalid_argument("Put price outside no-arbitrage bounds");
    }

    // ── initial guess ─────────────────────────────────────────────────────
    // Brenner-Subrahmanyam approximation: good closed-form starting point.
    // Much better than just guessing 0.2 — typically gets you within 5% of
    // the true IV, so NR converges in 3-5 iterations instead of 10-20.
    //
    // σ ≈ (marketPrice / S) * sqrt(2π / T)
    double sigma = (marketPrice / S) * std::sqrt(2.0 * std::numbers::pi / T);

    // Clamp to a sane range — very deep OTM options can give wild estimates
    sigma = std::clamp(sigma, 1e-4, 10.0);

    // ── Newton-Raphson loop ───────────────────────────────────────────────
    for (int i = 0; i < max_iter; ++i) {
        BSResult res = blackScholes(S, K, T, r, sigma);

        double price = (type == OptionType::Call) ? res.call : res.put;
        double vega  = res.vega * 100.0; // undo the /100 convention — we need
                                         // raw vega (dPrice/dSigma) here

        double error = price - marketPrice;

        // Converged
        if (std::abs(error) < tol)
            return { sigma, i + 1, true };

        // Vega too small — near-zero means the update step explodes.
        // This happens for deep OTM options close to expiry.
        if (std::abs(vega) < 1e-10)
            return { sigma, i + 1, false };

        // Newton-Raphson update
        sigma -= error / vega;

        // Keep sigma in valid range — NR can occasionally overshoot
        sigma = std::clamp(sigma, 1e-4, 10.0);
    }

    // Hit max iterations without converging
    return { sigma, max_iter, false };
}

} // namespace bs