// vol_surface.cpp
// Reads spy_options_chain.csv, runs IV solver on each row,
// writes strike, expiry (days), type, iv to iv_surface.csv

#include "../include/black_scholes.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <iomanip>

// ── CSV parser ────────────────────────────────────────────────────────────────
// Minimal — just enough to read the columns we need.
// Columns expected: type,strike,lastPrice,bid,ask,volume,openInterest,t_years,spot_price,mid

struct Row {
    std::string type;
    double      strike;
    double      t_years;
    double      spot;
    double      mid;
};

static bool parseRow(const std::string& line, Row& out) {
    std::stringstream ss(line);
    std::string tok;
    std::vector<std::string> cols;

    while (std::getline(ss, tok, ','))
        cols.push_back(tok);

    // Need at least 10 columns
    if (cols.size() < 10) return false;

    try {
        out.type   = cols[0];            // type
        out.strike = std::stod(cols[1]); // strike
        out.t_years= std::stod(cols[7]); // t_years
        out.spot   = std::stod(cols[8]); // spot_price
        out.mid    = std::stod(cols[9]); // mid
    } catch (...) {
        return false;  // skip malformed rows
    }

    // Skip rows with no real price
    if (out.mid <= 0 || out.spot <= 0 || out.strike <= 0 || out.t_years <= 0)
        return false;

    return true;
}

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    constexpr double r = 0.0533;  // 3-month T-bill rate — update to current

    const std::string inPath  = (argc > 1) ? argv[1] : "spy_options_chain.csv";
    const std::string outPath = (argc > 2) ? argv[2] : "iv_surface.csv";

    std::ifstream inFile(inPath);
    if (!inFile.is_open()) {
        std::cerr << "Could not open " << inPath << "\n"
                << "Usage: ./vol_surface <input.csv> <output.csv>\n";
        return 1;
    }

    std::ofstream outFile(outPath);
    if (!outFile.is_open()) {
        std::cerr << "Could not open " << outPath << " for writing\n";
        return 1;
    }

    outFile << "type,strike,spot,t_years,days_to_expiry,mid,iv,iterations,converged\n";

    std::string line;
    std::getline(inFile, line); // skip header

    int total     = 0;
    int solved    = 0;
    int skipped   = 0;
    int failed    = 0;

    while (std::getline(inFile, line)) {
        if (line.empty()) continue;
        ++total;

        Row row;
        if (!parseRow(line, row)) {
            ++skipped;
            continue;
        }

        bs::OptionType optType = (row.type == "call")
                               ? bs::OptionType::Call
                               : bs::OptionType::Put;

        try {
            auto result = bs::impliedVol(row.mid, row.spot, row.strike,
                                         row.t_years, r, optType);

            // Filter out clearly bad IVs — below 1% or above 300% is noise
            if (!result.converged || result.iv < 0.01 || result.iv > 3.0) {
                ++failed;
                continue;
            }

            int days = static_cast<int>(row.t_years * 365.0);

            outFile << row.type                  << ","
                    << row.strike                << ","
                    << row.spot                  << ","
                    << std::fixed << std::setprecision(6)
                    << row.t_years               << ","
                    << days                      << ","
                    << row.mid                   << ","
                    << result.iv                 << ","
                    << result.iterations         << ","
                    << result.converged          << "\n";
            ++solved;

        } catch (const std::exception& e) {
            // Arbitrage bound violations, malformed inputs — skip silently
            ++skipped;
        }
    }

    // Summary to stdout
    std::cout << "\n=== Vol Surface Build Complete ===\n";
    std::cout << "Total rows   : " << total   << "\n";
    std::cout << "Solved       : " << solved  << "\n";
    std::cout << "Failed/noisy : " << failed  << "\n";
    std::cout << "Skipped      : " << skipped << "\n";
    std::cout << "Output       : iv_surface.csv\n";

    return 0;
}