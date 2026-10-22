export module GPP.Graphics:UI.Launcher;

import std;

namespace GPP
{
    export enum class LauncherPage
    {
        Recent,
        Templates,
        Settings
    };

    export struct LauncherRecent
    {
        std::string Path;
        std::string Name;
        std::string Subtitle;
    };

    export struct LauncherTemplate
    {
        std::string Name;
        std::string Path;
        std::string Description;
    };

    export struct LauncherBranding
    {
        std::string Title;
        std::string Tagline;
        std::string Version;
        std::uint64_t IconTexture = 0;
    };

    export struct LauncherStyle
    {
        std::optional<std::array<float, 4>> Accent;
    };

    export struct LauncherAction
    {
        enum Type
        {
            None,
            OpenRecent,
            NewItem,
            OpenFile,
            OpenTemplate,
            RemoveRecent,
            Quit,
            Custom
        };

        Type Kind = None;
        std::string Path;
    };

    export struct LauncherData
    {
        LauncherBranding Branding;
        LauncherStyle Style;
        std::vector<LauncherRecent> Recents;
        std::vector<LauncherTemplate> Templates;
        bool ShowOnStartup = true;
        std::string NewLabel = "New Scene";
        std::string OpenLabel = "Open...";
        std::string EmptyText = "No recent items yet.";

        LauncherPage Page = LauncherPage::Recent;
        std::string Filter;
        std::size_t Selected = 0;
    };

    export struct LauncherButton
    {
        std::string Label;
        std::string Id;
    };

    export struct LauncherHooks
    {
        std::function<void()> DrawSettings;
        std::vector<LauncherButton> ExtraButtons;
    };

    // Whitespace separated terms; every term must occur in `text`, ignoring case.
    export [[nodiscard]] bool LauncherMatches(std::string_view text, std::string_view filter);

    export [[nodiscard]] std::vector<std::size_t> FilterLauncherRecents(
        const std::vector<LauncherRecent>& recents, std::string_view filter);

    // Draws into the current window, which the caller makes borderless and window-filling.
    export [[nodiscard]] LauncherAction DrawLauncher(LauncherData& data, const LauncherHooks& hooks = {});
}
