cmake_minimum_required(VERSION 4.1)

set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "d0edc3af-4c50-42ea-a356-e2862fe7a444")

project(gpp LANGUAGES CXX)

set(CMAKE_CXX_MODULE_STD 1)
set(CMAKE_CXX_SCAN_FOR_MODULES 1)
set(CMAKE_CXX_STANDARD 23)


add_executable(gpp
        test.cppm)

target_sources(gpp
        PRIVATE
        main.cpp
        PRIVATE FILE_SET modules TYPE CXX_MODULES FILES
        test.cppm
)
