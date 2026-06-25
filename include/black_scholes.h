#pragma once
#include <cmath>
#include <stdexcept>

namespace bs {
    struct BSResult {
        double call;
        double put;
        double delta_call;
        double delta_put;
        double gamma;
        double vega;
        double theta_call;
        double theta_put;
        double rho_call;
        double rho_put;
    };

    enum class OptionType { Call, Put };

    struct IVResult {
        double iv;           // implied volatility (annualized)
        int    iterations;   // how many NR steps it took
        bool   converged;    // false if hit max_iter without converging
    };

    IVResult impliedVol(double marketPrice,
                        double S, double K, double T, double r,
                        OptionType type,
                        double tol      = 1e-6,
                        int    max_iter = 100);
    
    BSResult blackScholes(double S, double K, double T, double r, double sigma);

    // inside black_scholes.h, inside namespace bs

    enum class ExerciseType { European, American };

    struct TreeResult {
        double price;
        int    steps;
    };

    TreeResult binomialTree(double S, double K, double T, double r, double sigma,
                            OptionType    type,
                            ExerciseType  exercise = ExerciseType::European,
                            int           steps    = 500);
}