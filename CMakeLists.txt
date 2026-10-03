cmake_minimum_required(VERSION 4.4)

set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "f35a9ac6-8463-4d38-8eec-5d6008153e7d")

project(gpp VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)

include(cmake/Dependencies.cmake)
include(cmake/GppLibrary.cmake)

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(cmake/Scratch.cmake)
    include(cmake/Tests.cmake)
endif ()
