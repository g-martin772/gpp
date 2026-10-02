#include <catch2/catch_test_macros.hpp>

import GPP;
import std;

using namespace GPP;

namespace
{
    std::filesystem::path TestPluginPath()
    {
        FileSystem fs;
        return fs.GetBinaryDirectory() / "gpp_test_hot_reload_plugin.so";
    }
}

TEST_CASE("DynamicLibrary loads a shared library and resolves symbols", "[hotreload][dynamic_library]")
{
    DynamicLibrary library(TestPluginPath());
    CHECK(library.IsLoaded());
    CHECK(library.GetPath() == TestPluginPath());

    const auto abiVersionFn = library.GetSymbol<std::uint32_t (*)()>("GPP_HotReloadModuleAbiVersion");
    REQUIRE(abiVersionFn != nullptr);
    CHECK(abiVersionFn() == kHotReloadAbiVersion);

    const auto missing = library.GetSymbol<void (*)()>("ThisSymbolDoesNotExist");
    CHECK(missing == nullptr);
}

TEST_CASE("DynamicLibrary throws a descriptive error for a missing file", "[hotreload][dynamic_library]")
{
    const auto missingPath = std::filesystem::temp_directory_path() / "gpp_does_not_exist.so";
    CHECK_THROWS_AS(DynamicLibrary(missingPath), std::runtime_error);
}

TEST_CASE("DynamicLibrary is move-only and unloads exactly once", "[hotreload][dynamic_library]")
{
    DynamicLibrary library(TestPluginPath());
    CHECK(library.IsLoaded());

    DynamicLibrary moved(std::move(library));
    CHECK(moved.IsLoaded());
    CHECK_FALSE(library.IsLoaded()); // NOLINT(bugprone-use-after-move) -- checking the moved-from state

    moved.Unload();
    CHECK_FALSE(moved.IsLoaded());
    moved.Unload(); // must be a safe no-op when called again
}

TEST_CASE("DynamicLibrary default construction leaves it unloaded", "[hotreload][dynamic_library]")
{
    DynamicLibrary library;
    CHECK_FALSE(library.IsLoaded());
    CHECK(library.GetSymbolRaw("anything") == nullptr);
}
