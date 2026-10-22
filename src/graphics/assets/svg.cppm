export module GPP.Graphics:Assets.Svg;

import std;

namespace GPP
{
    export struct RasterImage
    {
        int Width = 0;
        int Height = 0;
        std::vector<std::uint8_t> Rgba; // non-premultiplied RGBA8, row 0 on top
    };

    export [[nodiscard]] std::expected<RasterImage, std::string> RasterizeSvg(std::string_view svg, int maxWidth,
                                                                              int maxHeight);
    export [[nodiscard]] std::expected<RasterImage, std::string> RasterizeSvgFile(
        const std::filesystem::path& path, int maxWidth, int maxHeight);
}
