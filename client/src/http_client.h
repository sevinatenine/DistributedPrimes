#pragma once

#include <cctype>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <sys/time.h>
    #include <netdb.h>
    #include <unistd.h>
    using SOCKET = int;
    constexpr SOCKET INVALID_SOCKET = -1;
    constexpr int SOCKET_ERROR = -1;
    #define closesocket(s) close(s)
#endif

struct HttpResponse {
    int status = 0;
    std::string status_text;
    std::map<std::string, std::string> headers;  // keys are lowercased
    std::string body;
};

namespace http_detail {

inline std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

inline std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

inline void init_sockets() {
#ifdef _WIN32
    static const bool once = [] { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); return true; }();
    (void)once;
#endif
}

// Splits "http://host:port/path" (also "[::1]:8080")
inline bool parse_url(std::string url, std::string& host, std::string& port, std::string& path) {
    if (url.rfind("http://", 0) == 0) url = url.substr(7);
    else if (url.rfind("https://", 0) == 0) return false;  // no TLS support

    size_t slash = url.find('/');
    std::string authority = url.substr(0, slash);
    path = slash == std::string::npos ? "/" : url.substr(slash);

    port = "80";
    if (!authority.empty() && authority[0] == '[') {
        size_t end = authority.find(']');
        if (end == std::string::npos) return false;
        host = authority.substr(1, end - 1);
        if (end + 1 < authority.size() && authority[end + 1] == ':')
            port = authority.substr(end + 2);
    } else {
        size_t colon = authority.rfind(':');
        if (colon != std::string::npos) {
            host = authority.substr(0, colon);
            port = authority.substr(colon + 1);
        } else {
            host = authority;
        }
    }
    return !host.empty() && !port.empty();
}

inline bool decode_chunked(const std::string& in, std::string& out) {
    size_t pos = 0;
    while (true) {
        size_t eol = in.find("\r\n", pos);
        if (eol == std::string::npos) return false;

        size_t size = std::strtoull(in.substr(pos, eol - pos).c_str(), nullptr, 16);  // ignores ";ext"
        pos = eol + 2;
        if (size == 0) return true;  // trailers ignored

        if (pos + size > in.size()) return false;
        out.append(in, pos, size);
        pos += size + 2;  // skip data + CRLF
    }
}

} // namespace http_detail

// Returns nullopt on failure; `error` (optional) says why.
inline std::optional<HttpResponse> http_request(
    const std::string& method,
    const std::string& url,
    const std::string& body = "",
    const std::map<std::string, std::string>& extra_headers = {},
    std::string* error = nullptr,
    int timeout_seconds = 10
) {
    using namespace http_detail;
    auto fail = [&](const std::string& msg) -> std::optional<HttpResponse> {
        if (error) *error = msg;
        return std::nullopt;
    };

    init_sockets();

    std::string host, port, path;
    if (!parse_url(url, host, port, path))
        return fail("Invalid or unsupported URL (only http:// is supported)");

    // Resolve + connect (tries every address returned)
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* result = nullptr;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &result) != 0)
        return fail("DNS lookup failed for " + host);

    SOCKET sock = INVALID_SOCKET;
    for (addrinfo* ai = result; ai; ai = ai->ai_next) {
        sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (sock == INVALID_SOCKET) continue;

#ifdef _WIN32
        DWORD ms = timeout_seconds * 1000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&ms, sizeof(ms));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&ms, sizeof(ms));
#else
        timeval tv{timeout_seconds, 0};
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    #ifdef SO_NOSIGPIPE
        int yes = 1;
        setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
    #endif
#endif

        if (connect(sock, ai->ai_addr, (int)ai->ai_addrlen) == 0) break;
        closesocket(sock);
        sock = INVALID_SOCKET;
    }
    freeaddrinfo(result);
    if (sock == INVALID_SOCKET) return fail("Could not connect to " + host + ":" + port);

    // Build and send the request
    std::string req = method + " " + path + " HTTP/1.1\r\n";
    req += "Host: " + host + (port == "80" ? "" : ":" + port) + "\r\n";
    req += "Connection: close\r\n";
    req += "User-Agent: simple-cpp-client/1.0\r\n";
    for (const auto& [k, v] : extra_headers) req += k + ": " + v + "\r\n";
    if (!body.empty() || method == "POST" || method == "PUT")
        req += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    req += "\r\n" + body;

#if defined(MSG_NOSIGNAL)
    const int send_flags = MSG_NOSIGNAL;
#else
    const int send_flags = 0;
#endif
    size_t sent = 0;
    while (sent < req.size()) {
        int n = (int)send(sock, req.data() + sent, (int)(req.size() - sent), send_flags);
        if (n <= 0) { closesocket(sock); return fail("Send failed"); }
        sent += n;
    }

    // Read until the server closes the connection
    constexpr size_t MAX_RESPONSE = 64 * 1024 * 1024;
    std::string raw;
    char buf[4096];
    while (true) {
        int n = (int)recv(sock, buf, sizeof(buf), 0);
        if (n < 0) { closesocket(sock); return fail("Receive failed or timed out"); }
        if (n == 0) break;
        raw.append(buf, n);
        if (raw.size() > MAX_RESPONSE) { closesocket(sock); return fail("Response too large"); }
    }
    closesocket(sock);

    // Split head and body
    size_t head_end = raw.find("\r\n\r\n");
    if (head_end == std::string::npos) return fail("Malformed response (no header terminator)");

    HttpResponse res;
    std::string head = raw.substr(0, head_end);
    std::string payload = raw.substr(head_end + 4);

    // Status line: "HTTP/1.1 200 OK"
    size_t line_end = head.find("\r\n");
    std::string status_line = head.substr(0, line_end);
    size_t sp1 = status_line.find(' ');
    if (sp1 == std::string::npos) return fail("Malformed status line");
    size_t sp2 = status_line.find(' ', sp1 + 1);
    res.status = std::atoi(status_line.substr(sp1 + 1, sp2 == std::string::npos ? std::string::npos : sp2 - sp1 - 1).c_str());
    if (sp2 != std::string::npos) res.status_text = status_line.substr(sp2 + 1);

    // Headers
    size_t pos = line_end == std::string::npos ? head.size() : line_end + 2;
    while (pos < head.size()) {
        size_t eol = head.find("\r\n", pos);
        if (eol == std::string::npos) eol = head.size();
        std::string line = head.substr(pos, eol - pos);
        pos = eol + 2;

        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = lower(trim(line.substr(0, colon)));
        std::string val = trim(line.substr(colon + 1));

        auto it = res.headers.find(key);
        if (it == res.headers.end()) res.headers[key] = val;
        else it->second += ", " + val;
    }

    // Body
    auto te = res.headers.find("transfer-encoding");
    auto cl = res.headers.find("content-length");
    if (te != res.headers.end() && lower(te->second).find("chunked") != std::string::npos) {
        if (!decode_chunked(payload, res.body)) return fail("Malformed chunked body");
    } else if (cl != res.headers.end()) {
        size_t len = std::strtoull(cl->second.c_str(), nullptr, 10);
        if (payload.size() < len) return fail("Response body truncated");
        res.body = payload.substr(0, len);
    } else {
        res.body = std::move(payload);  // body ends at connection close
    }

    return res;
}