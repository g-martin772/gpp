export module GPP.Core:Platform.Desktop;

import std;
import :IO.AppPaths;
import :Application.Info;

namespace GPP
{
    export class DesktopEntry
    {
    public:
        [[nodiscard]] static std::string Build(const AppInfo& info, const std::filesystem::path& executable);
    };

    export struct DesktopInstallOptions
    {
        std::filesystem::path Prefix;
        std::filesystem::path Executable;
        std::filesystem::path IconSource;
        AppInfo Info;
        EnvLookup Env;
    };

    export struct DesktopInstallResult
    {
        bool Success{false};
        std::vector<std::filesystem::path> Paths;
        std::vector<std::string> Warnings;
    };

    export class DesktopInstaller
    {
    public:
        [[nodiscard]] static DesktopInstallResult Install(const DesktopInstallOptions& options);
        [[nodiscard]] static DesktopInstallResult Uninstall(
            const std::filesystem::path& prefix, std::string_view id, const EnvLookup& env = {});
    };

    export enum class WmKind
    {
        Hyprland,
        Sway,
        I3,
        Other
    };

    export enum class WmRulePolicy
    {
        Ask,
        Always,
        Never
    };

    export enum class WmRuleOutcome
    {
        Unsupported,
        NoConfigFile,
        AlreadyDecided,
        Skipped,
        Declined,
        Applied
    };

    export class IWmPrompter
    {
    public:
        virtual ~IWmPrompter() = default;
        [[nodiscard]] virtual bool IsInteractive() const = 0;
        [[nodiscard]] virtual bool Confirm(std::string_view question) = 0;
    };

    export class StdioWmPrompter final : public IWmPrompter
    {
    public:
        [[nodiscard]] bool IsInteractive() const override;
        [[nodiscard]] bool Confirm(std::string_view question) override;
    };

    export struct WmRulesOptions
    {
        AppInfo Info;
        std::filesystem::path ConfigDir;
        WmRulePolicy Policy{WmRulePolicy::Ask};
        EnvLookup Env;
        IWmPrompter* Prompter{nullptr};
    };

    export struct WmRulesResult
    {
        WmKind Wm{WmKind::Other};
        WmRuleOutcome Outcome{WmRuleOutcome::Unsupported};
        std::filesystem::path SnippetPath;
        std::filesystem::path ConfigFile;
        std::vector<std::string> Warnings;
    };

    export class WmRules
    {
    public:
        [[nodiscard]] static WmKind Detect(const EnvLookup& env);
        [[nodiscard]] static std::string Snippet(WmKind wm, std::string_view id, bool hyprlandLua);
        [[nodiscard]] static std::string_view CommentPrefix(WmKind wm, bool hyprlandLua);
        [[nodiscard]] static std::string_view SnippetExtension(WmKind wm, bool hyprlandLua);
        [[nodiscard]] static std::optional<std::filesystem::path> ConfigFile(WmKind wm, const EnvLookup& env);
        [[nodiscard]] static WmRulesResult Apply(const WmRulesOptions& options);
    };

    export struct DesktopCliContext
    {
        AppInfo Info;
        std::filesystem::path ResourceDir;
        std::filesystem::path ConfigDir;
        // Empty: /proc/self/exe.
        std::filesystem::path BinaryPath;
        EnvLookup Env;
        IWmPrompter* Prompter{nullptr};
        std::ostream* Out{nullptr};
    };

    // --install-desktop [--prefix P] [--apply-wm-rules|--no-wm-rules], --uninstall-desktop [--prefix P]
    export [[nodiscard]] std::optional<int> HandleDesktopArgs(
        std::span<const std::string> args, const DesktopCliContext& context);
    export [[nodiscard]] std::optional<int> HandleDesktopArgs(
        int argc, const char* const* argv, const DesktopCliContext& context);
}
