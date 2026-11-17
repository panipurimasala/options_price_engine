// iv_solver.cpp
#include "../include/black_scholes.h"
#include <cmath>
#include <stdexcept>
#include <numbers>
#include <algorithm>

namespace bs {

    // Corrado-Miller approximation for the initial IV guess. Used as a fallback
    // when the primary Brenner-Subrahmanyam guess (in impliedVol) is out of range.
    double corrado_miller_approximation(double marketPrice, double S, double K, double T, double r) {
    
        //1. Calculate the discount factor (d)
        double d = std::exp(-r * T);
        
        // 2. Pre-calculate common terms to optimize performance
        double Kd = K * d;
        double denominator = S + Kd;
        
        // Avoid division by zero if both S and K are somehow zero
        if (denominator <= 0.0 || T <= 0.0) {
            return 0.20; // Return a generic 20% flat fallback
        }
        
        double intrinsic_component = (S - Kd) / 2.0;
        double price_minus_intrinsic = marketPrice - intrinsic_component;
        
        // 3. Compute the inner square root term: (C - (S - Kd)/2)^2 - (S - Kd)^2 / pi
        double term1 = price_minus_intrinsic * price_minus_intrinsic;
        double term2 = ((S - Kd) * (S - Kd)) / std::numbers::pi;
        double inner_radial = term1 - term2;
        
        // Safety Guard: Handle edge cases where market noise causes a negative value under the radical
        if (inner_radial < 0.0) {
            inner_radial = 0.0; 
        }
        
        // 4. Combine terms according to the Corrado-Miller formula
        double main_bracket = price_minus_intrinsic + std::sqrt(inner_radial);
        double constant_factor = std::sqrt(2.0 * std::numbers::pi / T);
        
        double sigma = (constant_factor / denominator) * main_bracket;
        
        // 5. Hard limit clamping to ensure Newton-Raphson receives a valid economic number
        // Prevents zero/negative values or extreme numbers from ruining downstream calculations.
        return std::max(0.005, std::min(sigma, 5.0)); 
}


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
        constexpr double eps = 1e-6;
        if (marketPrice <= lowerBound - eps || marketPrice >= S + eps)
            throw std::invalid_argument("Call price outside no-arbitrage bounds");
    } else {
        double lowerBound = std::max(K * discount - S, 0.0);
        double upperBound = K * discount;
        constexpr double eps = 1e-6;
        if (marketPrice <= lowerBound - eps || marketPrice >= upperBound + eps)
                    throw std::invalid_argument("Put price outside no-arbitrage bounds");
            }       

    // ── initial guess ─────────────────────────────────────────────────────
    // Brenner-Subrahmanyam approximation: is best cloed formula for ATM options, but also gives a reasonable starting point for NR.
    // Esasy to calculate the true IV, so Newton-Raphson converges in 3-5 iterations instead of 10-20.
    // Formula: σ ≈ (marketPrice / S) * sqrt(2π / T)
    double sigma = (marketPrice / S) * std::sqrt(2.0 * std::numbers::pi / T);

    // Clamp to a same range — very deep OTM options can give wild estimates due to the approximation formula, which can cause NR to diverge.
    if (sigma < 0.005 || sigma > 5.0) {
        sigma = corrado_miller_approximation(marketPrice, S, K, T, r);
    }

    // ── Safeguarded (bracketed) Newton-Raphson loop ───────────────────────
    // BS price is strictly increasing in sigma, so every evaluation tells us
    // which side of the true IV we are on. We keep a bracket [lo, hi] that
    // always contains the root and shrink it each iteration.
    //
    // Why: an ATM-style seed is far too low for OTM options (e.g. 0.03 when the
    // true IV is 0.15). There price and vega are ~0, so the raw Newton step
    // shoots to the upper bound, the next one crashes to the lower bound where
    // vega underflows, and plain NR gives up. With the bracket, any Newton step
    // that would leave [lo, hi] (or a near-zero vega) is replaced by a
    // bisection step, which always makes progress. When Newton behaves, the
    // path is identical to plain NR, so quadratic convergence is kept.
    double lo = 0.0;    // price(lo) <= market price  (sigma -> 0 limit)
    double hi = 10.0;   // upper search bound for sigma

    for (int i = 0; i < max_iter; ++i) {
        BSResult res = blackScholes(S, K, T, r, sigma);

        double price = (type == OptionType::Call) ? res.call : res.put;
        double vega  = res.vega * 100.0; // undo the /100 convention — we need
                                         // raw vega (dPrice/dSigma) here

        double error = price - marketPrice;

        // Converged
        if (std::abs(error) < tol)
            return { sigma, i + 1, true };

        // Tighten the bracket: price too low -> root is above sigma, and vice versa.
        if (error < 0.0) lo = sigma;
        else             hi = sigma;

        // Newton candidate; fall back to bisection if vega is ~0 or the step
        // would leave the bracket.
        double next = (std::abs(vega) > 1e-10) ? sigma - error / vega : lo - 1.0;
        if (!(next > lo && next < hi))
            next = 0.5 * (lo + hi);

        sigma = next;
    }

    // Hit max iterations without converging
    return { sigma, max_iter, false };
}

} // namespace bs