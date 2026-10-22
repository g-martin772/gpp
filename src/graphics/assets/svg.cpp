module;
#include <lunasvg.h>
module GPP.Graphics;

import :Assets.Svg;
import std;

namespace GPP
{
    namespace
    {
        std::expected<RasterImage, std::string> Render(const lunasvg::Document* document, const int maxWidth,
                                                        const int maxHeight)
        {
            if (!document) return std::unexpected("invalid SVG document");
            const float sourceWidth = document->width();
            const float sourceHeight = document->height();
            if (sourceWidth <= 0.0f || sourceHeight <= 0.0f) return std::unexpected("SVG has no size");

            const float scale = std::min(static_cast<float>(maxWidth) / sourceWidth,
                                         static_cast<float>(maxHeight) / sourceHeight);
            const int width = std::max(1, static_cast<int>(std::lround(sourceWidth * scale)));
            const int height = std::max(1, static_cast<int>(std::lround(sourceHeight * scale)));

            auto bitmap = document->renderToBitmap(width, height, 0x00000000);
            if (bitmap.isNull()) return std::unexpected("SVG rasterization failed");
            bitmap.convertToRGBA();

            RasterImage image{.Width = width, .Height = height};
            image.Rgba.resize(static_cast<std::size_t>(width) * height * 4);
            for (int y = 0; y < height; ++y)
            {
                const auto* row = bitmap.data() + static_cast<std::size_t>(y) * bitmap.stride();
                std::copy_n(row, static_cast<std::size_t>(width) * 4,
                            image.Rgba.begin() + static_cast<std::ptrdiff_t>(y) * width * 4);
            }
            return image;
        }
    }

    std::expected<RasterImage, std::string> RasterizeSvg(const std::string_view svg, const int maxWidth,
                                                         const int maxHeight)
    {
        if (maxWidth <= 0 || maxHeight <= 0) return std::unexpected("invalid raster size");
        const auto document = lunasvg::Document::loadFromData(svg.data(), svg.size());
        if (!document) return std::unexpected("failed to parse SVG");
        return Render(document.get(), maxWidth, maxHeight);
    }

    std::expected<RasterImage, std::string> RasterizeSvgFile(const std::filesystem::path& path, const int maxWidth,
                                                             const int maxHeight)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) return std::unexpected("cannot open " + path.string());
        std::ostringstream buffer;
        buffer << stream.rdbuf();
        return RasterizeSvg(buffer.str(), maxWidth, maxHeight);
    }
}
