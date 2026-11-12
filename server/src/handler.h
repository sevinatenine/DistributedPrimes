#pragma once
#include <map>
#include <string>
#include <vector>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <unistd.h>
    #include <arpa/inet.h>

    using SOCKET = int;

    constexpr SOCKET INVALID_SOCKET = -1;
    constexpr int SOCKET_ERROR = -1;

    #define closesocket(s) close(s)
#endif

void handle_client(
    SOCKET client_socket,
    sockaddr_storage client_addr,
    socklen_t client_len
);

struct Request {
    std::string method;
    std::vector<std::string> path_split;
    std::string version;
    std::map<std::string, std::string> headers;
    sockaddr_storage address{};
};