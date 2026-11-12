#pragma once

#include <ctime>
#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

constexpr unsigned long long BLOCK_SIZE = 400;

inline unsigned long long block_start(unsigned long long block) { return block * BLOCK_SIZE; }
inline unsigned long long block_end(unsigned long long block)   { return (block + 1) * BLOCK_SIZE; }

void load_progress();
void write_progress();

unsigned long long get_lowest_for_alg(const std::string& alg);

unsigned long long assign_block(const std::string& alg, const std::string& client_id,
                                std::time_t now = std::time(nullptr),
                                long long stale_seconds = 3600);

bool complete_block(const std::string& alg, unsigned long long block, const std::string& client_id);

bool release_block(const std::string& alg, unsigned long long block);

size_t release_client_blocks(const std::string& client_id);

bool record_found(const std::string& alg, unsigned long long index,
                  const std::string& client_id, const json& info);

std::string progress_to_string(int indent = -1);
std::string found_to_string(int indent = -1);
size_t count_found_by(const std::string& client_id);
json progress_snapshot();