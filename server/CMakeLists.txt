cmake_minimum_required(VERSION 3.15)
project(DistributedPrimeServer VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
find_package(nlohmann_json 3.11 QUIET)
if(NOT nlohmann_json_FOUND)
    FetchContent_Declare(json
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz)
    FetchContent_MakeAvailable(json)
endif()
find_package(OpenSSL REQUIRED)

add_executable(distributed_prime_server src/main.cpp src/handler.cpp src/request_handlers.cpp src/clients.cpp src/algorithms.cpp src/algorithms/algs.cpp src/algorithms/rect.cpp src/algorithms/data_manager.cpp src/auth.cpp)
target_link_libraries(distributed_prime_server PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(distributed_prime_server PRIVATE OpenSSL::Crypto)