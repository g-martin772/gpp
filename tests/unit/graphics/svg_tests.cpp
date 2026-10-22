#include <catch2/catch_test_macros.hpp>

import GPP;
import std;

using namespace GPP;

namespace
{
    constexpr std::string_view kRect =
        R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 20 10"><rect x="5" y="2" width="10" height="6" fill="#ff0000"/></svg>)";

    std::span<const std::uint8_t, 4> Pixel(const RasterImage& image, const int x, const int y)
    {
        return std::span<const std::uint8_t, 4>(image.Rgba.data() + (static_cast<std::size_t>(y) * image.Width + x) * 4, 4);
    }
}

TEST_CASE("RasterizeSvg fits the box keeping aspect and leaves the background transparent", "[svg]")
{
    const auto result = RasterizeSvg(kRect, 80, 80);
    REQUIRE(result.has_value());
    CHECK(result->Width == 80);
    CHECK(result->Height == 40);
    CHECK(result->Rgba.size() == 80u * 40u * 4u);

    CHECK(Pixel(*result, 1, 1)[3] == 0);
    const auto middle = Pixel(*result, 40, 20);
    CHECK(middle[0] == 255);
    CHECK(middle[1] == 0);
    CHECK(middle[2] == 0);
    CHECK(middle[3] == 255);
}

TEST_CASE("RasterizeSvg returns non-premultiplied colour", "[svg]")
{
    constexpr std::string_view svg =
        R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 4 4"><rect width="4" height="4" fill="#ff0000" fill-opacity="0.5"/></svg>)";
    const auto result = RasterizeSvg(svg, 8, 8);
    REQUIRE(result.has_value());
    const auto pixel = Pixel(*result, 4, 4);
    CHECK(pixel[0] >= 250);
    CHECK(pixel[3] >= 126);
    CHECK(pixel[3] <= 130);
}

TEST_CASE("RasterizeSvg reports invalid input", "[svg]")
{
    CHECK_FALSE(RasterizeSvg("not an svg", 16, 16).has_value());
    CHECK_FALSE(RasterizeSvg(kRect, 0, 16).has_value());
    CHECK_FALSE(RasterizeSvgFile("/nonexistent/icon.svg", 16, 16).has_value());
}
