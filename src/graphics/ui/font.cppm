export module GPP.Graphics:FontAssets;

import std;
import GPP.Core;
import :RenderConfig;

namespace GPP
{
    export struct FontAssetMetadata
    {
        std::string Name;
        std::filesystem::path Path;
    };

    // Discovers font files (.ttf/.otf/.ttc)
    export class FontAssetCatalog : public IHostedService
    {
    public:
        using Dependencies = std::tuple<IFileSystem, FontOptions, Logger>;

        FontAssetCatalog(std::shared_ptr<IFileSystem> fileSystem,
                         std::shared_ptr<FontOptions> options,
                         std::shared_ptr<Logger> logger);

        Task<void> StartAsync(std::stop_token stopToken) override;
        Task<void> StopAsync() override;

        [[nodiscard]] std::vector<FontAssetMetadata> GetAvailableFonts() const;
        [[nodiscard]] std::optional<std::filesystem::path> ResolveFont(const std::string& name) const;

    private:
        void ScanDirectory(const std::filesystem::path& directory);

        std::shared_ptr<IFileSystem> m_FileSystem;
        std::shared_ptr<FontOptions> m_Options;
        std::shared_ptr<Logger> m_Logger;
        mutable std::mutex m_Mutex;
        std::vector<FontAssetMetadata> m_Fonts;
    };
}
