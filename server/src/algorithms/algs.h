#pragma once

#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

const std::string ALGORITHMS[] = {
    "rect",
    "square"
};

struct AlgItem {
    std::string type;
    std::vector<std::string> dims;
    std::vector<unsigned int> fills;
};

void init_algs();
bool is_valid_algorithm(const std::string& alg);

bool get_alg_data(const std::string& alg, unsigned long long id, AlgItem& out);

std::optional<json> request_work(const std::string& alg, const std::string& client_id);

bool finish_work(const std::string& alg, unsigned long long block, const std::string& client_id);
bool report_found(const std::string& alg, unsigned long long index, const std::string& client_id);
json get_stats(size_t top_n = 10, long long stale_seconds = 3600);