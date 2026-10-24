#include <catch2/catch_test_macros.hpp>

import GPP;
import std;

using namespace GPP;

TEST_CASE("LauncherMatches is case-insensitive and requires every term", "[launcher]")
{
    CHECK(LauncherMatches("Black Hole /home/u/scenes/bh.yaml", ""));
    CHECK(LauncherMatches("Black Hole", "hole"));
    CHECK(LauncherMatches("Black Hole", "  BLACK   hol "));
    CHECK_FALSE(LauncherMatches("Black Hole", "black star"));
    CHECK_FALSE(LauncherMatches("", "x"));
}

TEST_CASE("FilterLauncherRecents matches name or path and keeps order", "[launcher]")
{
    const std::vector<LauncherRecent> recents{
        {"/s/alpha.yaml", "Alpha", ""},
        {"/orbits/beta.yaml", "Beta", ""},
        {"/s/gamma.yaml", "", ""},
    };

    CHECK(FilterLauncherRecents(recents, "").size() == 3);
    CHECK(FilterLauncherRecents(recents, "orbits") == std::vector<std::size_t>{1});
    CHECK(FilterLauncherRecents(recents, "/S/ A") == std::vector<std::size_t>{0, 2});
    CHECK(FilterLauncherRecents(recents, "zzz").empty());
}

namespace
{
    LauncherKeyCommand Key(const LauncherKey key, const LauncherKeyState& state = {.ItemCount = 3},
                           const bool ctrl = false, const bool shift = false)
    {
        return ResolveLauncherKey({key, ctrl, shift}, state);
    }
}

TEST_CASE("ResolveLauncherKey maps navigation and activation", "[launcher]")
{
    using C = LauncherKeyCommand;
    using K = LauncherKey;
    CHECK(Key(K::Up) == C::Previous);
    CHECK(Key(K::Down) == C::Next);
    CHECK(Key(K::Home) == C::First);
    CHECK(Key(K::End) == C::Last);
    CHECK(Key(K::PageUp) == C::PageUp);
    CHECK(Key(K::PageDown) == C::PageDown);
    CHECK(Key(K::Enter) == C::Activate);
    CHECK(Key(K::Escape) == C::Close);
    CHECK(Key(K::Tab) == C::NextPage);
    CHECK(Key(K::Tab, {.ItemCount = 3}, false, true) == C::PreviousPage);
    CHECK(Key(K::Left) == C::PreviousPage);
    CHECK(Key(K::Right) == C::NextPage);
    CHECK(Key(K::F5) == C::Rescan);
    CHECK(Key(K::Slash) == C::FocusFilter);
}

TEST_CASE("ResolveLauncherKey handles shortcuts with Ctrl", "[launcher]")
{
    using C = LauncherKeyCommand;
    using K = LauncherKey;
    CHECK(Key(K::N, {}, true) == C::New);
    CHECK(Key(K::O, {}, true) == C::Open);
    CHECK(Key(K::R, {}, true) == C::Rescan);
    CHECK(Key(K::F, {}, true) == C::FocusFilter);
    CHECK(Key(K::N) == C::None);
    CHECK(Key(K::F) == C::None);
}

TEST_CASE("ResolveLauncherKey needs items and a list page", "[launcher]")
{
    using C = LauncherKeyCommand;
    using K = LauncherKey;
    const LauncherKeyState empty{.ItemCount = 0};
    CHECK(Key(K::Enter, empty) == C::None);
    CHECK(Key(K::Down, empty) == C::None);
    CHECK(Key(K::Delete, empty) == C::None);
    CHECK(Key(K::Escape, empty) == C::Close);

    const LauncherKeyState settings{.Page = LauncherPage::Settings, .ItemCount = 0};
    CHECK(Key(K::Enter, settings) == C::None);
    CHECK(Key(K::Tab, settings) == C::NextPage);
    CHECK(Key(K::Slash, settings) == C::FocusFilter);
}

TEST_CASE("ResolveLauncherKey delete only removes recents", "[launcher]")
{
    using C = LauncherKeyCommand;
    using K = LauncherKey;
    CHECK(Key(K::Delete) == C::Remove);
    CHECK(Key(K::Delete, {.ItemCount = 3}, false, true) == C::RemoveNow);
    CHECK(Key(K::Delete, {.Page = LauncherPage::Templates, .ItemCount = 3}) == C::None);
}

TEST_CASE("ResolveLauncherKey leaves text editing keys to the filter", "[launcher]")
{
    using C = LauncherKeyCommand;
    using K = LauncherKey;
    const LauncherKeyState typing{.TextActive = true, .FilterFocused = true, .FilterEmpty = false, .ItemCount = 3};
    CHECK(Key(K::Home, typing) == C::None);
    CHECK(Key(K::End, typing) == C::None);
    CHECK(Key(K::Left, typing) == C::None);
    CHECK(Key(K::Right, typing) == C::None);
    CHECK(Key(K::Delete, typing) == C::None);
    CHECK(Key(K::Slash, typing) == C::None);
    CHECK(Key(K::Down, typing) == C::Next);
    CHECK(Key(K::Enter, typing) == C::Activate);
    CHECK(Key(K::N, typing, true) == C::New);
    CHECK(Key(K::Escape, typing) == C::ClearFilter);

    const LauncherKeyState emptyFocused{.TextActive = true, .FilterFocused = true, .FilterEmpty = true, .ItemCount = 3};
    CHECK(Key(K::Home, emptyFocused) == C::First);
    CHECK(Key(K::Right, emptyFocused) == C::NextPage);
    CHECK(Key(K::Escape, emptyFocused) == C::Close);
}

TEST_CASE("ResolveLauncherKey ignores everything while another text field is active", "[launcher]")
{
    using K = LauncherKey;
    const LauncherKeyState other{.TextActive = true, .FilterFocused = false, .ItemCount = 3};
    for (const auto key : {K::Up, K::Down, K::Enter, K::Escape, K::Tab, K::Delete, K::F5, K::Slash, K::Left})
    {
        CHECK(Key(key, other) == LauncherKeyCommand::None);
    }
    CHECK(Key(K::N, other, true) == LauncherKeyCommand::None);
}

TEST_CASE("ResolveLauncherKey routes only Enter and Escape while confirming a removal", "[launcher]")
{
    using C = LauncherKeyCommand;
    using K = LauncherKey;
    const LauncherKeyState modal{.ModalOpen = true, .ItemCount = 3};
    CHECK(Key(K::Enter, modal) == C::ConfirmRemove);
    CHECK(Key(K::Escape, modal) == C::CancelRemove);
    CHECK(Key(K::Down, modal) == C::None);
    CHECK(Key(K::Delete, modal) == C::None);
    CHECK(Key(K::N, modal, true) == C::None);
}

TEST_CASE("MoveLauncherSelection clamps and pages", "[launcher]")
{
    using C = LauncherKeyCommand;
    CHECK(MoveLauncherSelection(0, 0, C::Next, 5) == 0);
    CHECK(MoveLauncherSelection(0, 10, C::Previous, 5) == 0);
    CHECK(MoveLauncherSelection(9, 10, C::Next, 5) == 9);
    CHECK(MoveLauncherSelection(3, 10, C::Next, 5) == 4);
    CHECK(MoveLauncherSelection(3, 10, C::Previous, 5) == 2);
    CHECK(MoveLauncherSelection(3, 10, C::First, 5) == 0);
    CHECK(MoveLauncherSelection(3, 10, C::Last, 5) == 9);
    CHECK(MoveLauncherSelection(3, 10, C::PageDown, 5) == 8);
    CHECK(MoveLauncherSelection(8, 10, C::PageDown, 5) == 9);
    CHECK(MoveLauncherSelection(3, 10, C::PageUp, 5) == 0);
    CHECK(MoveLauncherSelection(7, 10, C::PageUp, 5) == 2);
    CHECK(MoveLauncherSelection(2, 10, C::PageDown, 0) == 3);
    CHECK(MoveLauncherSelection(20, 10, C::Previous, 5) == 9);
}

TEST_CASE("LauncherKeyHints mention the available shortcuts", "[launcher]")
{
    CHECK(LauncherKeyHints(LauncherPage::Recent).contains("Del"));
    CHECK(LauncherKeyHints(LauncherPage::Recent).contains("Esc"));
    CHECK_FALSE(LauncherKeyHints(LauncherPage::Templates).contains("Del"));
    CHECK(LauncherKeyHints(LauncherPage::Settings).contains("Ctrl+N"));
}

TEST_CASE("DrawLauncher applies keyboard commands", "[launcher]")
{
    ImGuiContext* context = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(760.0f, 480.0f);
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.IniFilename = nullptr;

    LauncherData data;
    data.Recents = {{"/s/a.yaml", "A", ""}, {"/s/b.yaml", "B", ""}, {"/s/c.yaml", "C", ""}};
    data.Templates = {{"Empty", "/t/empty.yaml", ""}, {"Orbit", "/t/orbit.yaml", ""}};

    const auto frame = [&](const std::vector<ImGuiKey>& pressed, const bool ctrl = false)
    {
        io.AddKeyEvent(ImGuiMod_Ctrl, ctrl);
        for (const auto key : pressed) io.AddKeyEvent(key, true);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("launcher", nullptr, ImGuiWindowFlags_NoDecoration);
        const auto action = DrawLauncher(data);
        ImGui::End();
        ImGui::Render();
        for (const auto key : pressed) io.AddKeyEvent(key, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        return action;
    };

    frame({});
    CHECK(frame({ImGuiKey_DownArrow}).Kind == LauncherAction::None);
    CHECK(data.Selected == 1);
    frame({});
    const auto opened = frame({ImGuiKey_Enter});
    CHECK(opened.Kind == LauncherAction::OpenRecent);
    CHECK(opened.Path == "/s/b.yaml");
    frame({});

    CHECK(frame({ImGuiKey_N}, true).Kind == LauncherAction::NewItem);
    frame({});
    CHECK(frame({ImGuiKey_O}, true).Kind == LauncherAction::OpenFile);
    frame({});
    CHECK(frame({ImGuiKey_F5}).Kind == LauncherAction::Rescan);
    frame({});

    CHECK(frame({ImGuiKey_Delete}).Kind == LauncherAction::None);
    CHECK(data.PendingRemove == "/s/b.yaml");
    frame({});
    CHECK(frame({ImGuiKey_Escape}).Kind == LauncherAction::None);
    CHECK(data.PendingRemove.empty());
    frame({});
    CHECK(frame({ImGuiKey_Delete}).Kind == LauncherAction::None);
    frame({});
    const auto removed = frame({ImGuiKey_Enter});
    CHECK(removed.Kind == LauncherAction::RemoveRecent);
    CHECK(removed.Path == "/s/b.yaml");
    frame({});

    CHECK(frame({ImGuiKey_Tab}).Kind == LauncherAction::None);
    CHECK(data.Page == LauncherPage::Templates);
    frame({});
    frame({ImGuiKey_DownArrow});
    frame({});
    const auto templ = frame({ImGuiKey_Enter});
    CHECK(templ.Kind == LauncherAction::OpenTemplate);
    CHECK(templ.Path == "/t/orbit.yaml");
    frame({});

    CHECK(frame({ImGuiKey_Escape}).Kind == LauncherAction::Quit);
    ImGui::DestroyContext(context);
}

TEST_CASE("DrawLauncher Escape clears a typed filter before closing", "[launcher]")
{
    ImGuiContext* context = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(760.0f, 480.0f);
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.IniFilename = nullptr;

    LauncherData data;
    data.Recents = {{"/s/alpha.yaml", "alpha", ""}, {"/s/beta.yaml", "beta", ""}};

    const auto frame = [&](const std::optional<ImGuiKey> pressed = std::nullopt)
    {
        if (pressed) io.AddKeyEvent(*pressed, true);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("launcher", nullptr, ImGuiWindowFlags_NoDecoration);
        const auto action = DrawLauncher(data);
        ImGui::End();
        ImGui::Render();
        if (pressed) io.AddKeyEvent(*pressed, false);
        return action;
    };

    frame();
    frame();
    io.AddInputCharactersUTF8("bet");
    frame();
    frame();
    CHECK(data.Filter == "bet");

    CHECK(frame(ImGuiKey_Escape).Kind == LauncherAction::None);
    frame();
    frame();
    CHECK(data.Filter.empty());
    CHECK(frame().Kind == LauncherAction::None);

    io.AddInputCharactersUTF8("al");
    frame();
    frame();
    CHECK(data.Filter == "al");
    ImGui::DestroyContext(context);
}

TEST_CASE("DrawLauncher runs a frame without a backend", "[launcher]")
{
    ImGuiContext* context = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(760.0f, 480.0f);
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.IniFilename = nullptr;

    LauncherData data;
    data.Branding = {"App", "Tagline", "1.0", 0};
    data.Recents = {{"/s/a.yaml", "A", "today"}, {"/s/b.yaml", "", ""}};
    data.Templates = {{"Empty", "/t/empty.yaml", ""}};
    bool settingsDrawn = false;
    LauncherHooks hooks;
    hooks.DrawSettings = [&] { settingsDrawn = true; };
    hooks.ExtraButtons = {{"Docs", "docs"}};
    data.Notice = {"Last run crashed", "Open report", "notice:open", "notice:dismiss"};

    for (const auto page : {LauncherPage::Recent, LauncherPage::Templates, LauncherPage::Settings})
    {
        data.Page = page;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("launcher", nullptr, ImGuiWindowFlags_NoDecoration);
        const auto action = DrawLauncher(data, hooks);
        ImGui::End();
        ImGui::Render();
        CHECK(action.Kind == LauncherAction::None);
    }

    CHECK(settingsDrawn);
    ImGui::DestroyContext(context);
}

TEST_CASE("DrawLauncher draws several notices with custom dismiss labels", "[launcher]")
{
    ImGuiContext* context = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(760.0f, 480.0f);
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.IniFilename = nullptr;

    LauncherData data;
    data.Notices = {{"Recover unsaved changes: A (just now)", "Restore", "recover:restore:a", "recover:discard:a", "Discard"},
                    {"Recover unsaved changes: B (1 minute ago)", "Restore", "recover:restore:b", "recover:discard:b", "Discard"},
                    {}};

    for (int frame = 0; frame < 3; ++frame)
    {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("launcher", nullptr, ImGuiWindowFlags_NoDecoration);
        const auto action = DrawLauncher(data);
        ImGui::End();
        ImGui::Render();
        CHECK(action.Kind == LauncherAction::None);
    }
    ImGui::DestroyContext(context);
}
