// Validation of the pricing engine against real market option chains.
//
// Produces the three numbers a resume bullet needs:
//   1. coverage        -- contracts / tickers / expiries the engine handled
//   2. round-trip error -- solve IV from market mid, re-price, recover the mid
//   3. parity residual  -- C - P vs S - K*exp(-rT) on real quotes
//
// Build:  add to CMakeLists.txt (see README_VALIDATION.md)
// Run:    ./market_validation validation_data.csv engine_output.csv

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// ===================== ADAPTER =====================
// Point these at the real headers and signatures in this repo.
// Everything below this block is engine-agnostic.
#include "black_scholes.h"   // bs::blackScholes and bs::impliedVol both live here
#include <stdexcept>

namespace engine {

// bs::blackScholes returns call and put (plus Greeks) in one struct.
inline double price(double S, double K, double T, double r, double sigma, char type) {
    const bs::BSResult res = bs::blackScholes(S, K, T, r, sigma);
    return (type == 'C') ? res.call : res.put;
}

// bs::impliedVol returns {iv, iterations, converged}. A non-converged result
// still carries a finite sigma, so throw to make the harness count it as a
// solver failure instead of silently accepting it.
inline double implied_vol(double mkt_price, double S, double K, double T, double r, char type) {
    const bs::OptionType ot = (type == 'C') ? bs::OptionType::Call : bs::OptionType::Put;
    const bs::IVResult res = bs::impliedVol(mkt_price, S, K, T, r, ot);
    if (!res.converged) throw std::runtime_error("IV solver did not converge");
    return res.iv;
}

}  // namespace engine
// =================== END ADAPTER ===================

struct Quote {
    std::string ticker, expiry;
    char type;
    double S, K, T, r, bid, ask, mid, market_iv;
};

static std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> out;
    std::stringstream ss(line);
    std::string cell;
    while (std::getline(ss, cell, ',')) out.push_back(cell);
    return out;
}

static std::vector<Quote> load(const std::string& path) {
    std::ifstream fh(path);
    if (!fh) { std::cerr << "cannot open " << path << "\n"; std::exit(1); }

    std::string line;
    if (!std::getline(fh, line)) { std::cerr << "empty file\n"; std::exit(1); }

    std::map<std::string, int> col;
    auto headers = split_csv(line);
    for (size_t i = 0; i < headers.size(); ++i) col[headers[i]] = static_cast<int>(i);

    for (const char* need : {"ticker", "expiry", "type", "S", "K", "T", "r", "mid"}) {
        if (!col.count(need)) { std::cerr << "missing column: " << need << "\n"; std::exit(1); }
    }

    std::vector<Quote> quotes;
    while (std::getline(fh, line)) {
        if (line.empty()) continue;
        auto f = split_csv(line);
        Quote q;
        q.ticker = f[col["ticker"]];
        q.expiry = f[col["expiry"]];
        q.type   = f[col["type"]][0];
        q.S = std::stod(f[col["S"]]);
        q.K = std::stod(f[col["K"]]);
        q.T = std::stod(f[col["T"]]);
        q.r = std::stod(f[col["r"]]);
        q.mid = std::stod(f[col["mid"]]);
        q.bid = col.count("bid") ? std::stod(f[col["bid"]]) : 0.0;
        q.ask = col.count("ask") ? std::stod(f[col["ask"]]) : 0.0;
        q.market_iv = col.count("market_iv") ? std::stod(f[col["market_iv"]]) : 0.0;
        quotes.push_back(q);
    }
    return quotes;
}

template <typename T>
static double percentile(std::vector<T> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    size_t idx = static_cast<size_t>(p * (v.size() - 1));
    return static_cast<double>(v[idx]);
}

int main(int argc, char** argv) {
    const std::string in_path  = (argc > 1) ? argv[1] : "validation_data.csv";
    const std::string out_path = (argc > 2) ? argv[2] : "engine_output.csv";

    auto quotes = load(in_path);
    std::cout << "Loaded " << quotes.size() << " quotes from " << in_path << "\n\n";

    std::ofstream out(out_path);
    out << "ticker,expiry,type,S,K,T,r,mid,solved_iv,reprice,abs_err\n"
        << std::fixed << std::setprecision(10);

    std::vector<double> abs_errors, rel_errors;
    std::set<std::string> tickers;
    std::set<std::string> ticker_expiries;
    std::set<std::string> strikes;
    int failures = 0;

    // (ticker|expiry|strike) -> solved call/put prices, for the parity check
    std::map<std::string, std::pair<double, double>> pairs;
    std::map<std::string, Quote> pair_meta;

    for (const auto& q : quotes) {
        double iv = 0.0, reprice = 0.0;
        bool ok = true;
        try {
            iv = engine::implied_vol(q.mid, q.S, q.K, q.T, q.r, q.type);
            if (!std::isfinite(iv) || iv <= 0.0 || iv > 5.0) ok = false;
            else reprice = engine::price(q.S, q.K, q.T, q.r, iv, q.type);
        } catch (...) {
            ok = false;
        }
        if (!ok || !std::isfinite(reprice)) { ++failures; continue; }

        const double abs_err = std::fabs(reprice - q.mid);
        abs_errors.push_back(abs_err);
        rel_errors.push_back(abs_err / q.mid);

        tickers.insert(q.ticker);
        ticker_expiries.insert(q.ticker + "|" + q.expiry);
        const std::string key = q.ticker + "|" + q.expiry + "|" + std::to_string(q.K);
        strikes.insert(key);

        if (q.type == 'C') pairs[key].first = q.mid;
        else               pairs[key].second = q.mid;
        pair_meta[key] = q;

        out << q.ticker << ',' << q.expiry << ',' << q.type << ',' << q.S << ','
            << q.K << ',' << q.T << ',' << q.r << ',' << q.mid << ','
            << iv << ',' << reprice << ',' << abs_err << '\n';
    }
    out.close();

    // Put-call parity on real quotes: C - P - (S - K e^{-rT})
    std::vector<double> parity_abs;
    for (const auto& [key, cp] : pairs) {
        const auto& [call, put] = cp;
        if (call <= 0.0 || put <= 0.0) continue;   // need both legs
        const Quote& m = pair_meta.at(key);
        const double theo = m.S - m.K * std::exp(-m.r * m.T);
        parity_abs.push_back(std::fabs((call - put) - theo));
    }

    std::cout << std::fixed;
    std::cout << "=== COVERAGE ===\n"
              << "  contracts priced : " << abs_errors.size() << "\n"
              << "  tickers          : " << tickers.size() << "\n"
              << "  ticker-expiries  : " << ticker_expiries.size() << "\n"
              << "  distinct strikes : " << strikes.size() << "\n"
              << "  solver failures  : " << failures << "\n\n";

    if (!abs_errors.empty()) {
        const double worst = *std::max_element(abs_errors.begin(), abs_errors.end());
        std::cout << std::setprecision(3) << std::scientific
                  << "=== ROUND TRIP (solve IV from mid -> re-price -> compare) ===\n"
                  << "  max  abs error : $" << worst << "\n"
                  << "  p99  abs error : $" << percentile(abs_errors, 0.99) << "\n"
                  << "  median abs err : $" << percentile(abs_errors, 0.50) << "\n"
                  << "  max  rel error : "  << *std::max_element(rel_errors.begin(), rel_errors.end()) << "\n\n";
    }

    if (!parity_abs.empty()) {
        std::cout << std::setprecision(4) << std::fixed
                  << "=== PUT-CALL PARITY ON MARKET QUOTES ===\n"
                  << "  matched pairs  : " << parity_abs.size() << "\n"
                  << "  median |resid| : $" << percentile(parity_abs, 0.50) << "\n"
                  << "  p90    |resid| : $" << percentile(parity_abs, 0.90) << "\n"
                  << "  (residual reflects market frictions, not engine error)\n\n";
    }

    std::cout << "Wrote " << out_path
              << " -- now run: python vol_surface/compare_reference.py " << out_path << "\n";
    return 0;
}
