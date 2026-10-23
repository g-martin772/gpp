set_property(GLOBAL PROPERTY GPP_HOT_RELOAD_INTERFACE_SOURCES "")

# Plugins carry a build id (compiler, standard library, build type, hash of the interface sources they import)
# and the host refuses plugins whose id differs. Apps register their own module interface files here.
function(gpp_hot_reload_track_sources)
    set_property(GLOBAL APPEND PROPERTY GPP_HOT_RELOAD_INTERFACE_SOURCES ${ARGN})
endfunction()

function(_gpp_write_hot_reload_source_list)
    set(GPP_ROOT "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/..")
    get_property(LIST_FILE GLOBAL PROPERTY GPP_HOT_RELOAD_SOURCE_LIST_FILE)
    file(GLOB_RECURSE gpp_tracked CONFIGURE_DEPENDS "${GPP_ROOT}/src/*.cppm" "${GPP_ROOT}/include/gpp/*.h")
    get_property(app_tracked GLOBAL PROPERTY GPP_HOT_RELOAD_INTERFACE_SOURCES)
    list(REMOVE_DUPLICATES app_tracked)
    list(JOIN gpp_tracked "\n" content)
    if (app_tracked)
        list(JOIN app_tracked "\n" app_content)
        string(APPEND content "\n" "${app_content}")
    endif ()
    file(WRITE "${LIST_FILE}" "${content}\n")
endfunction()

function(_gpp_ensure_build_id_target)
    if (TARGET gpp_hot_reload_build_id)
        return()
    endif ()
    set(GPP_ROOT "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/..")
    execute_process(COMMAND ${CMAKE_CXX_COMPILER} -dM -E -x c++ -include version /dev/null
            OUTPUT_VARIABLE predefined ERROR_QUIET)
    set(stdlib "unknown-stdlib")
    foreach (macro __GLIBCXX__ _LIBCPP_VERSION)
        if (predefined MATCHES "#define ${macro} ([^\n]+)")
            set(stdlib "${macro}=${CMAKE_MATCH_1}")
        endif ()
    endforeach ()
    set(dir "${CMAKE_BINARY_DIR}/gpp_generated")
    set(list_file "${dir}/hot_reload_sources.txt")
    file(MAKE_DIRECTORY "${dir}")
    add_custom_target(gpp_hot_reload_build_id
            COMMAND ${CMAKE_COMMAND}
            "-DLIST_FILE=${list_file}"
            "-DOUTPUT=${dir}/gpp_hot_reload_generated.h"
            "-DPREFIX=${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}/${stdlib}/${CMAKE_BUILD_TYPE}"
            -P "${GPP_ROOT}/cmake/GppBuildId.cmake"
            BYPRODUCTS "${dir}/gpp_hot_reload_generated.h"
            COMMENT "Hot-reload build id"
            VERBATIM)
    set_property(GLOBAL PROPERTY GPP_HOT_RELOAD_SOURCE_LIST_FILE "${list_file}")
    cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL _gpp_write_hot_reload_source_list)
endfunction()

# Executables that embed the host build id (GPP_HOT_RELOAD_BUILD_ID) and plugins use this.
function(gpp_use_hot_reload_build_id TARGET)
    _gpp_ensure_build_id_target()
    add_dependencies(${TARGET} gpp_hot_reload_build_id)
    target_include_directories(${TARGET} PRIVATE "${CMAKE_BINARY_DIR}/gpp_generated")
endfunction()

# Shared implementation for gpp_add_hot_reload_layer() and gpp_add_hot_reload_theme() below --
# both just build a PIC MODULE library linked against gpp; the only real difference between a
# "layer" and a "theme" plugin is which GPP_DEFINE_HOT_RELOAD_* macro its source file uses.
function(_gpp_add_hot_reload_module NAME)
    set(oneValueArgs OUTPUT_DIRECTORY)
    set(multiValueArgs SOURCES LINK_LIBRARIES)
    cmake_parse_arguments(GPP_MODULE "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (NOT GPP_MODULE_SOURCES)
        message(FATAL_ERROR "${NAME}: SOURCES is required.")
    endif ()

    add_library(${NAME} MODULE ${GPP_MODULE_SOURCES})
    set_target_properties(${NAME} PROPERTIES
            PREFIX ""
            CXX_STANDARD 23
            CXX_STANDARD_REQUIRED ON
            POSITION_INDEPENDENT_CODE ON)
    target_link_libraries(${NAME} PRIVATE gpp ${GPP_MODULE_LINK_LIBRARIES})
    gpp_use_hot_reload_build_id(${NAME})

    if (GPP_MODULE_OUTPUT_DIRECTORY)
        set_target_properties(${NAME} PROPERTIES
                LIBRARY_OUTPUT_DIRECTORY "${GPP_MODULE_OUTPUT_DIRECTORY}"
                RUNTIME_OUTPUT_DIRECTORY "${GPP_MODULE_OUTPUT_DIRECTORY}")
    endif ()
endfunction()

# gpp_add_hot_reload_layer(<name>
#     SOURCES <src1> [<src2> ...]
#     [OUTPUT_DIRECTORY <dir>]
#     [LINK_LIBRARIES <lib1> ...]
# )
#
# Builds a hot-reloadable GuiLayer plugin (.so). The source must define its layer with
# GPP_DEFINE_HOT_RELOAD_LAYER(YourLayer), and the resulting library is pointed at by
# GuiApplicationBuilder::AddHotReloadableLayer(id, path).
function(gpp_add_hot_reload_layer NAME)
    _gpp_add_hot_reload_module(${NAME} ${ARGN})
endfunction()

# gpp_add_hot_reload_theme(<name>
#     SOURCES <src1> [<src2> ...]
#     [OUTPUT_DIRECTORY <dir>]
#     [LINK_LIBRARIES <lib1> ...]
# )
#
# Builds a hot-reloadable Theme plugin (.so). The source must define its theme with
# GPP_DEFINE_HOT_RELOAD_THEME(YourTheme), and the resulting library is pointed at by
# GuiApplicationBuilder::SetTheme(path, enableHotReload) or "GPP:Graphics:Theme:LibraryPath"
# in config.json.
function(gpp_add_hot_reload_theme NAME)
    _gpp_add_hot_reload_module(${NAME} ${ARGN})
endfunction()
