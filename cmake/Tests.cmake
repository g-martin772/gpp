add_executable(unit_tests)
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "tests/unit/**/*.cpp")
target_sources(unit_tests
        PRIVATE
        ${TEST_SOURCES})
target_link_libraries(unit_tests PRIVATE gpp)
target_compile_definitions(unit_tests PRIVATE GPP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(unit_tests PRIVATE Catch2::Catch2WithMain Vulkan::Vulkan)

gpp_add_hot_reload_layer(gpp_test_hot_reload_plugin
        SOURCES tests/fixtures/hotreload/test_hot_reload_plugin.cpp
        OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:unit_tests>")
add_dependencies(unit_tests gpp_test_hot_reload_plugin)

include(CTest)
include(Catch)

if(CMAKE_CXX_FLAGS MATCHES "fsanitize=[a-z,]*address")
    catch_discover_tests(unit_tests PROPERTIES
            ENVIRONMENT "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/tests/lsan.supp"
            SKIP_RETURN_CODE 4)
elseif(CMAKE_CXX_FLAGS MATCHES "fsanitize=thread")
    catch_discover_tests(unit_tests TEST_SPEC "~[gpu]" PROPERTIES
            ENVIRONMENT "TSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/tests/tsan.supp"
            SKIP_RETURN_CODE 4)
else()
    catch_discover_tests(unit_tests)
endif()
