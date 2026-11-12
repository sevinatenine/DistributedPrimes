#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <map>
#include <ranges>

#include "handler.h"
#include "request_handlers.h"

using namespace std;

const int BUFFER_SIZE = 4096;

string generate_response(const Response& res) {
    return
        "HTTP/1.1 " + to_string(res.statusCode) + " " + res.statusName + "\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: " + to_string(res.body.size()) + "\r\n"
        "Connection: close\r\n" +
        res.headers +
        "\r\n" + res.body;
}

Request parse_request(string req) {
    Request request;

    unsigned int part = 0;
    string buffer;

    for (unsigned int i = 0; i <= req.size(); i++) {
        if (i == req.size() || req[i] == '\r' || req[i] == '\n') {

            if (part == 0) {
                size_t first_space = buffer.find(' ');
                size_t second_space = buffer.find(' ', first_space + 1);

                if (first_space != string::npos &&
                    second_space != string::npos) {

                    request.method = buffer.substr(0, first_space);

                    string path = buffer.substr(
                        first_space + 1,
                        second_space - first_space - 1
                    );

                    request.version = buffer.substr(second_space + 1);

                    for (auto segment : path | views::split('/')) {
                        request.path_split.emplace_back(
                            segment.begin(), segment.end()
                        );
                    }
                }

                part++;
            }
            else if (!buffer.empty()) {
                size_t colon = buffer.find(':');

                if (colon != string::npos) {
                    string key = buffer.substr(0, colon);
                    for (auto& ch : key) ch = (char)tolower((unsigned char)ch);

                    string value = buffer.substr(colon + 1);

                    size_t start = value.find_first_not_of(" \t");

                    if (start != string::npos) {
                        value = value.substr(start);
                    } else {
                        value.clear();
                    }

                    request.headers[key] = value;
                }
            }

            buffer.clear();

            if (i < req.size() && req[i] == '\r' &&
                i + 1 < req.size() && req[i + 1] == '\n') {
                i++;
            }

            if (part > 0 && (i == req.size() ||
                (i + 1 < req.size() &&
                 (req[i + 1] == '\r' || req[i + 1] == '\n')))) {
                break;
            }
        }
        else {
            buffer.push_back(req[i]);
        }
    }

    return request;
}

void handle_client(
    SOCKET client_socket,
    sockaddr_storage client_addr,
    socklen_t client_len
) {
    (void)client_len;

#ifdef SO_NOSIGPIPE   // macOS/BSD
    int yes = 1;
    setsockopt(client_socket, SOL_SOCKET, SO_NOSIGPIPE,
               (const char*)&yes, sizeof(yes));
#endif

    // Don't let a silent client pin a thread forever.
#ifdef _WIN32
    DWORD tv = 5000;
    setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO,
               (const char*)&tv, sizeof(tv));
#else
    timeval tv{5, 0};
    setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    vector<char> buffer(BUFFER_SIZE);

    int bytes_received = recv(
        client_socket,
        buffer.data(),
        BUFFER_SIZE - 1,
        0
    );

    if (bytes_received <= 0) {
        closesocket(client_socket);
        return;
    }

    buffer[bytes_received] = '\0';
    string request(buffer.data());

    auto now = chrono::system_clock::to_time_t(
        chrono::system_clock::now()
    );

    tm local_time{};

#ifdef _WIN32
    localtime_s(&local_time, &now);
#else
    localtime_r(&now, &local_time);
#endif

    stringstream ss;
    ss << put_time(&local_time, "%Y-%m-%d %H:%M:%S");

    string time = ss.str();

    char ip[INET6_ADDRSTRLEN] = {};
    int port = 0;

    if (client_addr.ss_family == AF_INET) {
        auto* addr = (sockaddr_in*)&client_addr;

        inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));
        port = ntohs(addr->sin_port);

    } else if (client_addr.ss_family == AF_INET6) {
        auto* addr = (sockaddr_in6*)&client_addr;

        inet_ntop(AF_INET6, &addr->sin6_addr, ip, sizeof(ip));
        port = ntohs(addr->sin6_port);
    }

    Request req = parse_request(request);
    req.address = client_addr;
    Response res = handle_request(req);
    string response = generate_response(res);

    cout << "[Thread " << this_thread::get_id()
        << "] (" << ip << ":" << to_string(port) + ") <" + time + ">: "
        << request.substr(0, request.find("\r\n"))
        << " - " << res.statusCode << "\n";

    size_t total_sent = 0;

    while (total_sent < response.size()) {
        int bytes_sent = send(
            client_socket,
            response.c_str() + total_sent,
            static_cast<int>(response.size() - total_sent),
            MSG_NOSIGNAL
        );

        if (bytes_sent <= 0) {
            cerr << "Failed to send response.\n";
            break;
        }

        total_sent += bytes_sent;
    }

    closesocket(client_socket);
}