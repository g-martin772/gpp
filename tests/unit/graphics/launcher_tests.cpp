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
