#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <csignal>
#include <atomic>
#include <chrono>

#include "handler.h"
#include "clients.h"
#include "auth.h"
#include "algorithms/algs.h"
#include "algorithms/data_manager.h"

using namespace std;

const int PORT = 8080;
const auto SAVE_INTERVAL = chrono::seconds(5);

static atomic<bool> running{true};

static void on_signal(int) {
    running = false;
}

int main(int argc, char** argv) {
    // ./server --adduser <name>   (creates/updates a login, then exits)
    if (argc == 3 && string(argv[1]) == "--adduser") {
        auth_load();   // <-- add this, so existing users are kept
#ifndef _WIN32
        string pw = getpass("Password: ");
#else
        string pw;
        cout << "Password: ";
        getline(cin, pw);
#endif
        if (auth_add_user(argv[2], pw)) {
            cout << "User saved.\n";
            return 0;
        }
        cerr << "Invalid username or password.\n";
        return 1;
    }

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "Winsock initialization failed.\n";
        return 1;
    }
#endif

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    SOCKET server_socket = socket(AF_INET6, SOCK_STREAM, 0);

    if (server_socket == INVALID_SOCKET) {
        cerr << "Failed to create socket.\n";
        return 1;
    }

    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR,
            (const char*)&opt, sizeof(opt));

    int dual_stack = 0;
    if (setsockopt(server_socket, IPPROTO_IPV6, IPV6_V6ONLY,
                (const char*)&dual_stack, sizeof(dual_stack)) == SOCKET_ERROR) {
        cerr << "Failed to enable dual-stack mode.\n";
        closesocket(server_socket);
        return 1;
    }

    sockaddr_in6 server_addr{};

    server_addr.sin6_family = AF_INET6;
    server_addr.sin6_addr = in6addr_any;
    server_addr.sin6_port = htons(PORT);

    if (::bind(server_socket,
            (struct sockaddr*)&server_addr,
            sizeof(server_addr)) == SOCKET_ERROR) {
        cerr << "Bind failed.\n";
        closesocket(server_socket);
        return 1;
    }

    if (listen(server_socket, SOMAXCONN) == SOCKET_ERROR) {
        cerr << "Listen failed.\n";
        closesocket(server_socket);
        return 1;
    }

    cout << "Loading clients" << endl;

    load_clients();
    auth_load();

    cout << "Initializing algorithms" << endl;

    init_algs();

    cout << "HTTP Server running on port " << PORT << "...\n";

    auto last_save = chrono::steady_clock::now();

    while (running) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(server_socket, &readfds);
        timeval timeout{1, 0};

        int ready = select((int)server_socket + 1, &readfds, nullptr, nullptr, &timeout);

        if (ready > 0 && FD_ISSET(server_socket, &readfds)) {
            sockaddr_storage client_addr{};
            socklen_t client_len = sizeof(client_addr);

            SOCKET client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
            if (client_socket != INVALID_SOCKET) {
                thread client_thread(handle_client, client_socket, client_addr, client_len);
                client_thread.detach();
            }
        }

        auto now = chrono::steady_clock::now();
        if (now - last_save >= SAVE_INTERVAL) {
            write_clients();
            write_progress();
            last_save = now;
        }
    }

    cout << "\nShutting down, saving clients...\n";
    write_clients();
    write_progress();

    closesocket(server_socket);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}