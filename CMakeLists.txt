cmake_minimum_required(VERSION 4.1)

set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "d0edc3af-4c50-42ea-a356-e2862fe7a444")

project(gpp VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)

# ---------------- dependencies ----------------

set(FMT_MODULE ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_subdirectory(dependencies/spdlog)
add_subdirectory(dependencies/fmt)

# ------------------ my stuff ------------------

set(CMAKE_CXX_MODULE_STD 1)
set(CMAKE_CXX_SCAN_FOR_MODULES 1)
set(CMAKE_CXX_STANDARD 23)

if(MSVC)
    add_compile_options(/utf-8)
endif()

file(GLOB_RECURSE GPP_SOURCES "src/**/*.cpp")
file(GLOB_RECURSE GPP_MODULES "src/**/*.cppm")

add_library(gpp SHARED ${GPP_SOURCES})

target_sources(gpp
        PRIVATE FILE_SET private_modules TYPE CXX_MODULES FILES
        PUBLIC FILE_SET public_modules TYPE CXX_MODULES FILES
        ${GPP_MODULES}
)

set_target_properties(gpp PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

target_link_libraries(gpp PRIVATE spdlog::spdlog_module fmt::fmt)

add_executable(scratch)

target_sources(scratch
        PRIVATE
        tests/scratch/main.cpp)

target_link_libraries(scratch PRIVATE gpp)

# Copy DLLs to executable directory on Windows
if(WIN32)
    add_custom_command(TARGET scratch POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:gpp>
            $<TARGET_FILE_DIR:scratch>
        COMMENT "Copying gpp.dll to executable directory"
    )
    add_custom_command(TARGET scratch POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:spdlog>
            $<TARGET_FILE_DIR:scratch>
        COMMENT "Copying spdlog.dll to executable directory"
    )
endif()

