set(Vulkan_ROOT "/usr")
set(VULKAN_SDK "/usr")

set(Vulkan_INCLUDE_DIR "/usr/include" CACHE PATH "" FORCE)
set(Vulkan_LIBRARY "/usr/lib/libvulkan.so" CACHE FILEPATH "" FORCE)

# vcpkg

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

find_package(Stb REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(unofficial-omniverse-physx-sdk CONFIG REQUIRED)

# imgui

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

    # gpp patch: imgui-node-editor (below) was written against a pre-1.90 ImGui and calls
    # the long-removed ImRect::Floor(); add it back rather than pin ImGui to an old commit.
    set(GPP_IMRECT_HEADER "${imgui_docking_SOURCE_DIR}/imgui_internal.h")
    file(READ "${GPP_IMRECT_HEADER}" GPP_IMGUI_INTERNAL_H)
    if (NOT GPP_IMGUI_INTERNAL_H MATCHES "void[ \t]+Floor\\(\\)")
        string(REPLACE
                "const ImVec4& AsVec4() const                    { return *(const ImVec4*)&Min.x; }"
                "const ImVec4& AsVec4() const                    { return *(const ImVec4*)&Min.x; }\n    void        Floor()                             { Min.x = IM_TRUNC(Min.x); Min.y = IM_TRUNC(Min.y); Max.x = IM_TRUNC(Max.x); Max.y = IM_TRUNC(Max.y); }"
                GPP_IMGUI_INTERNAL_H "${GPP_IMGUI_INTERNAL_H}")
        file(WRITE "${GPP_IMRECT_HEADER}" "${GPP_IMGUI_INTERNAL_H}")
    endif ()
else()
    add_library(gpp_imgui ALIAS imgui::imgui)
endif()

FetchContent_Declare(
        imguizmo
        GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
        GIT_TAG master
        GIT_SHALLOW TRUE)
FetchContent_GetProperties(imguizmo)
if (NOT imguizmo_POPULATED)
    cmake_policy(PUSH)
    cmake_policy(SET CMP0169 OLD)
    FetchContent_Populate(imguizmo)
    cmake_policy(POP)
endif ()
add_library(gpp_imguizmo STATIC
        ${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp)
target_compile_definitions(gpp_imguizmo PRIVATE IMGUI_DEFINE_MATH_OPERATORS)
target_include_directories(gpp_imguizmo PUBLIC ${imguizmo_SOURCE_DIR}/src)
set_target_properties(gpp_imguizmo PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(gpp_imguizmo PUBLIC gpp_imgui)

FetchContent_Declare(
        node_editor
        GIT_REPOSITORY https://github.com/g-martin772/imgui-node-editor.git
        GIT_TAG master
        GIT_SHALLOW TRUE)

FetchContent_GetProperties(node_editor)
if (NOT node_editor_POPULATED)
    cmake_policy(PUSH)
    cmake_policy(SET CMP0169 OLD)
    FetchContent_Populate(node_editor)
    cmake_policy(POP)
endif ()

# gpp patch: imgui_extra_math.inl unconditionally redefines operator*(float, ImVec2),
# which ImGui itself now also defines under IMGUI_DEFINE_MATH_OPERATORS -> redefinition.
set(GPP_NODE_EDITOR_MATH_INL "${node_editor_SOURCE_DIR}/imgui_extra_math.inl")
file(READ "${GPP_NODE_EDITOR_MATH_INL}" GPP_NODE_EDITOR_MATH_CONTENT)
if (NOT GPP_NODE_EDITOR_MATH_CONTENT MATCHES "ifndef IMGUI_DEFINE_MATH_OPERATORS_IMPLEMENTED")
    string(REPLACE
            "inline ImVec2 operator*(const float lhs, const ImVec2& rhs)"
            "#ifndef IMGUI_DEFINE_MATH_OPERATORS_IMPLEMENTED\ninline ImVec2 operator*(const float lhs, const ImVec2& rhs)"
            GPP_NODE_EDITOR_MATH_CONTENT "${GPP_NODE_EDITOR_MATH_CONTENT}")
    string(REPLACE
            "    return ImVec2(lhs * rhs.x, lhs * rhs.y);\n}"
            "    return ImVec2(lhs * rhs.x, lhs * rhs.y);\n}\n#endif"
            GPP_NODE_EDITOR_MATH_CONTENT "${GPP_NODE_EDITOR_MATH_CONTENT}")
    file(WRITE "${GPP_NODE_EDITOR_MATH_INL}" "${GPP_NODE_EDITOR_MATH_CONTENT}")
endif ()

file(GLOB GPP_NODE_EDITOR_SOURCES CONFIGURE_DEPENDS "${node_editor_SOURCE_DIR}/*.cpp")
add_library(gpp_node_editor STATIC ${GPP_NODE_EDITOR_SOURCES})
target_include_directories(gpp_node_editor PUBLIC ${node_editor_SOURCE_DIR})
set_target_properties(gpp_node_editor PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(gpp_node_editor PUBLIC gpp_imgui)
