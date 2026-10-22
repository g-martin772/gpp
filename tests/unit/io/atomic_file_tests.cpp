#include <catch2/catch_test_macros.hpp>

import GPP;
import std;

using namespace GPP;
namespace fs = std::filesystem;

namespace
{
    struct TempDir
    {
        fs::path Root = fs::temp_directory_path() / std::format("gpp_atomic_{}_{}", CurrentProcessId(), std::chrono::steady_clock::now().time_since_epoch().count());
        TempDir() { fs::create_directories(Root); }
        ~TempDir()
        {
            std::error_code ec;
            fs::remove_all(Root, ec);
        }
    };

    std::string Slurp(const fs::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }
}

TEST_CASE("WriteFileAtomic creates parents, replaces content and leaves no temp files", "[io][atomic]")
{
    TempDir dir;
    const auto file = dir.Root / "sub" / "state.yaml";

    REQUIRE(WriteFileAtomic(file, "first").has_value());
    CHECK(Slurp(file) == "first");
    REQUIRE(WriteFileAtomic(file, "second and longer").has_value());
    CHECK(Slurp(file) == "second and longer");
    REQUIRE(WriteFileAtomic(file, "x").has_value());
    CHECK(Slurp(file) == "x");

    std::size_t entries = 0;
    for ([[maybe_unused]] const auto& entry : fs::directory_iterator(file.parent_path())) ++entries;
    CHECK(entries == 1);
}

TEST_CASE("WriteFileAtomic fails when the target cannot be replaced", "[io][atomic]")
{
    TempDir dir;
    fs::create_directories(dir.Root / "target");
    fs::create_directories(dir.Root / "target" / "child");
    CHECK_FALSE(WriteFileAtomic(dir.Root / "target", "x").has_value());
    CHECK(fs::is_directory(dir.Root / "target"));
}

TEST_CASE("WriteFileAtomic from many threads always leaves a complete file", "[io][atomic]")
{
    TempDir dir;
    const auto file = dir.Root / "shared.bin";
    std::atomic<int> failures{0};
    std::vector<std::jthread> writers;
    for (int t = 0; t < 8; ++t)
    {
        writers.emplace_back([&, t]
        {
            const std::string content(4096 + static_cast<std::size_t>(t), static_cast<char>('a' + t));
            for (int i = 0; i < 50; ++i) failures += WriteFileAtomic(file, content).has_value() ? 0 : 1;
        });
    }
    writers.clear();
    CHECK(failures.load() == 0);

    const auto result = Slurp(file);
    REQUIRE(result.size() >= 4096);
    CHECK(std::ranges::all_of(result, [&](const char c) { return c == result.front(); }));
    CHECK(result.size() == 4096 + static_cast<std::size_t>(result.front() - 'a'));
}
