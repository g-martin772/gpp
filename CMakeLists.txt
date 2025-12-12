cmake_minimum_required(VERSION 4.1)

set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "d0edc3af-4c50-42ea-a356-e2862fe7a444")

project(gpp VERSION 0.1.0 LANGUAGES CXX)

# ---------------- dependencies ----------------

add_subdirectory(dependencies/spdlog)

# ------------------ my stuff ------------------

set(CMAKE_CXX_MODULE_STD 1)
set(CMAKE_CXX_SCAN_FOR_MODULES 1)
set(CMAKE_CXX_STANDARD 23)

if(MSVC)
    add_compile_options(/utf-8)
endif()

add_library(gpp SHARED)

target_sources(gpp
        PRIVATE FILE_SET private_modules TYPE CXX_MODULES FILES
        PUBLIC FILE_SET public_modules TYPE CXX_MODULES FILES
        src/core/test.cppm
)

set_target_properties(gpp PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

target_link_libraries(gpp PRIVATE spdlog::spdlog_module)

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

