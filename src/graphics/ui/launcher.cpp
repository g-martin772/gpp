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

        float RowHeight()
        {
            const auto& style = ImGui::GetStyle();
            return ImGui::GetTextLineHeight() * 2.0f + style.ItemSpacing.y + style.FramePadding.y * 2.0f;
        }

        std::vector<std::size_t> VisibleTemplates(const LauncherData& data)
        {
            std::vector<std::size_t> result;
            for (std::size_t i = 0; i < data.Templates.size(); ++i)
            {
                const auto& item = data.Templates[i];
                if (LauncherMatches(item.Name + ' ' + item.Path, data.Filter)) result.push_back(i);
            }
            return result;
        }

        float ListHeight(const LauncherData& data)
        {
            const auto text = LauncherKeyHints(data.Page);
            const float hints = ImGui::CalcTextSize(text.c_str(), nullptr, false, ImGui::GetContentRegionAvail().x).y;
            return std::max(1.0f, ImGui::GetContentRegionAvail().y - hints - ImGui::GetStyle().ItemSpacing.y);
        }

        struct RowResult
        {
            bool Clicked = false;
            bool Opened = false;
            bool Removed = false;
        };

        RowResult DrawRow(const char* id, const std::string& name, const std::string& detail,
                          const std::string& trailing, const bool selected, const bool removable,
                          const ImU32 accent = 0, const bool ensureVisible = false)
        {
            RowResult result;
            const auto& style = ImGui::GetStyle();
            const float height = RowHeight();
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
            if (selected && accent != 0)
            {
                ImGui::GetWindowDrawList()->AddRectFilled(min, ImVec2(min.x + 3.0f, max.y), accent);
                ImGui::GetWindowDrawList()->AddRect(min, max, (accent & 0x00FFFFFFu) | 0x80000000u);
            }
            if (selected && ensureVisible)
            {
                const ImVec2 windowMin = ImGui::GetWindowPos();
                if (min.y < windowMin.y || max.y > windowMin.y + ImGui::GetWindowHeight()) ImGui::SetScrollHereY(0.5f);
            }

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
                const float size = ImGui::GetFontSize() * 4.0f;
                ImGui::Image(ImTextureRef(static_cast<ImTextureID>(branding.IconTexture)), ImVec2(size, size));
                ImGui::Dummy(ImVec2(0.0f, ImGui::GetFontSize() * 0.4f));
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

        void DrawNotice(const LauncherNotice& notice, LauncherAction& action, const int index = 0)
        {
            if (notice.Text.empty()) return;
            ImGui::PushID(index);
            const auto& style = ImGui::GetStyle();
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.45f, 0.18f, 0.12f, 0.55f));
            const float height = ImGui::GetFrameHeight() * 2.0f + style.WindowPadding.y * 2.0f + style.ItemSpacing.y;
            if (ImGui::BeginChild("##launcher_notice", ImVec2(0.0f, height),
                                  ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar))
            {
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(notice.Text.c_str());
                ImGui::PopTextWrapPos();
                if (!notice.ActionId.empty() && ImGui::SmallButton(notice.ActionLabel.empty() ? "Open" : notice.ActionLabel.c_str()))
                {
                    action = {LauncherAction::Custom, notice.ActionId};
                }
                if (!notice.DismissId.empty())
                {
                    ImGui::SameLine();
                    if (ImGui::SmallButton(notice.DismissLabel.empty() ? "Dismiss" : notice.DismissLabel.c_str()))
                        action = {LauncherAction::Custom, notice.DismissId};
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::Spacing();
            ImGui::PopID();
        }

        void DrawFilterBox(LauncherData& data)
        {
            std::array<char, 256> buffer{};
            const auto length = std::min(data.Filter.size(), buffer.size() - 1);
            std::copy_n(data.Filter.data(), length, buffer.data());
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::IsWindowAppearing() || data.FocusFilter) ImGui::SetKeyboardFocusHere();
            data.FocusFilter = false;
            if (ImGui::InputTextWithHint("##filter", "Search", buffer.data(), buffer.size(),
                                         ImGuiInputTextFlags_EscapeClearsAll))
            {
                data.Filter = buffer.data();
                data.Selected = 0;
                data.SelectedTemplate = 0;
            }
            data.FilterFocused = ImGui::IsItemActive();
            ImGui::Spacing();
        }

        void DrawRemoveConfirm(LauncherData& data, LauncherAction& action)
        {
            if (data.PendingRemove.empty()) return;
            const auto& style = ImGui::GetStyle();
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.45f, 0.18f, 0.12f, 0.55f));
            const float height = ImGui::GetFrameHeight() * 2.0f + style.WindowPadding.y * 2.0f + style.ItemSpacing.y;
            if (ImGui::BeginChild("##launcher_remove", ImVec2(0.0f, height),
                                  ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar))
            {
                ImGui::TextUnformatted("Remove from the recent list? The file itself is kept.");
                if (ImGui::SmallButton("Remove (Enter)"))
                {
                    action = {LauncherAction::RemoveRecent, data.PendingRemove};
                    data.PendingRemove.clear();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Cancel (Esc)")) data.PendingRemove.clear();
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::Spacing();
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

            DrawFilterBox(data);
            DrawRemoveConfirm(data, action);

            const auto visible = FilterLauncherRecents(data.Recents, data.Filter);
            if (visible.empty())
            {
                ImGui::TextDisabled("%s", data.Recents.empty() ? data.EmptyText.c_str() : "No matches.");
                return;
            }
            data.Selected = std::min(data.Selected, visible.size() - 1);

            const ImU32 accent = ImGui::ColorConvertFloat4ToU32(AccentColor(data.Style));
            ImGui::BeginChild("##recents", ImVec2(0.0f, ListHeight(data)), ImGuiChildFlags_None);
            data.PageRows = std::max<std::size_t>(
                1, static_cast<std::size_t>(ImGui::GetContentRegionAvail().y / (RowHeight() + style.ItemSpacing.y)));
            for (std::size_t row = 0; row < visible.size(); ++row)
            {
                const auto& recent = data.Recents[visible[row]];
                const std::string name = recent.Name.empty() ? std::filesystem::path(recent.Path).stem().string() : recent.Name;
                const auto result = DrawRow(recent.Path.c_str(), name, recent.Path, recent.Subtitle,
                                            row == data.Selected, true, accent, data.ScrollToSelected);
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
            DrawFilterBox(data);
            const auto visible = VisibleTemplates(data);
            if (visible.empty())
            {
                ImGui::TextDisabled("No matches.");
                return;
            }
            data.SelectedTemplate = std::min(data.SelectedTemplate, visible.size() - 1);

            const ImU32 accent = ImGui::ColorConvertFloat4ToU32(AccentColor(data.Style));
            ImGui::BeginChild("##templates", ImVec2(0.0f, ListHeight(data)), ImGuiChildFlags_None);
            data.PageRows = std::max<std::size_t>(
                1, static_cast<std::size_t>(ImGui::GetContentRegionAvail().y / (RowHeight() + ImGui::GetStyle().ItemSpacing.y)));
            for (std::size_t row = 0; row < visible.size(); ++row)
            {
                const auto& item = data.Templates[visible[row]];
                const auto result = DrawRow(item.Path.c_str(), item.Name,
                                            item.Description.empty() ? item.Path : item.Description, {},
                                            row == data.SelectedTemplate, false, accent, data.ScrollToSelected);
                if (result.Clicked || result.Opened) action = {LauncherAction::OpenTemplate, item.Path};
            }
            ImGui::EndChild();
        }

        void DrawKeyHints(const LauncherData& data)
        {
            const auto text = LauncherKeyHints(data.Page);
            const float height = ImGui::CalcTextSize(text.c_str(), nullptr, false, ImGui::GetContentRegionAvail().x).y;
            const float y = ImGui::GetWindowHeight() - ImGui::GetStyle().WindowPadding.y - height;
            if (ImGui::GetCursorPosY() < y) ImGui::SetCursorPosY(y);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("%s", text.c_str());
            ImGui::PopTextWrapPos();
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

        std::size_t ItemCount(const LauncherData& data)
        {
            switch (data.Page)
            {
                case LauncherPage::Recent: return FilterLauncherRecents(data.Recents, data.Filter).size();
                case LauncherPage::Templates: return VisibleTemplates(data).size();
                default: return 0;
            }
        }

        LauncherKeyCommand PollKeys(const LauncherData& data)
        {
            const auto& io = ImGui::GetIO();
            if (io.KeyAlt || io.KeySuper) return LauncherKeyCommand::None;

            struct Binding { ImGuiKey Source; LauncherKey Key; bool Repeat; };
            static constexpr std::array bindings{
                Binding{ImGuiKey_UpArrow, LauncherKey::Up, true}, Binding{ImGuiKey_DownArrow, LauncherKey::Down, true},
                Binding{ImGuiKey_PageUp, LauncherKey::PageUp, true}, Binding{ImGuiKey_PageDown, LauncherKey::PageDown, true},
                Binding{ImGuiKey_LeftArrow, LauncherKey::Left, false}, Binding{ImGuiKey_RightArrow, LauncherKey::Right, false},
                Binding{ImGuiKey_Home, LauncherKey::Home, false}, Binding{ImGuiKey_End, LauncherKey::End, false},
                Binding{ImGuiKey_Enter, LauncherKey::Enter, false}, Binding{ImGuiKey_KeypadEnter, LauncherKey::Enter, false},
                Binding{ImGuiKey_Escape, LauncherKey::Escape, false}, Binding{ImGuiKey_Tab, LauncherKey::Tab, false},
                Binding{ImGuiKey_Delete, LauncherKey::Delete, false}, Binding{ImGuiKey_F5, LauncherKey::F5, false},
                Binding{ImGuiKey_Slash, LauncherKey::Slash, false}, Binding{ImGuiKey_N, LauncherKey::N, false},
                Binding{ImGuiKey_O, LauncherKey::O, false}, Binding{ImGuiKey_R, LauncherKey::R, false},
                Binding{ImGuiKey_F, LauncherKey::F, false}};

            const LauncherKeyState state{.Page = data.Page, .TextActive = io.WantTextInput, .FilterFocused = data.FilterFocused,
                                         .FilterEmpty = data.Filter.empty(), .ModalOpen = !data.PendingRemove.empty(),
                                         .ItemCount = ItemCount(data)};
            for (const auto& binding : bindings)
            {
                if (!ImGui::IsKeyPressed(binding.Source, binding.Repeat)) continue;
                const auto command = ResolveLauncherKey({binding.Key, io.KeyCtrl, io.KeyShift}, state);
                if (command != LauncherKeyCommand::None) return command;
            }
            return LauncherKeyCommand::None;
        }

        void ApplyKey(LauncherData& data, LauncherAction& action, const LauncherKeyCommand command)
        {
            using C = LauncherKeyCommand;
            const auto cyclePage = [&](const int step)
            {
                constexpr int pages = 3;
                data.Page = static_cast<LauncherPage>((static_cast<int>(data.Page) + step + pages) % pages);
                data.ScrollToSelected = true;
            };
            switch (command)
            {
                case C::Previous: case C::Next: case C::First: case C::Last: case C::PageUp: case C::PageDown:
                {
                    auto& selected = data.Page == LauncherPage::Templates ? data.SelectedTemplate : data.Selected;
                    selected = MoveLauncherSelection(selected, ItemCount(data), command, data.PageRows);
                    data.ScrollToSelected = true;
                    break;
                }
                case C::Activate:
                {
                    if (data.Page == LauncherPage::Recent)
                    {
                        const auto visible = FilterLauncherRecents(data.Recents, data.Filter);
                        if (!visible.empty())
                        {
                            action = {LauncherAction::OpenRecent, data.Recents[visible[std::min(data.Selected, visible.size() - 1)]].Path};
                        }
                    }
                    else if (data.Page == LauncherPage::Templates)
                    {
                        const auto visible = VisibleTemplates(data);
                        if (!visible.empty())
                        {
                            action = {LauncherAction::OpenTemplate,
                                      data.Templates[visible[std::min(data.SelectedTemplate, visible.size() - 1)]].Path};
                        }
                    }
                    break;
                }
                case C::Remove: case C::RemoveNow:
                {
                    const auto visible = FilterLauncherRecents(data.Recents, data.Filter);
                    if (visible.empty()) break;
                    const auto& path = data.Recents[visible[std::min(data.Selected, visible.size() - 1)]].Path;
                    if (command == C::RemoveNow) action = {LauncherAction::RemoveRecent, path};
                    else data.PendingRemove = path;
                    break;
                }
                case C::NextPage: cyclePage(1); break;
                case C::PreviousPage: cyclePage(-1); break;
                case C::New: action.Kind = LauncherAction::NewItem; break;
                case C::Open: action.Kind = LauncherAction::OpenFile; break;
                case C::Rescan: action.Kind = LauncherAction::Rescan; break;
                case C::Close: action.Kind = LauncherAction::Quit; break;
                case C::FocusFilter:
                    if (data.Page == LauncherPage::Settings) data.Page = LauncherPage::Recent;
                    data.FocusFilter = true;
                    ImGui::GetIO().InputQueueCharacters.resize(0);
                    break;
                case C::ClearFilter:
                    if (data.FilterFocused) break;
                    data.Filter.clear();
                    data.Selected = 0;
                    data.SelectedTemplate = 0;
                    break;
                case C::ConfirmRemove:
                    action = {LauncherAction::RemoveRecent, data.PendingRemove};
                    data.PendingRemove.clear();
                    break;
                case C::CancelRemove: data.PendingRemove.clear(); break;
                case C::None: break;
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

    LauncherKeyCommand ResolveLauncherKey(const LauncherKeyEvent& event, const LauncherKeyState& state)
    {
        using C = LauncherKeyCommand;
        using K = LauncherKey;
        if (state.ModalOpen)
        {
            if (event.Key == K::Enter) return C::ConfirmRemove;
            if (event.Key == K::Escape) return C::CancelRemove;
            return C::None;
        }
        if (state.TextActive && !state.FilterFocused) return C::None;

        const bool list = state.Page != LauncherPage::Settings;
        const bool hasItems = list && state.ItemCount > 0;
        const bool typing = state.FilterFocused && !state.FilterEmpty;

        if (event.Ctrl)
        {
            switch (event.Key)
            {
                case K::N: return C::New;
                case K::O: return C::Open;
                case K::R: return C::Rescan;
                case K::F: return C::FocusFilter;
                case K::Tab: return event.Shift ? C::PreviousPage : C::NextPage;
                default: return C::None;
            }
        }

        switch (event.Key)
        {
            case K::Up: return hasItems ? C::Previous : C::None;
            case K::Down: return hasItems ? C::Next : C::None;
            case K::PageUp: return hasItems ? C::PageUp : C::None;
            case K::PageDown: return hasItems ? C::PageDown : C::None;
            case K::Home: return hasItems && !typing ? C::First : C::None;
            case K::End: return hasItems && !typing ? C::Last : C::None;
            case K::Left: return typing ? C::None : C::PreviousPage;
            case K::Right: return typing ? C::None : C::NextPage;
            case K::Tab: return event.Shift ? C::PreviousPage : C::NextPage;
            case K::Enter: return hasItems ? C::Activate : C::None;
            case K::Escape: return list && !state.FilterEmpty ? C::ClearFilter : C::Close;
            case K::Delete:
                if (state.Page != LauncherPage::Recent || !hasItems || typing) return C::None;
                return event.Shift ? C::RemoveNow : C::Remove;
            case K::F5: return C::Rescan;
            case K::Slash: return state.TextActive ? C::None : C::FocusFilter;
            default: return C::None;
        }
    }

    std::size_t MoveLauncherSelection(const std::size_t selected, const std::size_t count,
                                      const LauncherKeyCommand command, const std::size_t pageRows)
    {
        if (count == 0) return 0;
        const std::size_t last = count - 1;
        const std::size_t page = std::max<std::size_t>(pageRows, 1);
        switch (command)
        {
            case LauncherKeyCommand::Previous: return selected > 0 ? std::min(selected, count) - 1 : 0;
            case LauncherKeyCommand::Next: return std::min(selected + 1, last);
            case LauncherKeyCommand::First: return 0;
            case LauncherKeyCommand::Last: return last;
            case LauncherKeyCommand::PageUp: return selected - std::min(selected, page);
            case LauncherKeyCommand::PageDown: return std::min(selected + page, last);
            default: return std::min(selected, last);
        }
    }

    std::string LauncherKeyHints(const LauncherPage page)
    {
        switch (page)
        {
            case LauncherPage::Recent:
                return "Up/Down/PgUp/PgDn/Home/End select  |  Enter open  |  Del remove (Shift+Del no confirm)  |  "
                       "Ctrl+N new  |  Ctrl+O open file  |  / search  |  Tab/Left/Right section  |  F5 rescan  |  Esc close";
            case LauncherPage::Templates:
                return "Up/Down/PgUp/PgDn/Home/End select  |  Enter open  |  Ctrl+N new  |  Ctrl+O open file  |  "
                       "/ search  |  Tab/Left/Right section  |  F5 rescan  |  Esc close";
            default: return "Tab/Left/Right section  |  Ctrl+N new  |  Ctrl+O open file  |  Esc close";
        }
    }

    LauncherAction DrawLauncher(LauncherData& data, const LauncherHooks& hooks)
    {
        LauncherAction action;
        ApplyKey(data, action, PollKeys(data));
        if (data.Page != LauncherPage::Recent) data.PendingRemove.clear();
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
            DrawNotice(data.Notice, action);
            for (std::size_t i = 0; i < data.Notices.size(); ++i) DrawNotice(data.Notices[i], action, static_cast<int>(i) + 1);
            switch (data.Page)
            {
                case LauncherPage::Recent: DrawRecentPage(data, action); break;
                case LauncherPage::Templates: DrawTemplatesPage(data, action); break;
                case LauncherPage::Settings: DrawSettingsPage(data, hooks); data.FilterFocused = false; break;
            }
            data.ScrollToSelected = false;
            DrawKeyHints(data);
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        return action;
    }
}
