#include <catch2/catch_test_macros.hpp>

import GPP;
import std;
import vulkan;

using namespace GPP;

namespace
{
    class TempFile
    {
    public:
        explicit TempFile(const std::string& name)
            : m_Path(std::filesystem::temp_directory_path() / ("gpp_texture_test_" + name)) {}
        ~TempFile() { std::error_code ec; std::filesystem::remove(m_Path, ec); }
        [[nodiscard]] const std::filesystem::path& Path() const { return m_Path; }

    private:
        std::filesystem::path m_Path;
    };

    TempFile WriteRgba(const std::string& name, const int w, const int h, const std::vector<std::uint8_t>& rgba)
    {
        TempFile file(name);
        REQUIRE(stbi_write_png(file.Path().string().c_str(), w, h, 4, rgba.data(), w * 4) != 0);
        return file;
    }

    std::vector<std::uint8_t> BlackWhite()
    {
        return {0, 0, 0, 255, 255, 255, 255, 255};
    }
}

TEST_CASE("Texture roles pick the GPU format", "[texture]")
{
    CHECK(TextureFormat(TextureRole::Color) == vk::Format::eR8G8B8A8Srgb);
    CHECK(TextureFormat(TextureRole::Data) == vk::Format::eR8G8B8A8Unorm);
    CHECK(TextureFormat(TextureRole::Height) == vk::Format::eR16Unorm);
    CHECK(TextureFormat(TextureRole::Hdr) == vk::Format::eR16G16B16A16Sfloat);
}

TEST_CASE("DecodeTexture averages black and white in linear light for Color", "[texture]")
{
    const auto file = WriteRgba("color.png", 2, 1, BlackWhite());
    const auto texture = DecodeTexture(file.Path(), TextureRole::Color, 0);
    REQUIRE(texture.has_value());
    REQUIRE(texture->Levels.size() == 2);
    CHECK(texture->Format == vk::Format::eR8G8B8A8Srgb);
    CHECK(texture->Levels[0].size() == 8);
    CHECK(static_cast<int>(texture->Levels[1][0]) == 188);
    CHECK(static_cast<int>(texture->Levels[1][3]) == 255);
}

TEST_CASE("DecodeTexture averages the stored values for Data", "[texture]")
{
    const auto file = WriteRgba("data.png", 2, 1, BlackWhite());
    const auto texture = DecodeTexture(file.Path(), TextureRole::Data, 0);
    REQUIRE(texture.has_value());
    REQUIRE(texture->Levels.size() == 2);
    CHECK(static_cast<int>(texture->Levels[1][0]) == 128);
}

TEST_CASE("DecodeTexture widens 8-bit sources for Height", "[texture]")
{
    const auto file = WriteRgba("height.png", 2, 1, BlackWhite());
    const auto texture = DecodeTexture(file.Path(), TextureRole::Height, 0);
    REQUIRE(texture.has_value());
    REQUIRE(texture->Levels.size() == 2);
    REQUIRE(texture->Levels[0].size() == 4);
    std::array<std::uint16_t, 2> top{};
    std::memcpy(top.data(), texture->Levels[0].data(), 4);
    CHECK(top[0] == 0);
    CHECK(top[1] == 65535);
    std::uint16_t mean = 0;
    std::memcpy(&mean, texture->Levels[1].data(), 2);
    CHECK(mean == 32768);
}

TEST_CASE("DecodeTexture drops the top levels above MaxSize and keeps the source size", "[texture]")
{
    const auto file = WriteRgba("max.png", 8, 4, std::vector<std::uint8_t>(8 * 4 * 4, 200));
    const auto full = DecodeTexture(file.Path(), TextureRole::Data, 0);
    REQUIRE(full.has_value());
    CHECK(full->Levels.size() == 4);
    CHECK(full->Width == 8);

    const auto small = DecodeTexture(file.Path(), TextureRole::Data, 2);
    REQUIRE(small.has_value());
    CHECK(small->Width == 2);
    CHECK(small->Height == 1);
    CHECK(small->SourceWidth == 8);
    CHECK(small->SourceHeight == 4);
    CHECK(small->Levels.size() == 2);
    CHECK(small->Levels[0].size() == 2u * 1u * 4u);
}

TEST_CASE("DecodeTexture folds an odd last column into its neighbour", "[texture]")
{
    std::vector<std::uint8_t> rgba;
    for (const std::uint8_t v : {0, 0, 90}) rgba.insert(rgba.end(), {v, v, v, 255});
    const auto file = WriteRgba("odd.png", 3, 1, rgba);
    const auto texture = DecodeTexture(file.Path(), TextureRole::Data, 0);
    REQUIRE(texture.has_value());
    REQUIRE(texture->Levels.size() == 2);
    CHECK(static_cast<int>(texture->Levels[1][0]) == 30);
}

TEST_CASE("DecodeTexture converts HDR to half floats", "[texture]")
{
    const auto file = WriteRgba("hdr.png", 1, 1, {255, 255, 255, 255});
    const auto texture = DecodeTexture(file.Path(), TextureRole::Hdr, 0);
    REQUIRE(texture.has_value());
    REQUIRE(texture->Levels.size() == 1);
    REQUIRE(texture->Levels[0].size() == 8);
    std::uint16_t red = 0;
    std::memcpy(&red, texture->Levels[0].data(), 2);
    CHECK(red == 0x3c00);
}

TEST_CASE("DecodeTexture reports a missing or broken file", "[texture]")
{
    const auto missing = DecodeTexture("/nonexistent/gpp_texture_test.png", TextureRole::Color, 0);
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error() == "file not found");

    TempFile broken("broken.png");
    std::ofstream(broken.Path()) << "not an image";
    CHECK_FALSE(DecodeTexture(broken.Path(), TextureRole::Color, 0).has_value());
}
