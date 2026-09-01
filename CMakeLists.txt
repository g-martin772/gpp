cmake_minimum_required(VERSION 4.4)

set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "f35a9ac6-8463-4d38-8eec-5d6008153e7d")

project(gpp VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)

# ---------------- dependencies ----------------

set(FMT_MODULE ON)
set(Vulkan_ROOT "/usr")
set(VULKAN_SDK "/usr")

find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(Catch2 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(spirv_cross_core CONFIG REQUIRED)
find_package(spirv_cross_glsl CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Vulkan REQUIRED COMPONENTS shaderc_combined)

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
target_link_libraries(gpp PUBLIC spdlog::spdlog fmt::fmt nlohmann_json::nlohmann_json imgui::imgui glm::glm)
target_link_libraries(gpp PRIVATE SDL3::SDL3 Vulkan::Headers GPUOpen::VulkanMemoryAllocator
        Vulkan::shaderc_combined spirv-cross-core spirv-cross-glsl)

# ---- scratch -----

add_executable(scratch)
target_sources(scratch
        PRIVATE
        tests/scratch/main.cpp)
target_link_libraries(scratch PRIVATE gpp)
# add_custom_command(
#         OUTPUT "${SCRATCH_CONFIG_OUTPUT}"
#         DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/scratch/config.json"
#         COMMAND ${CMAKE_COMMAND} -E copy
#         "${CMAKE_CURRENT_SOURCE_DIR}/tests/scratch/config.json"
#         "${SCRATCH_CONFIG_OUTPUT}")
# target_sources(scratch PRIVATE "${SCRATCH_CONFIG_OUTPUT}")

# ------ tests -------

add_executable(unit_tests)
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "tests/unit/**/*.cpp")
target_sources(unit_tests
        PRIVATE
        ${TEST_SOURCES})
target_link_libraries(unit_tests PRIVATE gpp)
target_link_libraries(unit_tests PRIVATE Catch2::Catch2WithMain Vulkan::Vulkan)

include(CTest)
include(Catch)
catch_discover_tests(unit_tests)