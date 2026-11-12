#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <set>

#include "data_manager.h"

using namespace std;

static json progress = json::object();
static mutex progress_mutex;

static json& alg_entry(const std::string& alg) {
    json& e = progress[alg];
    if (!e.is_object()) e = json::object();
    if (!e.contains("completed_below")) e["completed_below"] = 0ULL;
    if (!e.contains("next_block"))      e["next_block"] = 0ULL;
    if (!e.contains("open") || !e["open"].is_object())           e["open"] = json::object();
    if (!e.contains("completed") || !e["completed"].is_array())  e["completed"] = json::array();
    if (!e.contains("found") || !e["found"].is_array()) e["found"] = json::array();
    return e;
}

void load_progress() {
    lock_guard lock(progress_mutex);

    ifstream file("data/progress.json");
    if (!file.is_open() || file.peek() == ifstream::traits_type::eof()) {
        cerr << "progress.json not found or empty, starting fresh\n";
        progress = json::object();
        return;
    }

    json parsed = json::parse(file, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        cerr << "progress.json is malformed, starting fresh\n";
        progress = json::object();
        return;
    }
    progress = std::move(parsed);
}

void write_progress() {
    lock_guard lock(progress_mutex);

    filesystem::create_directories("data");
    {
        ofstream file("data/progress.json.tmp");
        file << progress.dump(4);
    }
    filesystem::rename("data/progress.json.tmp", "data/progress.json");
}

unsigned long long get_lowest_for_alg(const std::string& alg) {
    lock_guard lock(progress_mutex);

    auto it = progress.find(alg);
    if (it == progress.end() || !it->is_object()) return 0;
    return it->value("completed_below", 0ULL);
}

unsigned long long assign_block(const std::string& alg, const std::string& client_id,
                                time_t now, long long stale_seconds) {
    lock_guard lock(progress_mutex);
    json& e = alg_entry(alg);
    json& open = e["open"];
    
    for (auto& [key, val] : open.items()) {
        if (val.value("client", "") == client_id) {
            val["assigned_at"] = static_cast<long long>(now);
            return stoull(key);
        }
    }

    long long cutoff = static_cast<long long>(now) - stale_seconds;
    optional<unsigned long long> reuse;
    for (auto& [key, val] : open.items()) {
        if (val.value("assigned_at", 0LL) <= cutoff) {
            unsigned long long b = stoull(key);
            if (!reuse || b < *reuse) reuse = b;
        }
    }

    unsigned long long block;
    if (reuse) {
        block = *reuse;
    } else {
        block = e["next_block"].get<unsigned long long>();
        e["next_block"] = block + 1;
    }

    open[to_string(block)] = {
        {"client", client_id},
        {"assigned_at", static_cast<long long>(now)}
    };
    return block;
}

bool complete_block(const std::string& alg, unsigned long long block, const std::string& client_id) {
    lock_guard lock(progress_mutex);
    json& e = alg_entry(alg);
    json& open = e["open"];

    auto it = open.find(to_string(block));
    if (it == open.end()) return false;
    if (it->value("client", "") != client_id) return false;
    open.erase(it);

    auto done = e["completed"].get<set<unsigned long long>>();
    done.insert(block);

    unsigned long long low = e["completed_below"].get<unsigned long long>();
    while (!done.empty() && *done.begin() == low) {
        done.erase(done.begin());
        ++low;
    }

    e["completed_below"] = low;
    e["completed"] = done;
    return true;
}

bool release_block(const std::string& alg, unsigned long long block) {
    lock_guard lock(progress_mutex);
    json& open = alg_entry(alg)["open"];

    auto it = open.find(to_string(block));
    if (it == open.end()) return false;

    (*it)["assigned_at"] = 0LL;
    (*it)["client"] = "";
    return true;
}

size_t release_client_blocks(const std::string& client_id) {
    lock_guard lock(progress_mutex);
    size_t count = 0;

    for (auto& [alg, e] : progress.items()) {
        if (!e.is_object() || !e.contains("open")) continue;
        for (auto& [key, val] : e["open"].items()) {
            if (val.value("client", "") == client_id) {
                val["assigned_at"] = 0LL;
                val["client"] = "";
                ++count;
            }
        }
    }
    return count;
}

std::string progress_to_string(int indent) {
    lock_guard lock(progress_mutex);
    return progress.dump(indent);
}

bool record_found(const string& alg, unsigned long long index,
                  const string& client_id, const json& info) {
    lock_guard lock(progress_mutex);
    json& found = alg_entry(alg)["found"];

    for (const auto& f : found)
        if (f.contains("index") && f["index"] == index) return false;   // already recorded

    json entry = info;
    entry["index"]    = index;
    entry["client"]   = client_id;
    entry["found_at"] = static_cast<long long>(time(nullptr));
    found.push_back(std::move(entry));
    return true;
}

string found_to_string(int indent) {
    lock_guard lock(progress_mutex);
    json out = json::object();
    for (auto& [alg, e] : progress.items())
        if (e.is_object() && e.contains("found")) out[alg] = e["found"];
    return out.dump(indent);
}

size_t count_found_by(const string& client_id) {
    lock_guard lock(progress_mutex);
    size_t n = 0;
    for (auto& [alg, e] : progress.items()) {
        if (!e.is_object() || !e.contains("found")) continue;
        for (const auto& f : e["found"])
            if (f.value("client", "") == client_id) ++n;
    }
    return n;
}

json progress_snapshot() {
    lock_guard lock(progress_mutex);
    return progress;
}