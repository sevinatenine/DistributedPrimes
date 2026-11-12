#pragma once

#include <ctime>
#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

extern json clients;

void load_clients();
void write_clients();

void add_client(const std::string& id, const std::string& ip, int port,
                std::time_t last_seen, const std::string& algorithm,
                const json& extra = json::object());

bool update_address(const std::string& id, const std::string& ip, int port);
bool update_time(const std::string& id, std::time_t last_seen);
bool update_algorithm(const std::string& id, const std::string& algo);
std::string clients_to_string(int indent = -1);
bool remove_client(const std::string& id);
std::string get_client_algorithm(const std::string& id);
bool set_current_block(const std::string& id, unsigned long long block);
bool record_block_done(const std::string& id);
bool record_prime(const std::string& id);