#include "../include/black_scholes.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace bs {

TreeResult binomialTree(double S, double K, double T, double r, double sigma,
                        OptionType   type,
                        ExerciseType exercise,
                        int          steps)
{
    if (S <= 0 || K <= 0)  throw std::invalid_argument("S and K must be > 0");
    if (T <= 0)            throw std::invalid_argument("T must be > 0");
    if (sigma <= 0)        throw std::invalid_argument("sigma must be > 0");
    if (steps <= 0)        throw std::invalid_argument("steps must be > 0");

    const size_t N        = static_cast<size_t>(steps);  // cast once, use everywhere
    const double dt       = T / steps;
    const double u        = std::exp(sigma * std::sqrt(dt));
    const double d        = 1.0 / u;
    const double discount = std::exp(-r * dt);
    const double p        = (std::exp(r * dt) - d) / (u - d);
    const double q        = 1.0 - p;

    if (p <= 0.0 || p >= 1.0)
        throw std::runtime_error("Risk-neutral probability outside (0,1) — "
                                 "reduce steps or check inputs");

    std::vector<double> V(N + 1);

    for (size_t j = 0; j <= N; ++j) {
        double spot = S * std::pow(u, static_cast<double>(j))
                        * std::pow(d, static_cast<double>(N - j));
        if (type == OptionType::Call)
            V[j] = std::max(spot - K, 0.0);
        else
            V[j] = std::max(K - spot, 0.0);
    }

    for (size_t i = N; i-- > 0;) {          // safe reverse loop for size_t
        for (size_t j = 0; j <= i; ++j) {
            double continuation = discount * (p * V[j + 1] + q * V[j]);

            if (exercise == ExerciseType::American) {
                double spot = S * std::pow(u, static_cast<double>(j))
                                * std::pow(d, static_cast<double>(i - j));
                double exerciseValue = (type == OptionType::Call)
                                     ? std::max(spot - K, 0.0)
                                     : std::max(K - spot, 0.0);
                V[j] = std::max(continuation, exerciseValue);
            } else {
                V[j] = continuation;
            }
        }
    }

    return { V[0], steps };
}

} // namespace bs