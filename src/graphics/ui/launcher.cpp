module GPP.Graphics;

import :UI.Launcher;
import std;

namespace GPP
{
    namespace
    {
        char Lower(const char c)
        {
            return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }

        bool ContainsNoCase(std::string_view text, std::string_view term)
        {
            if (term.empty()) return true;
            const auto it = std::ranges::search(text, term, [](const char a, const char b) { return Lower(a) == Lower(b); });
            return !it.empty();
        }

        ImVec4 AccentColor(const LauncherStyle& style)
        {
            if (style.Accent)
            {
                const auto& c = *style.Accent;
                return ImVec4(c[0], c[1], c[2], c[3]);
            }
            return ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
        }

        bool IsEnterPressed()
        {
            return ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
        }

        struct RowResult
        {
            bool Clicked = false;
            bool Opened = false;
            bool Removed = false;
        };

        RowResult DrawRow(const char* id, const std::string& name, const std::string& detail,
                          const std::string& trailing, const bool selected, const bool removable)
        {
            RowResult result;
            const auto& style = ImGui::GetStyle();
            const float height = ImGui::GetTextLineHeight() * 2.0f + style.ItemSpacing.y + style.FramePadding.y * 2.0f;
            const float width = ImGui::GetContentRegionAvail().x;
            const ImVec2 min = ImGui::GetCursorScreenPos();
            const ImVec2 max(min.x + width, min.y + height);

            ImGui::PushID(id);
            ImGui::SetNextItemAllowOverlap();
            if (ImGui::Selectable("##row", selected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(width, height)))
            {
                result.Clicked = true;
                if (ImGui::IsMouseDoubleClicked(0)) result.Opened = true;
            }
            if (ImGui::IsItemFocused() && IsEnterPressed()) result.Opened = true;

            const bool rowHovered = ImGui::IsMouseHoveringRect(min, max);
            auto* draw = ImGui::GetWindowDrawList();
            const float padX = style.FramePadding.x + 4.0f;
            const float padY = style.FramePadding.y;
            const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
            const ImU32 dimColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);

            const float buttonWidth = removable ? ImGui::CalcTextSize("x").x + style.FramePadding.x * 2.0f : 0.0f;
            float right = max.x - padX - (removable ? buttonWidth + style.ItemSpacing.x : 0.0f);
            if (!trailing.empty())
            {
                const float w = ImGui::CalcTextSize(trailing.c_str()).x;
                draw->AddText(ImVec2(right - w, min.y + padY), dimColor, trailing.c_str());
                right -= w + style.ItemSpacing.x;
            }

            draw->PushClipRect(min, ImVec2(std::max(min.x, right), max.y), true);
            const ImVec2 nameStart(min.x + padX, min.y + padY);
            draw->AddText(nameStart, textColor, name.c_str());
            draw->AddText(ImVec2(nameStart.x + 0.6f, nameStart.y), textColor, name.c_str());
            draw->AddText(ImVec2(nameStart.x, nameStart.y + ImGui::GetTextLineHeight() + style.ItemSpacing.y * 0.5f),
                          dimColor, detail.c_str());
            draw->PopClipRect();

            if (removable && rowHovered)
            {
                const float buttonHeight = ImGui::GetFrameHeight();
                ImGui::SetCursorScreenPos(ImVec2(max.x - padX - buttonWidth, min.y + (height - buttonHeight) * 0.5f));
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                if (ImGui::SmallButton("x")) result.Removed = true;
                ImGui::PopStyleColor();
            }

            ImGui::PopID();
            ImGui::SetCursorScreenPos(ImVec2(min.x, max.y));
            ImGui::Dummy(ImVec2(0.0f, 0.0f));
            return result;
        }

        void Heading(const char* text)
        {
            ImGui::PushFont(nullptr, ImGui::GetFontSize() * 1.5f);
            ImGui::TextUnformatted(text);
            ImGui::PopFont();
        }

        bool NavEntry(const char* label, const bool active, const ImVec4& accent, const float width)
        {
            const float height = ImGui::GetFrameHeight() * 1.15f;
            const ImVec2 min = ImGui::GetCursorScreenPos();
            const bool clicked = ImGui::Selectable((std::string("##nav_") + label).c_str(), active, 0, ImVec2(width, height));
            auto* drawList = ImGui::GetWindowDrawList();
            if (active)
            {
                drawList->AddRectFilled(
                    min, ImVec2(min.x + 3.0f, min.y + height), ImGui::ColorConvertFloat4ToU32(accent));
            }
            const float inset = ImGui::GetFontSize() * 0.8f;
            drawList->AddText(ImVec2(min.x + inset, min.y + (height - ImGui::GetTextLineHeight()) * 0.5f),
                              ImGui::GetColorU32(ImGuiCol_Text), label);
            return clicked;
        }

        void DrawSidebar(LauncherData& data, const LauncherHooks& hooks, LauncherAction& action)
        {
            const ImVec4 accent = AccentColor(data.Style);
            const float width = ImGui::GetContentRegionAvail().x;
            const auto& branding = data.Branding;

            if (branding.IconTexture != 0)
            {
                const float size = ImGui::GetFontSize() * 3.0f;
                ImGui::Image(ImTextureRef(static_cast<ImTextureID>(branding.IconTexture)), ImVec2(size, size));
                ImGui::SameLine();
            }
            ImGui::BeginGroup();
            ImGui::PushFont(nullptr, ImGui::GetFontSize() * 1.6f);
            ImGui::TextUnformatted(branding.Title.c_str());
            ImGui::PopFont();
            if (!branding.Version.empty()) ImGui::TextDisabled("%s", branding.Version.c_str());
            ImGui::EndGroup();

            if (!branding.Tagline.empty())
            {
                ImGui::Spacing();
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextDisabled("%s", branding.Tagline.c_str());
                ImGui::PopTextWrapPos();
            }
            ImGui::Spacing();
            ImGui::Spacing();

            struct Entry { const char* Label; LauncherPage Page; };
            for (const Entry& entry : {Entry{"Recent", LauncherPage::Recent}, Entry{"Templates", LauncherPage::Templates},
                                       Entry{"Settings", LauncherPage::Settings}})
            {
                if (NavEntry(entry.Label, data.Page == entry.Page, accent, width)) data.Page = entry.Page;
            }

            const float bottom = ImGui::GetFrameHeightWithSpacing() * (1 + static_cast<float>(hooks.ExtraButtons.size()));
            const float remaining = ImGui::GetContentRegionAvail().y - bottom;
            if (remaining > 0.0f) ImGui::Dummy(ImVec2(0.0f, remaining));

            for (const auto& button : hooks.ExtraButtons)
            {
                if (ImGui::Button(button.Label.c_str(), ImVec2(width, 0.0f)))
                {
                    action = {LauncherAction::Custom, button.Id};
                }
            }
            if (ImGui::Button("Quit", ImVec2(width, 0.0f))) action.Kind = LauncherAction::Quit;
        }

        void DrawRecentPage(LauncherData& data, LauncherAction& action)
        {
            Heading("Recent");
            const auto& style = ImGui::GetStyle();
            const float buttons = ImGui::CalcTextSize(data.NewLabel.c_str()).x + ImGui::CalcTextSize(data.OpenLabel.c_str()).x
                + style.FramePadding.x * 4.0f + style.ItemSpacing.x * 2.0f;

            ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - buttons));
            if (ImGui::Button(data.NewLabel.c_str())) action.Kind = LauncherAction::NewItem;
            ImGui::SameLine();
            if (ImGui::Button(data.OpenLabel.c_str())) action.Kind = LauncherAction::OpenFile;
            ImGui::Spacing();

            std::array<char, 256> buffer{};
            const auto length = std::min(data.Filter.size(), buffer.size() - 1);
            std::copy_n(data.Filter.data(), length, buffer.data());
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            if (ImGui::InputTextWithHint("##filter", "Search", buffer.data(), buffer.size()))
            {
                data.Filter = buffer.data();
                data.Selected = 0;
            }
            const bool filterActive = ImGui::IsItemActive();
            ImGui::Spacing();

            const auto visible = FilterLauncherRecents(data.Recents, data.Filter);
            if (visible.empty())
            {
                ImGui::TextDisabled("%s", data.Recents.empty() ? data.EmptyText.c_str() : "No matches.");
                return;
            }
            data.Selected = std::min(data.Selected, visible.size() - 1);

            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && data.Selected + 1 < visible.size()) ++data.Selected;
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && data.Selected > 0) --data.Selected;
            if (filterActive && IsEnterPressed())
            {
                action = {LauncherAction::OpenRecent, data.Recents[visible[data.Selected]].Path};
            }

            ImGui::BeginChild("##recents", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
            for (std::size_t row = 0; row < visible.size(); ++row)
            {
                const auto& recent = data.Recents[visible[row]];
                const std::string name = recent.Name.empty() ? std::filesystem::path(recent.Path).stem().string() : recent.Name;
                const auto result = DrawRow(recent.Path.c_str(), name, recent.Path, recent.Subtitle,
                                            row == data.Selected, true);
                if (result.Clicked) data.Selected = row;
                if (result.Opened) action = {LauncherAction::OpenRecent, recent.Path};
                if (result.Removed) action = {LauncherAction::RemoveRecent, recent.Path};
            }
            ImGui::EndChild();
        }

        void DrawTemplatesPage(LauncherData& data, LauncherAction& action)
        {
            Heading("Templates");
            ImGui::Spacing();
            if (data.Templates.empty())
            {
                ImGui::TextDisabled("No templates found.");
                return;
            }
            ImGui::BeginChild("##templates", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
            for (const auto& item : data.Templates)
            {
                const auto result = DrawRow(item.Path.c_str(), item.Name,
                                            item.Description.empty() ? item.Path : item.Description, {}, false, false);
                if (result.Clicked || result.Opened) action = {LauncherAction::OpenTemplate, item.Path};
            }
            ImGui::EndChild();
        }

        void DrawSettingsPage(LauncherData& data, const LauncherHooks& hooks)
        {
            Heading("Settings");
            ImGui::Spacing();
            ImGui::Checkbox("Show launcher on startup", &data.ShowOnStartup);
            if (hooks.DrawSettings)
            {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                hooks.DrawSettings();
            }
        }
    }

    bool LauncherMatches(const std::string_view text, const std::string_view filter)
    {
        std::size_t pos = 0;
        while (pos < filter.size())
        {
            while (pos < filter.size() && std::isspace(static_cast<unsigned char>(filter[pos]))) ++pos;
            std::size_t end = pos;
            while (end < filter.size() && !std::isspace(static_cast<unsigned char>(filter[end]))) ++end;
            if (end > pos && !ContainsNoCase(text, filter.substr(pos, end - pos))) return false;
            pos = end;
        }
        return true;
    }

    std::vector<std::size_t> FilterLauncherRecents(const std::vector<LauncherRecent>& recents, const std::string_view filter)
    {
        std::vector<std::size_t> result;
        for (std::size_t i = 0; i < recents.size(); ++i)
        {
            const auto& recent = recents[i];
            if (LauncherMatches(recent.Name, filter) || LauncherMatches(recent.Path, filter)
                || LauncherMatches(recent.Name + ' ' + recent.Path, filter))
            {
                result.push_back(i);
            }
        }
        return result;
    }

    LauncherAction DrawLauncher(LauncherData& data, const LauncherHooks& hooks)
    {
        LauncherAction action;
        const float sidebarWidth = ImGui::GetFontSize() * 14.0f;
        const float padding = ImGui::GetFontSize() * 1.1f;
        const ImVec2 avail = ImGui::GetContentRegionAvail();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding, padding));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_TitleBg));
        if (ImGui::BeginChild("##launcher_sidebar", ImVec2(sidebarWidth, avail.y), ImGuiChildFlags_AlwaysUseWindowPadding,
                              ImGuiWindowFlags_NoScrollbar))
        {
            DrawSidebar(data, hooks, action);
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::SameLine(0.0f, 0.0f);
        if (ImGui::BeginChild("##launcher_page", ImVec2(0.0f, avail.y), ImGuiChildFlags_AlwaysUseWindowPadding))
        {
            switch (data.Page)
            {
                case LauncherPage::Recent: DrawRecentPage(data, action); break;
                case LauncherPage::Templates: DrawTemplatesPage(data, action); break;
                case LauncherPage::Settings: DrawSettingsPage(data, hooks); break;
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        return action;
    }
}
