export module GPP.Core:Application.RecentList;

import std;
import :IO.Yaml;

namespace GPP
{
    export struct RecentEntry
    {
        std::string Path;
        std::string Name;
        std::int64_t LastOpened = 0;

        [[nodiscard]] std::string DisplayName() const
        {
            return Name.empty() ? std::filesystem::path(Path).stem().string() : Name;
        }
    };

    export class RecentList
    {
    public:
        explicit RecentList(const std::size_t maxEntries = 8) : m_Max(std::max<std::size_t>(maxEntries, 1)) {}

        [[nodiscard]] static std::int64_t Now();
        [[nodiscard]] static std::string Normalize(const std::filesystem::path& path);

        void Add(const std::filesystem::path& path, std::string name = {}, std::int64_t lastOpened = Now());
        bool Remove(const std::filesystem::path& path);
        void Clear() { m_Entries.clear(); }
        std::size_t Prune();

        void SetMaxEntries(std::size_t maxEntries);
        [[nodiscard]] std::size_t MaxEntries() const noexcept { return m_Max; }
        [[nodiscard]] const std::vector<RecentEntry>& Entries() const noexcept { return m_Entries; }
        [[nodiscard]] bool Empty() const noexcept { return m_Entries.empty(); }

        [[nodiscard]] YAML::Node ToYaml() const;
        void FromYaml(const YAML::Node& node);

        bool Load(const std::filesystem::path& file);
        bool Save(const std::filesystem::path& file) const;

    private:
        std::size_t m_Max;
        std::vector<RecentEntry> m_Entries;
    };
}
