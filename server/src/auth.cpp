#include "auth.h"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <ctime>
#include <vector>

using json = nlohmann::json;
namespace fs = std::filesystem;

static json db = {{"users", json::object()}, {"sessions", json::object()}};
static std::mutex auth_mutex;

static std::string to_hex(const unsigned char* d, size_t n) {
    static const char* h = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; i++) { s += h[d[i] >> 4]; s += h[d[i] & 15]; }
    return s;
}

static std::string random_hex(size_t bytes) {
    std::vector<unsigned char> b(bytes);
    RAND_bytes(b.data(), (int)bytes);
    return to_hex(b.data(), bytes);
}

static std::string hash_password(const std::string& pw, const std::string& salt) {
    unsigned char out[32];
    PKCS5_PBKDF2_HMAC(pw.c_str(), (int)pw.size(),
                      (const unsigned char*)salt.data(), (int)salt.size(),
                      200000, EVP_sha256(), sizeof(out), out);
    return to_hex(out, sizeof(out));
}

static void save_locked() {
    fs::create_directories("data");
    {
        std::ofstream f("data/auth.json.tmp");
        f << db.dump(2);
    }
    fs::rename("data/auth.json.tmp", "data/auth.json");
    fs::permissions("data/auth.json",
                    fs::perms::owner_read | fs::perms::owner_write,
                    fs::perm_options::replace);
}

static void prune_locked() {
    long now = (long)std::time(nullptr);
    auto& s = db["sessions"];
    for (auto it = s.begin(); it != s.end();) {
        if (it->value("expires", 0L) <= now) it = s.erase(it);
        else ++it;
    }
}

void auth_load() {
    std::lock_guard lock(auth_mutex);
    std::ifstream f("data/auth.json");
    if (!f) return;
    json parsed = json::parse(f, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) return;
    db = std::move(parsed);
    if (!db.contains("users"))    db["users"] = json::object();
    if (!db.contains("sessions")) db["sessions"] = json::object();
    prune_locked();
}

bool auth_add_user(const std::string& user, const std::string& password) {
    if (user.empty() || password.empty() || user.find(':') != std::string::npos)
        return false;
    std::lock_guard lock(auth_mutex);
    std::string salt = random_hex(16);
    db["users"][user] = {{"salt", salt}, {"hash", hash_password(password, salt)}};
    save_locked();
    return true;
}

bool auth_check_password(const std::string& user, const std::string& password) {
    std::string salt, expected;
    {
        std::lock_guard lock(auth_mutex);
        auto it = db["users"].find(user);
        if (it == db["users"].end()) return false;
        salt = it->value("salt", "");
        expected = it->value("hash", "");
    }
    // hash outside the lock (it's deliberately slow)
    std::string got = hash_password(password, salt);
    return got.size() == expected.size() &&
           CRYPTO_memcmp(got.data(), expected.data(), got.size()) == 0;
}

std::string auth_new_session(const std::string& user) {
    std::lock_guard lock(auth_mutex);
    std::string token = random_hex(32);
    db["sessions"][token] = {{"user", user},
                             {"expires", (long)std::time(nullptr) + SESSION_SECONDS}};
    prune_locked();
    save_locked();
    return token;
}

bool auth_session_valid(const std::string& token) {
    std::lock_guard lock(auth_mutex);
    auto it = db["sessions"].find(token);
    if (it == db["sessions"].end()) return false;
    return it->value("expires", 0L) > (long)std::time(nullptr);
}