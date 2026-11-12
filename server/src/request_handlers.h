#pragma once
#include <string>
#include "handler.h"

struct Response {
    std::string body;
    unsigned int statusCode = 200;
    std::string statusName = "OK";
    std::string headers;
};

Response handle_request(const Request& req);