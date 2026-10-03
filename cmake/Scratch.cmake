add_executable(scratch)
target_sources(scratch
        PRIVATE
        tests/scratch/main.cpp)
target_link_libraries(scratch PRIVATE gpp)

gpp_add_hot_reload_layer(demo_hot_reload_layer
        SOURCES tests/scratch/plugins/demo_hot_reload_layer.cpp
        OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:scratch>")

gpp_add_hot_reload_theme(demo_theme
        SOURCES tests/scratch/plugins/demo_theme.cpp
        OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:scratch>")

add_dependencies(scratch demo_hot_reload_layer demo_theme)
