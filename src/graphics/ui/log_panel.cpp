module GPP.Graphics;

import :UI.LogPanel;
import std;

namespace GPP
{
    namespace
    {
        constexpr std::size_t LevelIndex(const LogLevel level) noexcept
        {
            return std::min<std::size_t>(static_cast<std::size_t>(level), 5);
        }

        constexpr std::array<LogLevel, 6> kLevels{LogLevel::Trace, LogLevel::Debug, LogLevel::Info,
                                                   LogLevel::Warn, LogLevel::Error, LogLevel::Critical};

        ImVec4 ToImVec4(const std::array<float, 4>& color)
        {
            return ImVec4(color[0], color[1], color[2], color[3]);
        }

        std::string Lower(std::string_view text)
        {
            std::string result(text);
            std::ranges::transform(result, result.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return result;
        }

        std::string Stamp(const std::chrono::system_clock::time_point time)
        {
            const auto millis = std::chrono::floor<std::chrono::milliseconds>(time);
            try
            {
                return std::format("{:%H:%M:%S}", std::chrono::zoned_time{std::chrono::current_zone(), millis});
            }
            catch (const std::exception&)
            {
                return std::format("{:%H:%M:%S}", millis);
            }
        }
    }

    LogLevelStyle LogLevelAppearance(const LogLevel level)
    {
        switch (level)
        {
        case LogLevel::Trace: return {{0.80f, 0.80f, 0.80f, 1.0f}, false};
        case LogLevel::Debug: return {{0.30f, 0.85f, 0.85f, 1.0f}, false};
        case LogLevel::Info: return {{0.35f, 0.85f, 0.40f, 1.0f}, false};
        case LogLevel::Warn: return {{0.95f, 0.85f, 0.30f, 1.0f}, false};
        case LogLevel::Error: return {{0.95f, 0.35f, 0.35f, 1.0f}, false};
        default: return {{1.0f, 1.0f, 1.0f, 1.0f}, true};
        }
    }

    const char* LogLevelName(const LogLevel level)
    {
        switch (level)
        {
        case LogLevel::Trace: return "trace";
        case LogLevel::Debug: return "debug";
        case LogLevel::Info: return "info";
        case LogLevel::Warn: return "warning";
        case LogLevel::Error: return "error";
        case LogLevel::Critical: return "critical";
        default: return "off";
        }
    }

    void LogPanel::SetFilter(const std::string_view text)
    {
        m_FilterText.fill('\0');
        std::ranges::copy_n(text.begin(), std::min(text.size(), m_FilterText.size() - 1), m_FilterText.begin());
        m_Dirty = true;
    }

    void LogPanel::Clear()
    {
        m_Items.clear();
        m_Rows.clear();
        m_LevelCounts.fill(0);
    }

    void LogPanel::RecountLevels()
    {
        m_LevelCounts.fill(0);
        for (const auto& item : m_Items) ++m_LevelCounts[LevelIndex(item.Entry.Level)];
    }

    void LogPanel::Sync(const LogBuffer& buffer)
    {
        std::vector<LogEntry> fresh;
        m_Cursor = buffer.CopySince(m_Cursor, fresh);
        if (fresh.empty()) return;

        const auto firstNew = m_Items.size();
        for (auto& entry : fresh)
        {
            ++m_LevelCounts[LevelIndex(entry.Level)];
            auto stamp = Stamp(entry.Time);
            m_Items.push_back(Item{std::move(entry), std::move(stamp)});
        }

        if (m_Items.size() > m_MaxEntries + m_MaxEntries / 4)
        {
            m_Items.erase(m_Items.begin(), m_Items.begin() + static_cast<std::ptrdiff_t>(m_Items.size() - m_MaxEntries));
            RecountLevels();
            m_Dirty = true;
        }
        else if (!m_Dirty)
        {
            AppendRows(firstNew);
        }
    }

    bool LogPanel::Matches(const Item& item) const
    {
        if (!ShowLevel[LevelIndex(item.Entry.Level)]) return false;
        if (m_Terms.empty()) return true;
        const auto haystack = Lower(std::string(LogLevelName(item.Entry.Level)) + ' ' + item.Entry.Source + ' ' + item.Entry.Message);
        return std::ranges::all_of(m_Terms, [&](const Term& term)
        {
            return haystack.contains(term.Text) != term.Exclude;
        });
    }

    void LogPanel::AppendRows(const std::size_t firstItem)
    {
        for (auto index = firstItem; index < m_Items.size(); ++index)
        {
            const auto& item = m_Items[index];
            if (!Matches(item)) continue;
            const std::string_view message = item.Entry.Message;
            std::size_t begin = 0;
            bool first = true;
            while (true)
            {
                const auto end = message.find('\n', begin);
                const auto length = (end == std::string_view::npos ? message.size() : end) - begin;
                m_Rows.push_back(Row{static_cast<std::uint32_t>(index), static_cast<std::uint32_t>(begin),
                                     static_cast<std::uint32_t>(length), first});
                if (end == std::string_view::npos) break;
                begin = end + 1;
                first = false;
            }
        }
    }

    void LogPanel::Rebuild()
    {
        m_Terms.clear();
        std::istringstream words{std::string(m_FilterText.data())};
        for (std::string word; words >> word;)
        {
            const bool exclude = word.size() > 1 && word.front() == '-';
            if (exclude) word.erase(0, 1);
            m_Terms.push_back(Term{Lower(word), exclude});
        }
        m_Rows.clear();
        AppendRows(0);
        m_BuiltLevels = ShowLevel;
        m_Dirty = false;
    }

    std::string LogPanel::FormatLine(const Item& item) const
    {
        std::string line;
        if (ShowTime) line += std::format("[{}] ", item.Stamp);
        if (ShowSource) line += std::format("[{}] ", item.Entry.Source);
        return line + std::format("[{}] {}", LogLevelName(item.Entry.Level), item.Entry.Message);
    }

    void LogPanel::CopyVisible() const
    {
        std::string text;
        std::uint32_t lastItem = std::numeric_limits<std::uint32_t>::max();
        for (const auto& row : m_Rows)
        {
            if (row.Item == lastItem) continue;
            lastItem = row.Item;
            text += FormatLine(m_Items[row.Item]);
            text += '\n';
        }
        ImGui::SetClipboardText(text.c_str());
    }

    void LogPanel::Draw(const LogBuffer& buffer)
    {
        Sync(buffer);
        if (m_BuiltLevels != ShowLevel) m_Dirty = true;
        if (m_Dirty) Rebuild();

        const auto& style = ImGui::GetStyle();
        const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        bool firstButton = true;
        auto place = [&](const char* label)
        {
            const float width = ImGui::CalcTextSize(label, nullptr, true).x + style.FramePadding.x * 2.0f;
            if (!firstButton && ImGui::GetItemRectMax().x + style.ItemSpacing.x + width <= right) ImGui::SameLine();
            firstButton = false;
        };

        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputTextWithHint("##LogFilter", "Filter: words (all must match), -word excludes", m_FilterText.data(),
                                     m_FilterText.size()))
        {
            m_Dirty = true;
        }

        for (std::size_t i = 0; i < kLevels.size(); ++i)
        {
            const auto appearance = LogLevelAppearance(kLevels[i]);
            const auto label = std::format("{} {}###level{}", LogLevelName(kLevels[i]), m_LevelCounts[i], i);
            place(label.c_str());
            const bool on = ShowLevel[i];
            ImGui::PushStyleColor(ImGuiCol_Text, on ? ToImVec4(appearance.Color) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));
            else ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Button));
            if (ImGui::SmallButton(label.c_str())) ShowLevel[i] = !ShowLevel[i];
            ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show or hide %s messages", LogLevelName(kLevels[i]));
        }

        place("Clear");
        if (ImGui::SmallButton("Clear")) Clear();
        place("Copy");
        if (ImGui::SmallButton("Copy"))
        {
            CopyVisible();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copy the visible (filtered) lines");
        place("Follow");
        ImGui::PushStyleColor(ImGuiCol_Button, m_Follow ? ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive)
                                                         : ImGui::GetStyleColorVec4(ImGuiCol_Button));
        if (ImGui::SmallButton("Follow")) m_Follow = !m_Follow;
        ImGui::PopStyleColor();
        place("Time");
        ImGui::PushStyleColor(ImGuiCol_Button, ShowTime ? ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive)
                                                         : ImGui::GetStyleColorVec4(ImGuiCol_Button));
        if (ImGui::SmallButton("Time")) ShowTime = !ShowTime;
        ImGui::PopStyleColor();
        place("Source");
        ImGui::PushStyleColor(ImGuiCol_Button, ShowSource ? ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive)
                                                           : ImGui::GetStyleColorVec4(ImGuiCol_Button));
        if (ImGui::SmallButton("Source")) ShowSource = !ShowSource;
        ImGui::PopStyleColor();

        ImGui::Separator();
        if (!ImGui::BeginChild("##LogRows", ImVec2(0, 0), ImGuiChildFlags_None,
                               ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoNav))
        {
            ImGui::EndChild();
            return;
        }

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 1.0f));
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(m_Rows.size()));
        const ImU32 backdrop = ImGui::GetColorU32(ImVec4(0.80f, 0.15f, 0.15f, 1.0f));
        while (clipper.Step())
        {
            for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
            {
                const auto& row = m_Rows[static_cast<std::size_t>(index)];
                const auto& item = m_Items[row.Item];
                const char* text = item.Entry.Message.data() + row.Begin;
                if (!row.First)
                {
                    ImGui::TextUnformatted(text, text + row.Length);
                    continue;
                }
                bool sameLine = false;
                auto segment = [&](const std::string& piece, const ImVec4* color)
                {
                    if (sameLine) ImGui::SameLine(0.0f, 0.0f);
                    sameLine = true;
                    if (color) ImGui::TextColored(*color, "%s", piece.c_str());
                    else ImGui::TextUnformatted(piece.c_str());
                };
                const ImVec4 dim = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
                if (ShowTime) segment(std::format("[{}] ", item.Stamp), &dim);
                if (ShowSource) segment(std::format("[{}] ", item.Entry.Source), &dim);
                const auto appearance = LogLevelAppearance(item.Entry.Level);
                const auto color = ToImVec4(appearance.Color);
                segment("[", nullptr);
                if (appearance.Background)
                {
                    const auto name = std::string(LogLevelName(item.Entry.Level));
                    ImGui::SameLine(0.0f, 0.0f);
                    const auto position = ImGui::GetCursorScreenPos();
                    const auto size = ImGui::CalcTextSize(name.c_str());
                    ImGui::GetWindowDrawList()->AddRectFilled(position, ImVec2(position.x + size.x, position.y + size.y), backdrop);
                    ImGui::TextColored(color, "%s", name.c_str());
                }
                else
                {
                    segment(LogLevelName(item.Entry.Level), &color);
                }
                segment("] ", nullptr);
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::TextUnformatted(text, text + row.Length);
            }
        }
        clipper.End();
        ImGui::PopStyleVar();

        if (ImGui::BeginPopupContextWindow("##LogContext"))
        {
            if (ImGui::MenuItem("Copy visible lines")) CopyVisible();
            if (ImGui::MenuItem("Clear")) Clear();
            ImGui::EndPopup();
        }

        if (m_Follow) ImGui::SetScrollHereY(1.0f);
        if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel > 0.0f) m_Follow = false;
        else if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) m_Follow = true;
        ImGui::EndChild();
    }
}
