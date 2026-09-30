#include <catch2/catch_test_macros.hpp>

import GPP;
import std;

using namespace GPP;

namespace
{
    std::filesystem::path MakeShaderDirectory()
    {
        const auto path = std::filesystem::temp_directory_path() /
            ("gpp_shader_include_" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path);
        return path;
    }
}

TEST_CASE("Shader compiler resolves standard GLSL includes", "[graphics][shader]")
{
    const auto directory = MakeShaderDirectory();
    const auto includePath = directory / "common.glsl";
    const auto sourcePath = directory / "main.vert";

    std::ofstream(includePath) << "vec4 MakePosition(vec3 position) { return vec4(position, 1.0); }\n";
    std::ofstream(sourcePath) << R"(#version 450
#include "common.glsl"
layout(location = 0) in vec3 position;
void main() { gl_Position = MakePosition(position); }
)";

    auto fileSystem = std::make_shared<FileSystem>();
    auto logger = std::make_shared<Logger>();
    ShaderCompiler compiler(fileSystem, logger);
    ShaderCompileOptions options;
    options.enableCache = false;
    options.includeDirectories.push_back(directory);

    const auto compiled = compiler.Compile(
        ShaderSource{.path = sourcePath, .stage = ShaderStage::Vertex}, options);

    REQUIRE_FALSE(compiled.spirv.empty());
    REQUIRE(compiled.dependencies.size() == 1);
    CHECK(compiled.dependencies.front().lexically_normal() == includePath.lexically_normal());

    std::filesystem::remove_all(directory);
}

TEST_CASE("Shader compiler reports missing includes", "[graphics][shader]")
{
    const auto directory = MakeShaderDirectory();
    const auto sourcePath = directory / "main.vert";
    std::ofstream(sourcePath) << R"(#version 450
#include "missing.glsl"
void main() { gl_Position = vec4(0.0); }
)";

    auto fileSystem = std::make_shared<FileSystem>();
    auto logger = std::make_shared<Logger>();
    ShaderCompiler compiler(fileSystem, logger);
    ShaderCompileOptions options;
    options.enableCache = false;
    options.includeDirectories.push_back(directory);

    CHECK_THROWS(compiler.Compile(
        ShaderSource{.path = sourcePath, .stage = ShaderStage::Vertex}, options));

    std::filesystem::remove_all(directory);
}

TEST_CASE("Shader cache invalidates when an include changes", "[graphics][shader]")
{
    const auto directory = MakeShaderDirectory();
    const auto includePath = directory / "common.glsl";
    const auto sourcePath = directory / "main.vert";
    std::ofstream(sourcePath) << R"(#version 450
#include "common.glsl"
layout(location = 0) in vec3 position;
void main() { gl_Position = MakePosition(position); }
)";

    std::ofstream(includePath) << "vec4 MakePosition(vec3 position) { return vec4(position, 1.0); }\n";
    auto fileSystem = std::make_shared<FileSystem>();
    auto logger = std::make_shared<Logger>();
    ShaderCompiler compiler(fileSystem, logger);
    ShaderCompileOptions options;
    options.cacheDirectory = directory / "cache";
    options.includeDirectories.push_back(directory);

    const auto first = compiler.Compile(
        ShaderSource{.path = sourcePath, .stage = ShaderStage::Vertex}, options);
    std::ofstream(includePath) << "vec4 MakePosition(vec3 position) { return vec4(position * 2.0, 1.0); }\n";
    const auto second = compiler.Compile(
        ShaderSource{.path = sourcePath, .stage = ShaderStage::Vertex}, options);

    REQUIRE_FALSE(first.spirv.empty());
    REQUIRE_FALSE(second.spirv.empty());
    CHECK(first.spirv != second.spirv);

    std::filesystem::remove_all(directory);
}
