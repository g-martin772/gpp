# gpp_add_hot_reload_layer(<name>
#     SOURCES <src1> [<src2> ...]
#     [OUTPUT_DIRECTORY <dir>]
#     [LINK_LIBRARIES <lib1> ...]
# )

function(gpp_add_hot_reload_layer NAME)
    set(oneValueArgs OUTPUT_DIRECTORY)
    set(multiValueArgs SOURCES LINK_LIBRARIES)
    cmake_parse_arguments(GPP_LAYER "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (NOT GPP_LAYER_SOURCES)
        message(FATAL_ERROR "gpp_add_hot_reload_layer(${NAME}): SOURCES is required.")
    endif ()

    add_library(${NAME} MODULE ${GPP_LAYER_SOURCES})
    set_target_properties(${NAME} PROPERTIES
            PREFIX ""
            CXX_STANDARD 23
            CXX_STANDARD_REQUIRED ON
            POSITION_INDEPENDENT_CODE ON)
    target_link_libraries(${NAME} PRIVATE gpp ${GPP_LAYER_LINK_LIBRARIES})

    if (GPP_LAYER_OUTPUT_DIRECTORY)
        set_target_properties(${NAME} PROPERTIES
                LIBRARY_OUTPUT_DIRECTORY "${GPP_LAYER_OUTPUT_DIRECTORY}"
                RUNTIME_OUTPUT_DIRECTORY "${GPP_LAYER_OUTPUT_DIRECTORY}")
    endif ()
endfunction()
