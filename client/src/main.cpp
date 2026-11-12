#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <gmpxx.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

#include "http_client.h"
#include "work.h"

using namespace std;

static atomic<bool> running{true};
static mutex wake_mutex;
static condition_variable wake_cv;

static void on_signal(int) {
    running = false;
}

static void nap(int seconds) {
    auto deadline = chrono::steady_clock::now() + chrono::seconds(seconds);
    while (running && chrono::steady_clock::now() < deadline)
        this_thread::sleep_for(chrono::milliseconds(100));
}

// "192.168.1.5:8080" -> "http://192.168.1.5:8080", strips trailing slashes
static string normalize_server(string s) {
    if (s.rfind("http://", 0) != 0 && s.rfind("https://", 0) != 0)
        s = "http://" + s;
    while (!s.empty() && s.back() == '/') s.pop_back();
    return s;
}

static void usage(const char* prog) {
    cerr << "Usage: " << prog << " [server-address]\n"
         << "  e.g. " << prog << " 192.168.1.5:8080\n"
         << "       " << prog << " http://example.com:8080\n"
         << "If omitted, DISTRIBUTED_PRIMES_SERVER is used, then http://localhost:8080.\n"
         << "DISTRIBUTED_PRIMES_CLIENT_ID must always be set.\n";
}

void ping_loop(const string& server, const string& client_id) {
    while (running) {
        string err;
        auto res = http_request("GET", server + "/api/ping/" + client_id, "", {}, &err);

        if (!res) {
            cerr << "Ping failed: " << err << "\n";
        } else if (res->status != 200) {
            cerr << "Ping returned " << res->status << " " << res->status_text << "\n";
        }

        unique_lock lock(wake_mutex);
        auto deadline = chrono::steady_clock::now() + chrono::seconds(5);
        while (running && chrono::steady_clock::now() < deadline) {
            wake_cv.wait_for(lock, chrono::milliseconds(500));
        }
    }
}

int main(int argc, char** argv) {
    if (argc > 2 || (argc == 2 && (string(argv[1]) == "-h" || string(argv[1]) == "--help"))) {
        usage(argv[0]);
        return argc > 2 ? 1 : 0;
    }

    const char* id_env = getenv("DISTRIBUTED_PRIMES_CLIENT_ID");
    if (!id_env || !*id_env) {
        cerr << "The variable DISTRIBUTED_PRIMES_CLIENT_ID must be set.\n";
        return 1;
    }
    const string client_id = id_env;

    string server_raw = "http://localhost:8080";
    if (argc == 2) {
        server_raw = argv[1];
    } else if (const char* server_env = getenv("DISTRIBUTED_PRIMES_SERVER")) {
        if (*server_env) server_raw = server_env;
    }
    const string server = normalize_server(server_raw);

    cout << "Using server " << server << " as client " << client_id << endl;

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    thread pinger(ping_loop, cref(server), cref(client_id));

    while (running) {
        string err;
        auto res = http_request("POST", server + "/api/work/" + client_id, "", {}, &err);

        if (!res) {
            cerr << "Work request failed: " << err << "\n";
            nap(5);
            continue;
        }
        if (res->status != 200) {
            cerr << "Work request returned " << res->status << ": " << res->body << "\n";
            nap(2);
            continue;
        }

        json work = json::parse(res->body, nullptr, false);
        if (work.is_discarded() || !work.contains("block") || !work.contains("alg")) {
            cerr << "Server sent malformed work\n";
            nap(5);
            continue;
        }

        auto t0 = chrono::steady_clock::now();
        auto found = run_block(work, running);
        if (!found) break;   // Ctrl+C mid-block: leave it open

        cout << "Block took "
             << chrono::duration<double>(chrono::steady_clock::now() - t0).count()
             << "s\n";

        string alg = work["alg"].get<string>();
        unsigned long long block = work["block"].get<unsigned long long>();

        bool all_reported = true;
        for (const auto& f : *found) {
            cout << "Prime found #" << f.index << " (" << f.shape << "): " << f.number << endl;

            auto r = http_request("POST",
                server + "/api/found/" + client_id + "/" + alg + "/" + to_string(f.index),
                "", {}, &err);
            if (!r || r->status != 200) {
                cerr << "Could not report prime #" << f.index << "\n";
                all_reported = false;
            }
        }

        if (!all_reported) { nap(5); continue; }

        auto done = http_request("POST",
            server + "/api/complete/" + client_id + "/" + alg + "/" + to_string(block),
            "", {}, &err);
        if (!done || done->status != 200)
            cerr << "Could not report block " << block << " complete\n";
        else
            cout << "Block " << block << " done (" << found->size() << " primes)\n";
    }

    wake_cv.notify_all();
    pinger.join();
    return 0;
}