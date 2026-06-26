// iv_solver.cpp
#include "../include/black_scholes.h"
#include <cmath>
#include <stdexcept>
#include <numbers>
#include <algorithm>

namespace bs {

    // This function implements corrado_miller approximation for initial guess of IV instead of using Brenner-Subrahmanyam approximation. 
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