module GPP.Graphics;

import :FontAssets;
import std;

namespace GPP
{
    FontAssetCatalog::FontAssetCatalog(std::shared_ptr<IFileSystem> fileSystem,
                                       std::shared_ptr<FontOptions> options,
                                       std::shared_ptr<Logger> logger)
        : m_FileSystem(std::move(fileSystem)), m_Options(std::move(options)), m_Logger(std::move(logger))
    {
    }

    void FontAssetCatalog::ScanDirectory(const std::filesystem::path& directory)
    {
        const auto resolved = m_FileSystem->ResolvePath(directory);
        std::error_code error;
        if (!std::filesystem::exists(resolved, error) || !std::filesystem::is_directory(resolved, error))
        {
            return;
        }

        for (const auto& entry : std::filesystem::directory_iterator(resolved, error))
        {
            if (!entry.is_regular_file())
            {
                continue;
            }
            auto extension = entry.path().extension().string();
            std::ranges::transform(extension, extension.begin(),
                                   [](unsigned char c) { return std::tolower(c); });
            if (extension != ".ttf" && extension != ".otf" && extension != ".ttc")
            {
                continue;
            }

            m_Fonts.push_back(FontAssetMetadata{
                .Name = entry.path().stem().string(),
                .Path = entry.path()
            });
        }
    }

    Task<void> FontAssetCatalog::StartAsync(std::stop_token stopToken)
    {
        std::scoped_lock lock(m_Mutex);
        m_Fonts.clear();
        for (const auto& directory : m_Options->FontDirectories)
        {
            ScanDirectory(directory);
        }
        m_Logger->Info("FontAssetCatalog discovered {} font(s).", m_Fonts.size());
        co_return;
    }

    Task<void> FontAssetCatalog::StopAsync()
    {
        co_return;
    }

    std::vector<FontAssetMetadata> FontAssetCatalog::GetAvailableFonts() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_Fonts;
    }

    std::optional<std::filesystem::path> FontAssetCatalog::ResolveFont(const std::string& name) const
    {
        std::scoped_lock lock(m_Mutex);
        for (const auto& font : m_Fonts)
        {
            if (font.Name == name)
            {
                return font.Path;
            }
        }
        return std::nullopt;
    }
}
