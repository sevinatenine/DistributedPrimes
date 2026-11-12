cmake_minimum_required(VERSION 3.15)
project(DistributedPrimeClient VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_path(GMP_INCLUDE_DIR NAMES gmp.h gmpxx.h)

# 2. Find both required libraries
find_library(GMP_LIBRARY NAMES gmp)
find_library(GMPXX_LIBRARY NAMES gmpxx)

# 3. Verify everything was found successfully
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GMPXX
    REQUIRED_VARS GMP_INCLUDE_DIR GMP_LIBRARY GMPXX_LIBRARY
)

include(FetchContent)
find_package(nlohmann_json 3.11 QUIET)
if(NOT nlohmann_json_FOUND)
    FetchContent_Declare(json
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz)
    FetchContent_MakeAvailable(json)
endif()

add_executable(distributed_prime_client src/main.cpp src/work.cpp)

# 4. Link headers and libraries to your target
if(GMPXX_FOUND)
    target_include_directories(distributed_prime_client PRIVATE ${GMP_INCLUDE_DIR})
    target_link_libraries(distributed_prime_client PRIVATE ${GMPXX_LIBRARY} ${GMP_LIBRARY})
endif()

target_link_libraries(distributed_prime_client PRIVATE nlohmann_json::nlohmann_json)