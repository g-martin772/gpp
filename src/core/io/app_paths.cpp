module GPP.Core;

import std;
import :IO.AppPaths;

namespace GPP
{
    namespace
    {
        std::optional<std::filesystem::path> AbsoluteEnvPath(const EnvLookup& env, std::string_view name)
        {
            if (const auto value = env(name); value && !value->empty())
            {
                std::filesystem::path path(*value);
                if (path.is_absolute())
                {
                    return path;
                }
            }
            return std::nullopt;
        }

        std::string_view Trim(std::string_view text)
        {
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
                text.remove_prefix(1);
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
                text.remove_suffix(1);
            return text;
        }
    }

    EnvLookup AppPaths::ProcessEnv()
    {
        return [](std::string_view name) -> std::optional<std::string>
        {
            if (const char* value = std::getenv(std::string(name).c_str()))
            {
                return std::string(value);
            }
            return std::nullopt;
        };
    }

    std::optional<std::filesystem::path> AppPaths::ParseUserDir(
        const std::string_view userDirsContent, const std::string_view key, const std::filesystem::path& home)
    {
        std::istringstream stream{std::string(userDirsContent)};
        std::string line;
        while (std::getline(stream, line))
        {
            auto text = Trim(line);
            if (text.empty() || text.front() == '#' || !text.starts_with(key))
            {
                continue;
            }
            text = Trim(text.substr(key.size()));
            if (text.empty() || text.front() != '=')
            {
                continue;
            }
            text = Trim(text.substr(1));
            if (text.size() >= 2 && text.front() == '"' && text.back() == '"')
            {
                text = text.substr(1, text.size() - 2);
            }
            std::string value(text);
            if (value.starts_with("$HOME"))
            {
                value = home.string() + value.substr(5);
            }
            std::filesystem::path path(value);
            if (!path.is_absolute())
            {
                return std::nullopt;
            }
            path = path.lexically_normal();
            if (path.has_filename() == false)
            {
                path = path.parent_path();
            }
            if (path == home)
            {
                return std::nullopt;
            }
            return path;
        }
        return std::nullopt;
    }

    XdgDirs AppPaths::ResolveXdgDirs(const EnvLookup& env)
    {
        XdgDirs dirs;
        dirs.Home = AbsoluteEnvPath(env, "HOME").value_or(std::filesystem::path("/"));
        dirs.ConfigHome = AbsoluteEnvPath(env, "XDG_CONFIG_HOME").value_or(dirs.Home / ".config");
        dirs.StateHome = AbsoluteEnvPath(env, "XDG_STATE_HOME").value_or(dirs.Home / ".local" / "state");
        dirs.CacheHome = AbsoluteEnvPath(env, "XDG_CACHE_HOME").value_or(dirs.Home / ".cache");
        dirs.DataHome = AbsoluteEnvPath(env, "XDG_DATA_HOME").value_or(dirs.Home / ".local" / "share");

        if (const auto documents = AbsoluteEnvPath(env, "XDG_DOCUMENTS_DIR"))
        {
            dirs.Documents = *documents;
            return dirs;
        }
        std::ifstream file(dirs.ConfigHome / "user-dirs.dirs");
        if (file)
        {
            std::stringstream buffer;
            buffer << file.rdbuf();
            if (const auto parsed = ParseUserDir(buffer.str(), "XDG_DOCUMENTS_DIR", dirs.Home))
            {
                dirs.Documents = *parsed;
                return dirs;
            }
        }
        dirs.Documents = dirs.Home / "Documents";
        return dirs;
    }

    AppPaths::Mode AppPaths::DetectMode(
        const std::filesystem::path& binaryDir, const std::string_view appId, const EnvLookup& env)
    {
        if (const auto forced = env("GPP_APP_MODE"))
        {
            if (*forced == "dev")
                return Mode::Dev;
            if (*forced == "installed")
                return Mode::Installed;
        }
        std::error_code error;
        return std::filesystem::is_directory(binaryDir / ".." / ".." / "share" / std::string(appId), error)
                   ? Mode::Installed
                   : Mode::Dev;
    }

    AppPaths AppPaths::Resolve(Inputs inputs)
    {
        if (!inputs.Env)
        {
            inputs.Env = ProcessEnv();
        }
        AppPaths paths;
        paths.m_Mode = DetectMode(inputs.BinaryDir, inputs.Info.Id, inputs.Env);
        if (paths.m_Mode == Mode::Dev)
        {
            paths.m_ResourceDir = inputs.WorkingDir;
            paths.m_ConfigDir = paths.m_StateDir = paths.m_CacheDir = paths.m_UserDataDir = ".gpp";
            paths.m_LogDir = std::filesystem::path(".gpp") / "logs";
            return paths;
        }

        const auto xdg = ResolveXdgDirs(inputs.Env);
        const std::filesystem::path id(inputs.Info.Id);
        paths.m_ResourceDir = (inputs.BinaryDir / ".." / ".." / "share" / id).lexically_normal();
        paths.m_ConfigDir = paths.m_StateDir = xdg.ConfigHome / id;
        paths.m_CacheDir = xdg.CacheHome / id;
        paths.m_LogDir = xdg.StateHome / id / "logs";
        paths.m_UserDataDir = xdg.Documents / (inputs.Info.Name.empty() ? inputs.Info.Id : inputs.Info.Name);
        return paths;
    }

    AppPaths::AppPaths(std::shared_ptr<IFileSystem> fileSystem, std::shared_ptr<AppInfo> info)
    {
        *this = Resolve({
            .BinaryDir = fileSystem->GetBinaryDirectory(),
            .WorkingDir = fileSystem->GetWorkingDirectory(),
            .Info = *info,
            .Env = ProcessEnv()
        });
        fileSystem->SetResourceDir(m_ResourceDir);
        fileSystem->SetCacheDir(m_CacheDir);
    }

    std::filesystem::path AppPaths::UserSubdir(const std::string_view name) const
    {
        return m_UserDataDir / std::string(name);
    }

    std::filesystem::path AppPaths::TempDir() const
    {
        return m_CacheDir / "tmp";
    }
}
