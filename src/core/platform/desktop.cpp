module;

#include <nlohmann/json.hpp>

#if defined(__linux__)
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

module GPP.Core;

import std;
import :Platform.Desktop;

namespace GPP
{
    namespace fs = std::filesystem;

    namespace
    {
        EnvLookup OrProcessEnv(const EnvLookup& env)
        {
            return env ? env : AppPaths::ProcessEnv();
        }

        std::string EscapeExecArg(const std::string& argument)
        {
            constexpr std::string_view reserved = " \t\n\"'\\<>~|&;$*?#()`";
            std::string text;
            if (argument.find_first_of(reserved) == std::string::npos)
            {
                text = argument;
            }
            else
            {
                text = "\"";
                for (const char c : argument)
                {
                    if (c == '"' || c == '`' || c == '$' || c == '\\')
                    {
                        text += '\\';
                    }
                    text += c;
                }
                text += '"';
            }
            std::string out;
            for (const char c : text)
            {
                if (c == '\\')
                {
                    out += "\\\\";
                }
                else if (c == '%')
                {
                    out += "%%";
                }
                else
                {
                    out += c;
                }
            }
            return out;
        }

        std::string WithTrailingSemicolon(std::string text)
        {
            if (!text.empty() && text.back() != ';')
            {
                text += ';';
            }
            return text;
        }

        bool WriteTextFile(const fs::path& path, const std::string& content)
        {
            std::error_code error;
            fs::create_directories(path.parent_path(), error);
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file)
            {
                return false;
            }
            file << content;
            return static_cast<bool>(file);
        }

        std::optional<std::string> ReadTextFile(const fs::path& path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file)
            {
                return std::nullopt;
            }
            std::stringstream buffer;
            buffer << file.rdbuf();
            return buffer.str();
        }

        std::optional<fs::path> FindInPath(const EnvLookup& env, const std::string& tool)
        {
            const auto path = env("PATH");
            if (!path)
            {
                return std::nullopt;
            }
            std::istringstream stream(*path);
            std::string entry;
            while (std::getline(stream, entry, ':'))
            {
                if (entry.empty())
                {
                    continue;
                }
                const auto candidate = fs::path(entry) / tool;
                std::error_code error;
                if (fs::is_regular_file(candidate, error) && ::access(candidate.c_str(), X_OK) == 0)
                {
                    return candidate;
                }
            }
            return std::nullopt;
        }

        void RunTool(const EnvLookup& env, const std::string& tool, const std::vector<std::string>& args,
                     std::vector<std::string>& warnings)
        {
#if defined(__linux__)
            const auto executable = FindInPath(env, tool);
            if (!executable)
            {
                return;
            }
            std::vector<std::string> storage{executable->string()};
            storage.insert(storage.end(), args.begin(), args.end());
            std::vector<char*> argv;
            for (auto& arg : storage)
            {
                argv.push_back(arg.data());
            }
            argv.push_back(nullptr);

            posix_spawn_file_actions_t actions;
            posix_spawn_file_actions_init(&actions);
            posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
            posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
            pid_t pid{};
            const int rc = posix_spawn(&pid, executable->c_str(), &actions, nullptr, argv.data(), environ);
            posix_spawn_file_actions_destroy(&actions);
            int status = 0;
            if (rc != 0 || waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
            {
                warnings.push_back(tool + " failed");
            }
#else
            (void)env;
            (void)tool;
            (void)args;
            (void)warnings;
#endif
        }

        bool CacheSkipped(const EnvLookup& env)
        {
            const auto value = env("GPP_SKIP_DESKTOP_CACHE");
            return value && *value == "1";
        }

        std::optional<unsigned> PngSquareSize(const fs::path& path)
        {
            std::ifstream file(path, std::ios::binary);
            std::array<unsigned char, 24> header{};
            if (!file.read(reinterpret_cast<char*>(header.data()), header.size()))
            {
                return std::nullopt;
            }
            constexpr std::array<unsigned char, 8> signature{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
            if (!std::equal(signature.begin(), signature.end(), header.begin()))
            {
                return std::nullopt;
            }
            const auto read = [&](const std::size_t offset)
            {
                return (static_cast<unsigned>(header[offset]) << 24) | (static_cast<unsigned>(header[offset + 1]) << 16) |
                    (static_cast<unsigned>(header[offset + 2]) << 8) | static_cast<unsigned>(header[offset + 3]);
            };
            const unsigned width = read(16);
            const unsigned height = read(20);
            if (width == 0 || width != height)
            {
                return std::nullopt;
            }
            return width;
        }

        fs::path ApplicationsDir(const fs::path& prefix) { return prefix / "share" / "applications"; }
        fs::path HicolorDir(const fs::path& prefix) { return prefix / "share" / "icons" / "hicolor"; }

        void RefreshCaches(const fs::path& prefix, const EnvLookup& env, std::vector<std::string>& warnings)
        {
            if (CacheSkipped(env))
            {
                return;
            }
            RunTool(env, "update-desktop-database", {ApplicationsDir(prefix).string()}, warnings);
            RunTool(env, "gtk-update-icon-cache", {"-f", "-t", HicolorDir(prefix).string()}, warnings);
        }

        std::string Lower(std::string text)
        {
            std::ranges::transform(text, text.begin(), [](const unsigned char c) { return std::tolower(c); });
            return text;
        }

        bool ContainsToken(const std::string& value, const std::string_view token)
        {
            std::istringstream stream(Lower(value));
            std::string part;
            while (std::getline(stream, part, ':'))
            {
                if (part == token)
                {
                    return true;
                }
            }
            return false;
        }

        bool EnvSet(const EnvLookup& env, const std::string_view name)
        {
            const auto value = env(name);
            return value && !value->empty();
        }

        std::string MarkerLine(const std::string_view prefix, const std::string_view id, const std::string_view tag)
        {
            return std::format("{} {}:{}", prefix, id, tag);
        }

        bool HasMarkerBlock(const std::string& content, const std::string& beginMarker)
        {
            std::istringstream stream(content);
            std::string line;
            while (std::getline(stream, line))
            {
                if (line == beginMarker)
                {
                    return true;
                }
            }
            return false;
        }

        std::optional<std::string> StoredDecision(const fs::path& installJson)
        {
            const auto text = ReadTextFile(installJson);
            if (!text)
            {
                return std::nullopt;
            }
            const auto json = nlohmann::json::parse(*text, nullptr, false);
            if (json.is_object() && json.contains("wmRules") && json["wmRules"].is_string())
            {
                return json["wmRules"].get<std::string>();
            }
            return std::nullopt;
        }

        bool StoreDecision(const fs::path& installJson, const std::string& decision)
        {
            nlohmann::json json = nlohmann::json::object();
            if (const auto text = ReadTextFile(installJson))
            {
                if (auto parsed = nlohmann::json::parse(*text, nullptr, false); parsed.is_object())
                {
                    json = std::move(parsed);
                }
            }
            json["wmRules"] = decision;
            return WriteTextFile(installJson, json.dump(2) + "\n");
        }
    }

    std::string DesktopEntry::Build(const AppInfo& info, const fs::path& executable)
    {
        const auto exec = EscapeExecArg(executable.string());
        std::string text = "[Desktop Entry]\nType=Application\nVersion=1.5\n";
        text += std::format("Name={}\n", info.Name);
        if (!info.Summary.empty())
        {
            text += std::format("Comment={}\n", info.Summary);
        }
        text += std::format("Exec={} %f\n", exec);
        text += std::format("Icon={}\n", info.Id);
        text += "Terminal=false\n";
        if (!info.Categories.empty())
        {
            text += std::format("Categories={}\n", WithTrailingSemicolon(info.Categories));
        }
        text += std::format("StartupWMClass={}\n", info.Id);
        text += "StartupNotify=true\nActions=Launcher;\n\n";
        text += "[Desktop Action Launcher]\nName=Open Launcher\n";
        text += std::format("Exec={} --launcher\n", exec);
        return text;
    }

    DesktopInstallResult DesktopInstaller::Install(const DesktopInstallOptions& options)
    {
        DesktopInstallResult result;
        const auto env = OrProcessEnv(options.Env);
        const auto& id = options.Info.Id;

        const auto desktopFile = ApplicationsDir(options.Prefix) / (id + ".desktop");
        if (!WriteTextFile(desktopFile, DesktopEntry::Build(options.Info, options.Executable)))
        {
            result.Warnings.push_back("could not write " + desktopFile.string());
            return result;
        }
        result.Paths.push_back(desktopFile);

        std::error_code error;
        if (options.IconSource.empty() || !fs::is_regular_file(options.IconSource, error))
        {
            result.Warnings.push_back("icon not found: " + options.IconSource.string());
        }
        else
        {
            const auto extension = Lower(options.IconSource.extension().string());
            std::optional<fs::path> target;
            if (extension == ".svg")
            {
                target = HicolorDir(options.Prefix) / "scalable" / "apps" / (id + ".svg");
            }
            else if (extension == ".png")
            {
                if (const auto size = PngSquareSize(options.IconSource))
                {
                    target = HicolorDir(options.Prefix) / std::format("{0}x{0}", *size) / "apps" / (id + ".png");
                }
                else
                {
                    result.Warnings.push_back("png icon is not square: " + options.IconSource.string());
                }
            }
            else
            {
                result.Warnings.push_back("unsupported icon format: " + options.IconSource.string());
            }
            if (target)
            {
                fs::create_directories(target->parent_path(), error);
                fs::copy_file(options.IconSource, *target, fs::copy_options::overwrite_existing, error);
                if (error)
                {
                    result.Warnings.push_back("could not copy icon: " + error.message());
                }
                else
                {
                    result.Paths.push_back(*target);
                }
            }
        }

        RefreshCaches(options.Prefix, env, result.Warnings);
        result.Success = true;
        return result;
    }

    DesktopInstallResult DesktopInstaller::Uninstall(const fs::path& prefix, const std::string_view id, const EnvLookup& env)
    {
        DesktopInstallResult result;
        const auto lookup = OrProcessEnv(env);
        const std::string name(id);
        std::error_code error;

        const auto remove = [&](const fs::path& path)
        {
            error.clear();
            if (fs::remove(path, error))
            {
                result.Paths.push_back(path);
            }
            else if (error)
            {
                result.Warnings.push_back("could not remove " + path.string());
            }
        };

        remove(ApplicationsDir(prefix) / (name + ".desktop"));
        const auto hicolor = HicolorDir(prefix);
        if (fs::is_directory(hicolor, error))
        {
            for (const auto& entry : fs::directory_iterator(hicolor, error))
            {
                if (!entry.is_directory(error))
                {
                    continue;
                }
                remove(entry.path() / "apps" / (name + ".svg"));
                remove(entry.path() / "apps" / (name + ".png"));
            }
        }
        RefreshCaches(prefix, lookup, result.Warnings);
        result.Success = true;
        return result;
    }

    bool StdioWmPrompter::IsInteractive() const
    {
#if defined(__linux__)
        return ::isatty(0) != 0;
#else
        return false;
#endif
    }

    bool StdioWmPrompter::Confirm(const std::string_view question)
    {
        std::cout << question << " [y/N] " << std::flush;
        std::string answer;
        if (!std::getline(std::cin, answer))
        {
            return false;
        }
        answer = Lower(answer);
        return answer == "y" || answer == "yes";
    }

    WmKind WmRules::Detect(const EnvLookup& env)
    {
        const auto classify = [](const std::string& value) -> std::optional<WmKind>
        {
            if (ContainsToken(value, "hyprland"))
                return WmKind::Hyprland;
            if (ContainsToken(value, "sway"))
                return WmKind::Sway;
            if (ContainsToken(value, "i3"))
                return WmKind::I3;
            return std::nullopt;
        };

        if (const auto desktop = env("XDG_CURRENT_DESKTOP"))
        {
            if (const auto wm = classify(*desktop))
                return *wm;
        }
        if (EnvSet(env, "HYPRLAND_INSTANCE_SIGNATURE"))
            return WmKind::Hyprland;
        if (EnvSet(env, "SWAYSOCK"))
            return WmKind::Sway;
        if (EnvSet(env, "I3SOCK"))
            return WmKind::I3;
        if (const auto session = env("DESKTOP_SESSION"))
        {
            if (const auto wm = classify(*session))
                return *wm;
        }
        return WmKind::Other;
    }

    std::string WmRules::Snippet(const WmKind wm, const std::string_view id, const bool hyprlandLua)
    {
        switch (wm)
        {
        case WmKind::Hyprland:
            if (hyprlandLua)
            {
                return std::format("hl.window_rule({{ match = {{ class = \"{}\" }}, float = true, center = true }})\n", id);
            }
            return std::format(
                "windowrule = float on, match:class ^({0})$\nwindowrule = center on, match:class ^({0})$\n", id);
        case WmKind::Sway:
            return std::format("for_window [app_id=\"{}\"] floating enable\n", id);
        case WmKind::I3:
            return std::format("for_window [class=\"{}\"] floating enable\n", id);
        case WmKind::Other:
            break;
        }
        return {};
    }

    std::string_view WmRules::CommentPrefix(const WmKind wm, const bool hyprlandLua)
    {
        return wm == WmKind::Hyprland && hyprlandLua ? "--" : "#";
    }

    std::string_view WmRules::SnippetExtension(const WmKind wm, const bool hyprlandLua)
    {
        switch (wm)
        {
        case WmKind::Hyprland:
            return hyprlandLua ? "lua" : "conf";
        case WmKind::Sway:
            return "sway";
        case WmKind::I3:
            return "i3";
        case WmKind::Other:
            break;
        }
        return "txt";
    }

    std::optional<fs::path> WmRules::ConfigFile(const WmKind wm, const EnvLookup& env)
    {
        const auto home = AppPaths::ResolveXdgDirs(env).ConfigHome;
        std::vector<fs::path> candidates;
        switch (wm)
        {
        case WmKind::Hyprland:
            candidates = {home / "hypr" / "hyprland.lua", home / "hypr" / "hyprland.conf"};
            break;
        case WmKind::Sway:
            candidates = {home / "sway" / "config"};
            break;
        case WmKind::I3:
            candidates = {home / "i3" / "config"};
            break;
        case WmKind::Other:
            break;
        }
        for (const auto& candidate : candidates)
        {
            std::error_code error;
            if (fs::is_regular_file(candidate, error))
            {
                return candidate;
            }
        }
        return std::nullopt;
    }

    WmRulesResult WmRules::Apply(const WmRulesOptions& options)
    {
        WmRulesResult result;
        const auto env = OrProcessEnv(options.Env);
        const auto& id = options.Info.Id;
        result.Wm = Detect(env);
        if (result.Wm == WmKind::Other)
        {
            return result;
        }

        const auto configFile = ConfigFile(result.Wm, env);
        const bool lua = configFile && configFile->extension() == ".lua";
        const auto snippet = Snippet(result.Wm, id, lua);
        const auto prefix = CommentPrefix(result.Wm, lua);

        result.SnippetPath = options.ConfigDir / "wm" / (id + "." + std::string(SnippetExtension(result.Wm, lua)));
        if (!WriteTextFile(result.SnippetPath, snippet))
        {
            result.Warnings.push_back("could not write " + result.SnippetPath.string());
        }
        if (!configFile)
        {
            result.Outcome = WmRuleOutcome::NoConfigFile;
            return result;
        }
        result.ConfigFile = *configFile;

        const auto installJson = options.ConfigDir / "install.json";
        const auto decision = StoredDecision(installJson);
        const bool decided = decision && (*decision == "applied" || *decision == "declined");
        // An explicit request overrides an earlier "declined".
        if (decided && !(options.Policy == WmRulePolicy::Always && *decision == "declined"))
        {
            result.Outcome = WmRuleOutcome::AlreadyDecided;
            return result;
        }

        bool apply = options.Policy == WmRulePolicy::Always;
        if (options.Policy == WmRulePolicy::Never)
        {
            result.Outcome = WmRuleOutcome::Skipped;
            return result;
        }
        if (options.Policy == WmRulePolicy::Ask)
        {
            StdioWmPrompter fallback;
            IWmPrompter& prompter = options.Prompter ? *options.Prompter : fallback;
            if (!prompter.IsInteractive())
            {
                result.Outcome = WmRuleOutcome::Skipped;
                return result;
            }
            apply = prompter.Confirm(
                std::format("Add a floating rule for {} to {}?", options.Info.Name, configFile->string()));
            if (!apply)
            {
                if (!StoreDecision(installJson, "declined"))
                {
                    result.Warnings.push_back("could not write " + installJson.string());
                }
                result.Outcome = WmRuleOutcome::Declined;
                return result;
            }
        }

        const auto original = ReadTextFile(*configFile).value_or(std::string{});
        const auto beginMarker = MarkerLine(prefix, id, "begin");
        if (!HasMarkerBlock(original, beginMarker))
        {
            const auto backup = fs::path(configFile->string() + ".bak-" + id);
            std::error_code error;
            if (!fs::exists(backup, error))
            {
                fs::copy_file(*configFile, backup, error);
                if (error)
                {
                    result.Warnings.push_back("could not back up " + configFile->string());
                    return result;
                }
            }
            std::string updated = original;
            if (!updated.empty() && updated.back() != '\n')
            {
                updated += '\n';
            }
            updated += std::format("\n{}\n{}{}\n", beginMarker, snippet, MarkerLine(prefix, id, "end"));
            if (!WriteTextFile(*configFile, updated))
            {
                result.Warnings.push_back("could not write " + configFile->string());
                return result;
            }
        }
        if (!StoreDecision(installJson, "applied"))
        {
            result.Warnings.push_back("could not write " + installJson.string());
        }
        result.Outcome = WmRuleOutcome::Applied;
        return result;
    }

    std::optional<int> HandleDesktopArgs(const std::span<const std::string> args, const DesktopCliContext& context)
    {
        bool install = false;
        bool uninstall = false;
        bool applyRules = false;
        bool noRules = false;
        std::optional<fs::path> prefix;
        for (std::size_t i = 0; i < args.size(); ++i)
        {
            if (args[i] == "--install-desktop")
                install = true;
            else if (args[i] == "--uninstall-desktop")
                uninstall = true;
            else if (args[i] == "--apply-wm-rules")
                applyRules = true;
            else if (args[i] == "--no-wm-rules")
                noRules = true;
            else if (args[i] == "--prefix" && i + 1 < args.size())
                prefix = fs::path(args[++i]);
            else if (args[i].starts_with("--prefix="))
                prefix = fs::path(args[i].substr(9));
        }
        if (!install && !uninstall)
        {
            return std::nullopt;
        }

        std::ostream& out = context.Out ? *context.Out : std::cout;
        const auto env = OrProcessEnv(context.Env);
        const auto& id = context.Info.Id;

        std::error_code error;
        fs::path binary = context.BinaryPath;
        if (binary.empty())
        {
            binary = fs::read_symlink("/proc/self/exe", error);
        }
        if (!prefix)
        {
            const auto derived = (binary.parent_path() / ".." / "..").lexically_normal();
            if (fs::is_directory(derived / "share" / id, error))
            {
                prefix = derived;
            }
            else
            {
                prefix = AppPaths::ResolveXdgDirs(env).Home / ".local";
            }
        }
        *prefix = fs::absolute(*prefix, error).lexically_normal();

        if (install && uninstall)
        {
            out << "Use either --install-desktop or --uninstall-desktop\n";
            return 2;
        }

        if (uninstall)
        {
            const auto result = DesktopInstaller::Uninstall(*prefix, id, env);
            for (const auto& path : result.Paths)
                out << "Removed " << path.string() << '\n';
            for (const auto& warning : result.Warnings)
                out << "Warning: " << warning << '\n';
            out << "Desktop integration removed from " << prefix->string() << '\n';
            return 0;
        }

        auto executable = *prefix / "bin" / id;
        if (!fs::exists(fs::symlink_status(executable, error)))
        {
            executable = binary;
        }
        const auto result = DesktopInstaller::Install({
            .Prefix = *prefix,
            .Executable = executable,
            .IconSource = context.ResourceDir / context.Info.IconFile,
            .Info = context.Info,
            .Env = env
        });
        for (const auto& path : result.Paths)
            out << "Wrote " << path.string() << '\n';
        for (const auto& warning : result.Warnings)
            out << "Warning: " << warning << '\n';
        if (!result.Success)
        {
            return 1;
        }

        if (!noRules)
        {
            const auto rules = WmRules::Apply({
                .Info = context.Info,
                .ConfigDir = context.ConfigDir,
                .Policy = applyRules ? WmRulePolicy::Always : WmRulePolicy::Ask,
                .Env = env,
                .Prompter = context.Prompter
            });
            if (!rules.SnippetPath.empty())
                out << "Wrote " << rules.SnippetPath.string() << '\n';
            if (rules.Outcome == WmRuleOutcome::Applied)
                out << "Added window rule to " << rules.ConfigFile.string() << '\n';
            for (const auto& warning : rules.Warnings)
                out << "Warning: " << warning << '\n';
        }
        out << "Desktop integration installed to " << prefix->string() << '\n';
        return 0;
    }

    std::optional<int> HandleDesktopArgs(const int argc, const char* const* argv, const DesktopCliContext& context)
    {
        std::vector<std::string> args;
        for (int i = 1; i < argc; ++i)
        {
            args.emplace_back(argv[i]);
        }
        return HandleDesktopArgs(args, context);
    }
}
