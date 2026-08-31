cmake_minimum_required(VERSION 4.4)

set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "f35a9ac6-8463-4d38-8eec-5d6008153e7d")

project(gpp VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)

# ---------------- dependencies ----------------

set(FMT_MODULE ON)

find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(Catch2 CONFIG REQUIRED)

# ------------------ my stuff ------------------

set(CMAKE_CXX_MODULE_STD 1)
set(CMAKE_CXX_SCAN_FOR_MODULES 1)
set(CMAKE_CXX_STANDARD 23)

file(GLOB_RECURSE GPP_SOURCES CONFIGURE_DEPENDS "src/**/*.cpp")
file(GLOB_RECURSE GPP_MODULES CONFIGURE_DEPENDS "src/**/*.cppm")

add_library(gpp SHARED ${GPP_SOURCES})
target_sources(gpp
        PRIVATE FILE_SET private_modules TYPE CXX_MODULES FILES
        PUBLIC FILE_SET public_modules TYPE CXX_MODULES FILES
        ${GPP_MODULES} "src/gpp.cppm"
)
target_link_libraries(gpp PUBLIC spdlog::spdlog fmt::fmt)

add_executable(scratch)
target_sources(scratch
        PRIVATE
        tests/scratch/main.cpp)
target_link_libraries(scratch PRIVATE gpp)

# ------ tests -------

add_executable(unit_tests)
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "tests/unit/**/*.cpp")
target_sources(unit_tests
        PRIVATE
        ${TEST_SOURCES})
target_link_libraries(unit_tests PRIVATE gpp)
target_link_libraries(unit_tests PRIVATE Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(unit_tests)