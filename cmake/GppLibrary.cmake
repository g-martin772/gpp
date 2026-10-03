set(CMAKE_CXX_MODULE_STD 1)
set(CMAKE_CXX_SCAN_FOR_MODULES 1)

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

target_include_directories(gpp PRIVATE ${Stb_INCLUDE_DIR})
target_link_libraries(gpp PUBLIC yaml-cpp::yaml-cpp gpp_imguizmo gpp_node_editor)
target_link_libraries(gpp PUBLIC unofficial::omniverse-physx-sdk::sdk EnTT::EnTT)

include(cmake/GppHotReload.cmake)
