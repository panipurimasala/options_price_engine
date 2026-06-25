// black_scholes.cpp
#include "../include/black_scholes.h"
#include <cmath>
#include <stdexcept>
#include <numbers>

namespace bs {

    namespace {
        constexpr double PI = std::numbers::pi;

        double normalPDF(double x) {
            return std::exp(-0.5 * x * x) / std::sqrt(2.0 * PI);
        }
        
        double normalCDF(double x) {
            return 0.5 * std::erfc(-x / std::sqrt(2.0));
        }
    }



BSResult blackScholes(double S, double K, double T, double r, double sigma) {
    if (S <= 0) throw std::invalid_argument("S must be > 0");
    if (K <= 0) throw std::invalid_argument("K must be > 0");
    if (T <= 0) throw std::invalid_argument("T must be > 0");
    if (sigma <= 0) throw std::invalid_argument("sigma must be > 0");
    
    const double sqrtT = std::sqrt(T);
    const double discount = std::exp(-r * T);
    const double d1 = (std::log(S / K) + (r + 0.5 * sigma * sigma) * T)/ (sigma * sqrtT);
    const double d2 = d1 - sigma * sqrtT;

    const double Nd1  = normalCDF(d1);
    const double Nd2  = normalCDF(d2);
    const double Nd1n = 1.0-Nd1;
    const double Nd2n = 1.0-Nd2;
    const double phi  = normalPDF(d1);

    BSResult res;
    res.call       = S * Nd1 - K * discount * Nd2;
    res.put        = K * discount * Nd2n - S * Nd1n;
    res.delta_call = Nd1;
    res.delta_put  = Nd1 - 1.0;
    res.gamma      = phi / (S * sigma * sqrtT);
    res.vega       = S * phi * sqrtT / 100.0;       // per 1% vol move
    res.theta_call = (-(S * phi * sigma) / (2.0 * sqrtT)
                     - r * K * discount * Nd2)  / 365.0;
    res.theta_put  = (-(S * phi * sigma) / (2.0 * sqrtT)
                     + r * K * discount * Nd2n) / 365.0;
    res.rho_call   =  K * T * discount * Nd2  / 100.0; // per 1% rate move
    res.rho_put    = -K * T * discount * Nd2n / 100.0;
    return res;
}

} // namespace bs