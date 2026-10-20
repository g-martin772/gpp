export module GPP.Graphics:UI.LogPanel;

import std;
import GPP.Core;

namespace GPP
{
    export struct LogLevelStyle
    {
        std::array<float, 4> Color;
        bool Background;
    };

    export [[nodiscard]] LogLevelStyle LogLevelAppearance(LogLevel level);
    export [[nodiscard]] const char* LogLevelName(LogLevel level);

    export class LogPanel
    {
    public:
        explicit LogPanel(std::size_t maxEntries = 20000) : m_MaxEntries(std::max<std::size_t>(maxEntries, 1)) {}

        void Draw(const LogBuffer& buffer);
        void Clear();

        void SetFilter(std::string_view text);
        [[nodiscard]] std::string_view Filter() const noexcept { return m_FilterText.data(); }

        std::array<bool, 6> ShowLevel{true, true, true, true, true, true};
        bool ShowTime = true;
        bool ShowSource = false;

    private:
        struct Item
        {
            LogEntry Entry;
            std::string Stamp;
        };

        struct Row
        {
            std::uint32_t Item = 0;
            std::uint32_t Begin = 0;
            std::uint32_t Length = 0;
            bool First = true;
        };

        struct Term
        {
            std::string Text;
            bool Exclude = false;
        };

        void Sync(const LogBuffer& buffer);
        void Rebuild();
        void AppendRows(std::size_t firstItem);
        [[nodiscard]] bool Matches(const Item& item) const;
        [[nodiscard]] std::string FormatLine(const Item& item) const;
        void CopyVisible() const;
        void RecountLevels();

        std::size_t m_MaxEntries;
        std::vector<Item> m_Items;
        std::vector<Row> m_Rows;
        std::array<std::size_t, 6> m_LevelCounts{};
        std::vector<Term> m_Terms;
        std::array<char, 256> m_FilterText{};
        std::uint64_t m_Cursor = 0;
        std::array<bool, 6> m_BuiltLevels{};
        bool m_Dirty = true;
        bool m_Follow = true;
    };
}
