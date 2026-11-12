#include <iostream>
#include <fstream>
#include <filesystem>
#include <mutex>
#include <chrono>

#include "clients.h"

json clients = json::object();
static std::mutex clients_mutex;

void load_clients() {
    std::lock_guard lock(clients_mutex);

    std::ifstream file("data/clients.json");
    if (!file.is_open()) {
        std::cerr << "clients.json not found, starting empty\n";
        clients = json::object();
        return;
    }

    if (file.peek() == std::ifstream::traits_type::eof()) {
        clients = json::object();
        return;
    }

    json parsed = json::parse(file, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        std::cerr << "clients.json is malformed, starting empty\n";
        clients = json::object();
        return;
    }

    clients = std::move(parsed);
}

void add_client(const std::string& id, const std::string& ip, int port,
                std::time_t last_seen, const std::string& algorithm,
                const json& extra) {
    std::lock_guard lock(clients_mutex);

    json entry = extra.is_object() ? extra : json::object();
    entry["ip"] = ip;
    entry["port"] = port;
    entry["last_seen"] = static_cast<long long>(last_seen);
    entry["algorithm"] = algorithm;

    clients[id] = std::move(entry);
}

bool update_algorithm(const std::string& id, const std::string& algo) {
    std::lock_guard lock(clients_mutex);

    auto it = clients.find(id);
    if (it == clients.end()) return false;

    (*it)["algorithm"] = algo;
    return true;
}

bool update_address(const std::string& id, const std::string& ip, int port) {
    std::lock_guard lock(clients_mutex);

    auto it = clients.find(id);
    if (it == clients.end()) return false;

    (*it)["ip"] = ip;
    (*it)["port"] = port;
    return true;
}

bool update_time(const std::string& id, std::time_t last_seen) {
    std::lock_guard lock(clients_mutex);

    auto it = clients.find(id);
    if (it == clients.end()) return false;

    (*it)["last_seen"] = static_cast<long long>(last_seen);
    return true;
}

void write_clients() {
    std::lock_guard lock(clients_mutex);

    std::filesystem::create_directories("data");
    {
        std::ofstream file("data/clients.json.tmp");
        file << clients.dump(4);
    }

    std::filesystem::rename("data/clients.json.tmp", "data/clients.json");
}

std::string clients_to_string(int indent) {
    std::lock_guard lock(clients_mutex);
    return clients.dump(indent);
}

bool remove_client(const std::string& id) {
    std::lock_guard lock(clients_mutex);
    return clients.erase(id) > 0;
}

std::string get_client_algorithm(const std::string& id) {
    std::lock_guard lock(clients_mutex);

    auto it = clients.find(id);
    if (it == clients.end()) return "";
    return it->value("algorithm", "");
}

static double now_seconds() {
    return std::chrono::duration<double>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

static json* stats_for(const std::string& id) {
    auto it = clients.find(id);
    if (it == clients.end()) return nullptr;
    if (!it->contains("stats") || !(*it)["stats"].is_object())
        (*it)["stats"] = json::object();
    return &(*it)["stats"];
}

bool set_current_block(const std::string& id, unsigned long long block) {
    std::lock_guard lock(clients_mutex);
    json* s = stats_for(id);
    if (!s) return false;

    (*s)["current_block"] = block;
    (*s)["block_started"] = now_seconds();
    return true;
}

bool record_block_done(const std::string& id) {
    std::lock_guard lock(clients_mutex);
    json* s = stats_for(id);
    if (!s) return false;

    double started = s->value("block_started", 0.0);
    double elapsed = started > 0 ? now_seconds() - started : 0.0;

    (*s)["blocks_done"] = s->value("blocks_done", 0ULL) + 1;
    (*s)["total_block_seconds"] = s->value("total_block_seconds", 0.0) + elapsed;
    s->erase("current_block");
    s->erase("block_started");
    return true;
}

bool record_prime(const std::string& id) {
    std::lock_guard lock(clients_mutex);
    json* s = stats_for(id);
    if (!s) return false;

    (*s)["primes_found"] = s->value("primes_found", 0ULL) + 1;
    return true;
}