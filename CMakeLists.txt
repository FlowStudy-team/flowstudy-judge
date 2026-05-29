cmake_minimum_required(VERSION 3.21)
project(FlowStudy-Judge VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(nlohmann_json REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(RABBITMQ REQUIRED librabbitmq)
pkg_check_modules(MYSQL REQUIRED mysqlclient)
pkg_check_modules(SPDLOG REQUIRED spdlog)

add_subdirectory(src)

option(BUILD_TESTS "Build test suite" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
