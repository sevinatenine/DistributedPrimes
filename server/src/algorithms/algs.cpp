#include <algorithm>
#include <ctime>

#include "algs.h"
#include "rect.h"
#include "data_manager.h"
#include "../clients.h"

using namespace std;

static const unsigned int RECT_BORDER = 1;
static const unsigned int RECT_INNER  = 3;

void init_algs() {
    load_progress();
}

bool is_valid_algorithm(const string& alg) {
    for (const auto& a : ALGORITHMS)
        if (a == alg) return true;
    return false;
}

static const unsigned int RECT_BORDERS[] = {1, 3, 7, 9};
static const unsigned long long RECT_NBORDERS = 4;
static const unsigned long long RECT_NINNERS  = 10;                      // 0-9
static const unsigned long long RECT_NFILLS   = RECT_NBORDERS * RECT_NINNERS;

bool get_alg_data(const string& alg, unsigned long long id, AlgItem& out) {
    if (alg == "rect") {
        unsigned long long w, h;
        rect_at(id / RECT_NFILLS, &w, &h);

        unsigned long long fill = id % RECT_NFILLS;
        unsigned int border = RECT_BORDERS[fill / RECT_NINNERS];
        unsigned int inner  = fill % RECT_NINNERS;

        bool has_interior = w > 2 && h > 2;
        if (!has_interior && inner != 0) return false;

        out.type  = "rectangle";
        out.dims  = {to_string(w), to_string(h)};
        out.fills = {border, inner};
        return true;
    } else if (alg == "square") {
        unsigned long long side = id / RECT_NFILLS + 1;
        unsigned long long fill = id % RECT_NFILLS;
        unsigned int border = RECT_BORDERS[fill / RECT_NINNERS];
        unsigned int inner  = fill % RECT_NINNERS;

        if (side <= 2 && inner != 0) return false;

        out.type  = "rectangle";
        out.dims  = {to_string(side), to_string(side)};
        out.fills = {border, inner};
        return true;
    }
    return false;
}

optional<json> request_work(const string& alg, const string& client_id) {
    if (!is_valid_algorithm(alg)) return nullopt;

    unsigned long long block = assign_block(alg, client_id);

    json patterns = json::array();
    for (unsigned long long id = block_start(block); id < block_end(block); ++id) {
        AlgItem item;
        if (!get_alg_data(alg, id, item)) continue;

        json p;
        p["index"] = id;
        p["type"]  = item.type;
        p["dims"]  = item.dims;
        p["fills"] = item.fills;
        patterns.push_back(std::move(p));
    }

    json work;
    work["alg"]      = alg;
    work["block"]    = block;
    work["start"]    = block_start(block);
    work["end"]      = block_end(block);   // exclusive
    work["patterns"] = std::move(patterns);
    return work;
}

bool finish_work(const string& alg, unsigned long long block, const string& client_id) {
    return complete_block(alg, block, client_id);
}

bool report_found(const string& alg, unsigned long long index, const string& client_id) {
    AlgItem item;
    if (!get_alg_data(alg, index, item)) return false;

    json info;
    info["type"]  = item.type;
    info["dims"]  = item.dims;
    info["fills"] = item.fills;

    record_found(alg, index, client_id, info);   // repeat reports are ignored
    return true;
}

json get_stats(size_t top_n, long long stale_seconds) {
    json snap = progress_snapshot();
    json cl = json::parse(clients_to_string());
    long long now = static_cast<long long>(time(nullptr));
    long long cutoff = now - stale_seconds;

    auto bigger_first = [](const json& a, const json& b) {
        auto da = a["digits"].get<unsigned long long>();
        auto db = b["digits"].get<unsigned long long>();
        if (da != db) return da > db;
        return a.value("found_at", 0LL) < b.value("found_at", 0LL);
    };

    json per_alg = json::object();
    unsigned long long total_found = 0;

    for (const auto& alg : ALGORITHMS) {
        json e = snap.contains(alg) && snap[alg].is_object() ? snap[alg] : json::object();

        unsigned long long low  = e.value("completed_below", 0ULL);
        unsigned long long done = low + (e.contains("completed") ? e["completed"].size() : 0);

        unsigned long long open = 0, stale = 0;
        if (e.contains("open"))
            for (auto& [key, val] : e["open"].items()) {
                ++open;
                if (val.value("assigned_at", 0LL) <= cutoff) ++stale;
            }

        vector<json> found_list;
        unsigned long long found_n = 0;
        if (e.contains("found"))
            for (const auto& f : e["found"]) {
                ++found_n;
                try {
                    unsigned long long digits = 1;
                    for (const auto& d : f.at("dims")) digits *= stoull(d.get<string>());

                    json entry = f;
                    entry["digits"] = digits;
                    found_list.push_back(std::move(entry));
                } catch (...) {}
            }

        sort(found_list.begin(), found_list.end(), bigger_first);
        if (found_list.size() > top_n) found_list.resize(top_n);

        json frontier = {{"index", low * BLOCK_SIZE}};
        if (alg == "rect") {
            unsigned long long w, h;
            rect_at(low * BLOCK_SIZE / RECT_NFILLS, &w, &h);
            frontier["dims"] = {w, h};
        } else if (alg == "square") {
            unsigned long long side = low * BLOCK_SIZE / RECT_NFILLS + 1;
            frontier["dims"] = {side, side};
        }

        per_alg[alg] = {
            {"blocks_completed", done},
            {"patterns_tested", done * BLOCK_SIZE},
            {"open_blocks", open},
            {"stale_blocks", stale},
            {"primes_found", found_n},
            {"largest_digits", found_list.empty() ? 0ULL : found_list[0]["digits"].get<unsigned long long>()},
            {"frontier", frontier},
            {"top", found_list}
        };

        total_found += found_n;
    }

    size_t online = 0;
    for (auto& [id, c] : cl.items())
        if (now - c.value("last_seen", 0LL) < 300) ++online;

    json out;
    out["clients_total"]  = cl.size();
    out["clients_online"] = online;
    out["primes_found"]   = total_found;
    out["algorithms"]     = per_alg;
    return out;
}