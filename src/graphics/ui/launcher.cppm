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
            Rescan,
            Quit,
            Custom
        };

        Type Kind = None;
        std::string Path;
    };

    export struct LauncherNotice
    {
        std::string Text;
        std::string ActionLabel;
        std::string ActionId;
        std::string DismissId;
        std::string DismissLabel;
    };

    export struct LauncherData
    {
        LauncherBranding Branding;
        LauncherStyle Style;
        std::vector<LauncherRecent> Recents;
        std::vector<LauncherTemplate> Templates;
        LauncherNotice Notice;
        std::vector<LauncherNotice> Notices;
        bool ShowOnStartup = true;
        std::string NewLabel = "New Scene";
        std::string OpenLabel = "Open...";
        std::string EmptyText = "No recent items yet.";

        LauncherPage Page = LauncherPage::Recent;
        std::string Filter;
        std::size_t Selected = 0;
        std::size_t SelectedTemplate = 0;
        std::size_t PageRows = 8;
        bool FilterFocused = false;
        bool FocusFilter = false;
        bool ScrollToSelected = false;
        std::string PendingRemove;
    };

    export enum class LauncherKey
    {
        Up, Down, Left, Right, Home, End, PageUp, PageDown, Enter, Escape, Tab, Delete, F5, Slash, N, O, R, F
    };

    export struct LauncherKeyEvent
    {
        LauncherKey Key = LauncherKey::Enter;
        bool Ctrl = false;
        bool Shift = false;
    };

    export struct LauncherKeyState
    {
        LauncherPage Page = LauncherPage::Recent;
        bool TextActive = false;
        bool FilterFocused = false;
        bool FilterEmpty = true;
        bool ModalOpen = false;
        std::size_t ItemCount = 0;
    };

    export enum class LauncherKeyCommand
    {
        None,
        Previous,
        Next,
        First,
        Last,
        PageUp,
        PageDown,
        Activate,
        NextPage,
        PreviousPage,
        New,
        Open,
        Remove,
        RemoveNow,
        Rescan,
        FocusFilter,
        ClearFilter,
        Close,
        ConfirmRemove,
        CancelRemove
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

    export [[nodiscard]] bool LauncherMatches(std::string_view text, std::string_view filter);

    export [[nodiscard]] std::vector<std::size_t> FilterLauncherRecents(
        const std::vector<LauncherRecent>& recents, std::string_view filter);

    export [[nodiscard]] LauncherKeyCommand ResolveLauncherKey(const LauncherKeyEvent& event, const LauncherKeyState& state);

    export [[nodiscard]] std::size_t MoveLauncherSelection(
        std::size_t selected, std::size_t count, LauncherKeyCommand command, std::size_t pageRows);

    export [[nodiscard]] std::string LauncherKeyHints(LauncherPage page);

    export [[nodiscard]] LauncherAction DrawLauncher(LauncherData& data, const LauncherHooks& hooks = {});
}
