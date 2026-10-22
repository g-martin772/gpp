export module GPP.Core:IO.AppPaths;

import std;
import :DI.Service;
import :IO.File;
import :Application.Info;

namespace GPP
{
    export using EnvLookup = std::function<std::optional<std::string>(std::string_view)>;

    export struct XdgDirs
    {
        std::filesystem::path Home;
        std::filesystem::path ConfigHome;
        std::filesystem::path StateHome;
        std::filesystem::path CacheHome;
        std::filesystem::path DataHome;
        std::filesystem::path Documents;
    };

    export class AppPaths : public IService
    {
    public:
        enum class Mode
        {
            Dev,
            Installed
        };

        struct Inputs
        {
            std::filesystem::path BinaryDir;
            std::filesystem::path WorkingDir;
            AppInfo Info;
            EnvLookup Env;
        };

        using Dependencies = std::tuple<IFileSystem, AppInfo>;

        AppPaths(std::shared_ptr<IFileSystem> fileSystem, std::shared_ptr<AppInfo> info);

        static AppPaths Resolve(Inputs inputs);
        static Mode DetectMode(const std::filesystem::path& binaryDir, std::string_view appId, const EnvLookup& env);
        static XdgDirs ResolveXdgDirs(const EnvLookup& env);
        static std::optional<std::filesystem::path> ParseUserDir(
            std::string_view userDirsContent, std::string_view key, const std::filesystem::path& home);
        static EnvLookup ProcessEnv();

        [[nodiscard]] Mode GetMode() const noexcept { return m_Mode; }
        [[nodiscard]] bool IsInstalled() const noexcept { return m_Mode == Mode::Installed; }
        [[nodiscard]] const std::filesystem::path& ResourceDir() const noexcept { return m_ResourceDir; }
        [[nodiscard]] const std::filesystem::path& ConfigDir() const noexcept { return m_ConfigDir; }
        [[nodiscard]] const std::filesystem::path& StateDir() const noexcept { return m_StateDir; }
        [[nodiscard]] const std::filesystem::path& CacheDir() const noexcept { return m_CacheDir; }
        [[nodiscard]] const std::filesystem::path& UserDataDir() const noexcept { return m_UserDataDir; }
        [[nodiscard]] std::filesystem::path UserSubdir(std::string_view name) const;
        [[nodiscard]] std::filesystem::path TempDir() const;

    private:
        AppPaths() = default;

        Mode m_Mode{Mode::Dev};
        std::filesystem::path m_ResourceDir;
        std::filesystem::path m_ConfigDir;
        std::filesystem::path m_StateDir;
        std::filesystem::path m_CacheDir;
        std::filesystem::path m_UserDataDir;
    };
}
