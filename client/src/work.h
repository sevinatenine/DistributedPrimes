#pragma once

#include <atomic>
#include <optional>
#include <string>
#include <vector>
#include <gmpxx.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct Pattern {
    std::string type;
    std::vector<std::string> dims;
    std::vector<unsigned int> fills;
};

bool isPrime(const mpz_class& n, int reps = 25);

mpz_class generatePrimeShape(const std::string& type,
                             const std::vector<std::string>& dims,
                             const std::vector<unsigned int>& fills);

bool testPattern(const Pattern& p);

struct FoundPrime {
    unsigned long long index;
    std::string shape;    // e.g. "5x4"
    std::string number;
};

std::optional<std::vector<FoundPrime>> run_block(const json& block,
                                                 const std::atomic<bool>& running);