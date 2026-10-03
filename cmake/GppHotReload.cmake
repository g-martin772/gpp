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
