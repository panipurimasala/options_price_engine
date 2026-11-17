// verify_port.cpp -- prints C++ engine results as JSON lines so
// web/verify_port.mjs can check web/engine.js against them.
//
//   c++ -std=c++20 -O2 -Iinclude web/verify_port.cpp build/libbs_core.a -o build/verify_port
//   ./build/verify_port validation_data.csv > build/port_vectors.jsonl
//   node web/verify_port.mjs build/port_vectors.jsonl validation_data.csv
#include "black_scholes.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace bs { double corrado_miller_approximation(double, double, double, double, double); }

// Same loop as impliedVol, with the starting guess passed in. Checked against
// bs::impliedVol below: with the engine's own seed it must agree exactly.
static bs::IVResult nr_from(double sigma, double mkt, double S, double K, double T, double r, bs::OptionType type) {
    double lo = 0.0, hi = 10.0;
    for (int i = 0; i < 100; ++i) {
        bs::BSResult res = bs::blackScholes(S, K, T, r, sigma);
        double price = (type == bs::OptionType::Call) ? res.call : res.put;
        double vega = res.vega * 100.0, error = price - mkt;
        if (std::abs(error) < 1e-6) return {sigma, i + 1, true};
        if (error < 0.0) lo = sigma; else hi = sigma;
        double next = (std::abs(vega) > 1e-10) ? sigma - error / vega : lo - 1.0;
        if (!(next > lo && next < hi)) next = 0.5 * (lo + hi);
        sigma = next;
    }
    return {sigma, 100, false};
}

static std::vector<std::string> split(const std::string& l) {
    std::vector<std::string> o; std::stringstream ss(l); std::string c;
    while (std::getline(ss, c, ',')) { if (!c.empty() && c.back() == '\r') c.pop_back(); o.push_back(c); }
    return o;
}

int main(int argc, char** argv) {
    // 1. Closed form + Greeks on a grid
    for (double S : {50.0, 100.0, 250.0, 754.0})
    for (double m : {0.7, 0.9, 1.0, 1.1, 1.4})
    for (double T : {0.01, 0.1, 0.5, 2.0})
    for (double r : {-0.01, 0.0, 0.05})
    for (double v : {0.05, 0.2, 0.6, 1.5}) {
        double K = S * m;
        auto b = bs::blackScholes(S, K, T, r, v);
        std::printf("{\"k\":\"bs\",\"in\":[%.17g,%.17g,%.17g,%.17g,%.17g],\"out\":[%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g]}\n",
                    S, K, T, r, v, b.call, b.put, b.delta_call, b.delta_put, b.gamma, b.vega,
                    b.theta_call, b.theta_put, b.rho_call, b.rho_put);
    }
    // 2. CRR tree, European and American, calls and puts
    for (double m : {0.8, 1.0, 1.25})
    for (double T : {0.05, 1.0})
    for (double v : {0.15, 0.5})
    for (int steps : {25, 100, 501})
    for (int t = 0; t < 2; ++t)
    for (int am = 0; am < 2; ++am) {
        auto ty = t ? bs::OptionType::Put : bs::OptionType::Call;
        auto ex = am ? bs::ExerciseType::American : bs::ExerciseType::European;
        auto res = bs::binomialTree(100.0, 100.0 * m, T, 0.05, v, ty, ex, steps);
        std::printf("{\"k\":\"tree\",\"in\":[100,%.17g,%.17g,0.05,%.17g,\"%c\",%d,%d],\"out\":%.17g}\n",
                    100.0 * m, T, v, t ? 'P' : 'C', am, steps, res.price);
    }
    // 3. IV on every validation contract, three starting guesses
    if (argc > 1) {
        std::ifstream fh(argv[1]); std::string line; std::getline(fh, line);
        std::map<std::string, size_t> col; auto h = split(line);
        for (size_t i = 0; i < h.size(); ++i) col[h[i]] = i;
        int mismatch = 0;
        while (std::getline(fh, line)) {
            if (line.empty()) continue;
            auto f = split(line);
            double S = std::stod(f[col["S"]]), K = std::stod(f[col["K"]]), T = std::stod(f[col["T"]]);
            double r = std::stod(f[col["r"]]), mid = std::stod(f[col["mid"]]);
            char t = f[col["type"]][0];
            auto ty = t == 'C' ? bs::OptionType::Call : bs::OptionType::Put;
            bs::IVResult e;
            try { e = bs::impliedVol(mid, S, K, T, r, ty); }
            catch (...) { std::printf("{\"k\":\"iv\",\"in\":[%.17g,%.17g,%.17g,%.17g,%.17g,\"%c\"],\"threw\":true}\n", mid, S, K, T, r, t); continue; }
            double seed = (mid / S) * std::sqrt(2.0 * 3.14159265358979323846 / T);
            if (seed < 0.005 || seed > 5.0) seed = bs::corrado_miller_approximation(mid, S, K, T, r);
            auto rep = nr_from(seed, mid, S, K, T, r, ty);
            if (rep.iterations != e.iterations || rep.iv != e.iv) ++mismatch;
            auto nv = nr_from(0.3, mid, S, K, T, r, ty);
            auto cm = nr_from(bs::corrado_miller_approximation(mid, S, K, T, r), mid, S, K, T, r, ty);
            std::printf("{\"k\":\"iv\",\"in\":[%.17g,%.17g,%.17g,%.17g,%.17g,\"%c\"],\"engine\":[%.17g,%d,%d],\"naive\":[%.17g,%d,%d],\"cm\":[%.17g,%d,%d]}\n",
                        mid, S, K, T, r, t, e.iv, e.iterations, e.converged ? 1 : 0,
                        nv.iv, nv.iterations, nv.converged ? 1 : 0, cm.iv, cm.iterations, cm.converged ? 1 : 0);
        }
        std::fprintf(stderr, "seed-loop replica mismatches vs bs::impliedVol: %d\n", mismatch);
    }
}
