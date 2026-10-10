#include <catch2/catch_test_macros.hpp>

import std;

namespace
{
    namespace fs = std::filesystem;

    std::string ReadFile(const fs::path& path)
    {
        std::ifstream stream(path);
        REQUIRE(stream.good());
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }

    std::vector<std::string> RegisteredNames(const std::string& source, const std::string& table)
    {
        std::vector<std::string> names;
        const std::regex pattern("vm\\.Register\\(\"" + table + "\"\\s*,\\s*\"([A-Za-z_0-9]+)\"");
        for (std::sregex_iterator it(source.begin(), source.end(), pattern), end; it != end; ++it) names.push_back((*it)[1]);
        return names;
    }

    bool Declares(const std::string& definitions, const std::string& table, const std::string& name)
    {
        const auto start = definitions.find("declare " + table + ": {");
        if (start == std::string::npos) return false;
        const auto close = definitions.find("\n}", start);
        const std::regex entry("\\n\\s+" + name + ":");
        const std::string body = definitions.substr(start, close - start);
        return std::regex_search(body, entry);
    }
}

TEST_CASE("Luau definitions declare every registered scene native", "[scripting][definitions]")
{
    const fs::path root = GPP_SOURCE_DIR;
    const auto definitions = ReadFile(root / "scripts" / "gpp.d.luau");
    const auto bindings = ReadFile(root / "src" / "scripting" / "luau_bindings.cpp");

    const auto sceneNames = RegisteredNames(bindings, "scene");
    REQUIRE(sceneNames.size() >= 7);
    for (const auto& name : sceneNames)
    {
        INFO("scene." + name);
        CHECK(Declares(definitions, "scene", name));
    }

    CHECK(Declares(definitions, "gpp", "ease"));
    CHECK(Declares(definitions, "gpp", "random"));
    for (const char* global : {"vec2", "vec4", "ease"}) CHECK(definitions.contains(std::string("declare ") + global + ":"));
}
