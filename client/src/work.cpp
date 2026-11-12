#include <gmpxx.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

#include "work.h"

using namespace std;

bool isPrime(const mpz_class& n, int reps) {
    return mpz_probab_prime_p(n.get_mpz_t(), reps) != 0;
}

mpz_class generatePrimeShape(const string& type,
                             const vector<string>& dims,
                             const vector<unsigned int>& fills) {
    // fills[0] is border (single digit), fills[1] is inner fill (single digit)
    if (fills.size() < 2 || fills[0] > 9 || fills[1] > 9)
        throw invalid_argument("fills must be two single digits");

    char border = static_cast<char>('0' + fills[0]);
    char inner  = static_cast<char>('0' + fills[1]);

    if (type == "rectangle") {
        if (dims.size() < 2)
            throw invalid_argument("rectangle needs width and height");

        long w = stol(dims[0]);
        long h = stol(dims[1]);
        if (w < 1 || h < 1)
            throw invalid_argument("dimensions must be positive");
        if (border == '0')
            throw invalid_argument("border digit can't be 0 (it would be the leading digit)");

        string edgeRow(w, border);
        string midRow(w, inner);
        midRow.front() = border;
        midRow.back()  = border;

        string digits;
        digits.reserve(static_cast<size_t>(w) * h);
        for (long r = 0; r < h; ++r)
            digits += (r == 0 || r == h - 1) ? edgeRow : midRow;

        return mpz_class(digits, 10);
    }

    throw invalid_argument("unknown shape type: " + type);
}

bool testPattern(const Pattern& p) {
    mpz_class number = generatePrimeShape(p.type, p.dims, p.fills);
    return isPrime(number);
}

optional<vector<FoundPrime>> run_block(const json& block, const atomic<bool>& running) {
    vector<FoundPrime> found;
    if (!block.contains("patterns")) return found;

    for (const auto& item : block["patterns"]) {
        if (!running) return nullopt;

        try {
            Pattern p{
                item.at("type").get<string>(),
                item.at("dims").get<vector<string>>(),
                item.at("fills").get<vector<unsigned int>>()
            };

            mpz_class number = generatePrimeShape(p.type, p.dims, p.fills);
            if (isPrime(number)) {
                string shape;
                for (size_t i = 0; i < p.dims.size(); ++i)
                    shape += (i ? "x" : "") + p.dims[i];

                found.push_back({item.value("index", 0ULL), shape, number.get_str()});
            }
        } catch (const exception& e) {
            cerr << "Skipping bad pattern: " << e.what() << "\n";
        }
    }
    return found;
}