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
find_library(SHADERC_SHARED_LIBRARY NAMES shaderc_shared)

include(FetchContent)
option(GPP_USE_IMGUI_DOCKING "Build ImGui from the official docking branch" ON)
if (GPP_USE_IMGUI_DOCKING)
    FetchContent_Declare(
            imgui_docking
            GIT_REPOSITORY https://github.com/ocornut/imgui.git
            GIT_TAG docking
            GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(imgui_docking)
    add_library(gpp_imgui STATIC
            ${imgui_docking_SOURCE_DIR}/imgui.cpp
            ${imgui_docking_SOURCE_DIR}/imgui_draw.cpp
            ${imgui_docking_SOURCE_DIR}/imgui_tables.cpp
            ${imgui_docking_SOURCE_DIR}/imgui_demo.cpp
            ${imgui_docking_SOURCE_DIR}/imgui_widgets.cpp
            ${imgui_docking_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
            ${imgui_docking_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
    target_include_directories(gpp_imgui PUBLIC
            ${imgui_docking_SOURCE_DIR}
            ${imgui_docking_SOURCE_DIR}/backends)
    set_target_properties(gpp_imgui PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_link_libraries(gpp_imgui PUBLIC SDL3::SDL3 Vulkan::Headers)
else()
    add_library(gpp_imgui ALIAS imgui::imgui)
endif()

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
target_sources(gpp
        PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS ${glm_DIR}../../../include/glm
        FILES ${glm_DIR}/../../include/glm/glm.cppm
)
target_sources(gpp
        PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS ${glm_DIR}../../../include/glm
        FILES ${glm_DIR}/../../include/glm/glm.cppm
)
target_sources(gpp
        PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS ${glm_DIR}../../../include/vulkan
        FILES ${glm_DIR}/../../include/vulkan/vulkan.cppm
)
target_include_directories(gpp PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(gpp PUBLIC spdlog::spdlog fmt::fmt nlohmann_json::nlohmann_json gpp_imgui glm::glm)
target_link_libraries(gpp PUBLIC Vulkan::Vulkan)
target_link_libraries(gpp PRIVATE SDL3::SDL3 Vulkan::Headers GPUOpen::VulkanMemoryAllocator
        spirv-cross-core spirv-cross-glsl)
if (SHADERC_SHARED_LIBRARY)
    target_link_libraries(gpp PUBLIC ${SHADERC_SHARED_LIBRARY})
else()
    target_link_libraries(gpp PUBLIC Vulkan::shaderc_combined)
endif()

include(cmake/GppHotReload.cmake)

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

gpp_add_hot_reload_layer(demo_hot_reload_layer
        SOURCES tests/scratch/plugins/demo_hot_reload_layer.cpp
        OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:scratch>")

gpp_add_hot_reload_theme(demo_theme
        SOURCES tests/scratch/plugins/demo_theme.cpp
        OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:scratch>")

add_dependencies(scratch demo_hot_reload_layer demo_theme)

# ------ tests -------

add_executable(unit_tests)
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "tests/unit/**/*.cpp")
target_sources(unit_tests
        PRIVATE
        ${TEST_SOURCES})
target_link_libraries(unit_tests PRIVATE gpp)
target_link_libraries(unit_tests PRIVATE Catch2::Catch2WithMain Vulkan::Vulkan)

gpp_add_hot_reload_layer(gpp_test_hot_reload_plugin
        SOURCES tests/fixtures/hotreload/test_hot_reload_plugin.cpp
        OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:unit_tests>")
add_dependencies(unit_tests gpp_test_hot_reload_plugin)

include(CTest)
include(Catch)
catch_discover_tests(unit_tests)