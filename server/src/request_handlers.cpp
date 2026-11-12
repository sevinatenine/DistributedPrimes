#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <optional>
#include <ctime>
#include <iostream>

#include "request_handlers.h"
#include "clients.h"
#include "auth.h"
#include "algorithms/algs.h"
#include "algorithms/data_manager.h"


using namespace std;

optional<string> get_file(const string& path) {
    ifstream file(path, ios::binary);
    if (!file) return nullopt;

    stringstream out;
    out << file.rdbuf();
    return out.str();
}

static string clean_segment(const string& s) {
    return s.substr(0, s.find_first_of("?#"));
}

Response not_found() {
    auto body = get_file("./public/404.html");
    return {body.value_or("<h1>404 Not Found</h1>"), 404, "Not Found"};
}

const map<string, string> staticRoutes = {
    {"dashboard", "dashboard.html"},
    {"info", "info.html"}
};

static void address_to_string(const sockaddr_storage& addr, string& ip, int& port) {
    char buf[INET6_ADDRSTRLEN] = {};
    ip.clear();
    port = 0;

    if (addr.ss_family == AF_INET) {
        auto* a = (const sockaddr_in*)&addr;
        inet_ntop(AF_INET, &a->sin_addr, buf, sizeof(buf));
        port = ntohs(a->sin_port);
    } else if (addr.ss_family == AF_INET6) {
        auto* a = (const sockaddr_in6*)&addr;
        inet_ntop(AF_INET6, &a->sin6_addr, buf, sizeof(buf));
        port = ntohs(a->sin6_port);
    }

    ip = buf;

    if (ip.rfind("::ffff:", 0) == 0 && ip.find('.') != string::npos) {
        ip = ip.substr(7);
    }
}

static Response handle_request_inner(const Request& req) {
    if (req.path_split.empty()) return not_found();

    if (req.path_split.size() <= 2 && req.path_split.back().empty()) {
        if (auto body = get_file("./public/index.html")) return {*body, 200, "OK"};
        return not_found();
    }

    string segment = req.path_split[1];
    segment = segment.substr(0, segment.find_first_of("?#"));

    if (segment == "api" && req.path_split.size() > 2) {
        string command = req.path_split[2];
        command = command.substr(0, command.find_first_of("?#"));

        if (command == "ping" && req.path_split.size() > 3) {
            string arg = req.path_split[3];
            arg = arg.substr(0, arg.find_first_of("?#"));

            if (!arg.empty()) {
                string ip;
                int port;
                address_to_string(req.address, ip, port);

                auto now = std::time(nullptr);
                if (update_time(arg, now)) {
                    update_address(arg, ip, port);
                } else {
                    add_client(arg, ip, port, now, ALGORITHMS[0]);
                }

                return {"Success", 200, "OK"};
            }
        } else if (command == "clients") {
            json out = json::parse(clients_to_string());
            for (auto& [id, c] : out.items())
                c["stats"]["primes_found"] = count_found_by(id);
            return { out.dump(), 200, "OK" };
        } else if (command == "delete" && req.path_split.size() > 3) {
            if (req.method != "POST")
                return {"Method Not Allowed", 405, "Method Not Allowed"};

            string arg = clean_segment(req.path_split[3]);
            release_client_blocks(arg);   // put its work back in the pool

            if (remove_client(arg)) return {"Deleted", 200, "OK"};
            return {"Client not found", 404, "Not Found"};

        } else if (command == "config" && req.path_split.size() > 5) {
            if (req.method != "POST")
                return {"Method Not Allowed", 405, "Method Not Allowed"};

            string id    = clean_segment(req.path_split[3]);
            string key   = clean_segment(req.path_split[4]);
            string value = clean_segment(req.path_split[5]);

            if (key == "algorithm") {
                if (!is_valid_algorithm(value))
                    return {"Unknown algorithm", 400, "Bad Request"};
                if (!update_algorithm(id, value))
                    return {"Client not found", 404, "Not Found"};
                return {"Success", 200, "OK"};
            }
            return {"Invalid key", 400, "Bad Request"};

        } else if (command == "work" && req.path_split.size() > 3) {
            if (req.method != "POST")
                return {"Method Not Allowed", 405, "Method Not Allowed"};

            string id  = clean_segment(req.path_split[3]);
            string alg = get_client_algorithm(id);
            if (alg.empty()) return {"Unknown client (ping first)", 404, "Not Found"};

            auto work = request_work(alg, id);
            if (!work) return {"Unknown algorithm", 400, "Bad Request"};

            set_current_block(id, (*work)["block"].get<unsigned long long>());
            return {work->dump(), 200, "OK"};

        } else if (command == "complete" && req.path_split.size() > 5) {
            if (req.method != "POST")
                return {"Method Not Allowed", 405, "Method Not Allowed"};

            string id  = clean_segment(req.path_split[3]);
            string alg = clean_segment(req.path_split[4]);

            unsigned long long block;
            try {
                block = stoull(clean_segment(req.path_split[5]));
            } catch (...) {
                return {"Bad block number", 400, "Bad Request"};
            }

            if (finish_work(alg, block, id)) {
                record_block_done(id);
                return {"Completed", 200, "OK"};
            }
            return {"Block isn't open for this client", 409, "Conflict"};
        } else if (command == "algorithms") {
            json algs = json::array();
            for (const auto& a : ALGORITHMS) algs.push_back(string(a));

            return {algs.dump(), 200, "OK"};
        } else if (command == "found") {
            if (req.method == "GET")
                return {found_to_string(2), 200, "OK"};

            // POST /api/found/<client>/<alg>/<index>
            if (req.method == "POST" && req.path_split.size() > 5) {
                string id  = clean_segment(req.path_split[3]);
                string alg = clean_segment(req.path_split[4]);

                unsigned long long index;
                try {
                    index = stoull(clean_segment(req.path_split[5]));
                } catch (...) {
                    return {"Bad index", 400, "Bad Request"};
                }

                if (!report_found(alg, index, id))
                    return {"Unknown algorithm or invalid pattern", 400, "Bad Request"};

                write_progress();   // finds are rare, so save immediately
                return {"Recorded", 200, "OK"};
            }
            return {"Method Not Allowed", 405, "Method Not Allowed"};
        } else if (command == "stats") {
            return {get_stats().dump(), 200, "OK"};
        }
    }

    auto route = staticRoutes.find(segment);
    if (route != staticRoutes.end()) {
        if (auto body = get_file("./public/" + route->second)) {
            return {*body, 200, "OK"};
        }
    }

    return not_found();
}

// ---------------------------------------------------------------- auth

static string b64_decode(const string& in) {
    static const string tbl =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    string out;
    int val = 0, bits = -8;
    for (unsigned char c : in) {
        if (c == '=') break;
        auto p = tbl.find((char)c);
        if (p == string::npos) continue;
        val = (val << 6) + (int)p;
        bits += 6;
        if (bits >= 0) { out.push_back((char)((val >> bits) & 0xFF)); bits -= 8; }
    }
    return out;
}

static bool parse_basic(const Request& req, string& user, string& pass) {
    auto it = req.headers.find("authorization");
    if (it == req.headers.end()) return false;
    const string& v = it->second;
    if (v.rfind("Basic ", 0) != 0) return false;
    string decoded = b64_decode(v.substr(6));
    auto colon = decoded.find(':');
    if (colon == string::npos) return false;
    user = decoded.substr(0, colon);
    pass = decoded.substr(colon + 1);
    return true;
}

static string get_cookie(const Request& req, const string& name) {
    auto it = req.headers.find("cookie");
    if (it == req.headers.end()) return "";
    const string& c = it->second;
    string key = name + "=";
    size_t pos = 0;
    while (pos < c.size()) {
        size_t end = c.find(';', pos);
        if (end == string::npos) end = c.size();
        size_t start = c.find_first_not_of(' ', pos);
        if (start != string::npos && start < end &&
            c.compare(start, key.size(), key) == 0)
            return c.substr(start + key.size(), end - start - key.size());
        pos = end + 1;
    }
    return "";
}

static bool needs_auth(const Request& req) {
    if (req.path_split.size() < 3) return true;           // "/", "/dashboard", ...
    if (clean_segment(req.path_split[1]) != "api") return true;

    string cmd = clean_segment(req.path_split[2]);
    // endpoints the worker clients use stay open
    if (cmd == "ping" || cmd == "work" || cmd == "complete" || cmd == "algorithms")
        return false;
    if (cmd == "found" && req.method == "POST") return false;
    return true;   // clients, delete, config, stats, GET found
}

Response handle_request(const Request& req) {
    if (!needs_auth(req)) return handle_request_inner(req);

    // 1. valid session cookie?
    string token = get_cookie(req, "session");
    if (!token.empty() && auth_session_valid(token))
        return handle_request_inner(req);

    // 2. Basic credentials from the browser prompt?
    string user, pass;
    if (parse_basic(req, user, pass) && auth_check_password(user, pass)) {
        Response res = handle_request_inner(req);
        res.headers += "Set-Cookie: session=" + auth_new_session(user) +
                       "; Max-Age=" + to_string(SESSION_SECONDS) +
                       "; Path=/; HttpOnly; SameSite=Lax\r\n";
        return res;
    }

    // 3. ask for credentials
    Response res{"Unauthorized", 401, "Unauthorized"};
    res.headers = "WWW-Authenticate: Basic realm=\"Prime Server\"\r\n";
    return res;
}