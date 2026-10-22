module GPP.Core;

import std;
import :Application.RecentList;
import :IO.AtomicFile;

namespace GPP
{
    namespace fs = std::filesystem;

    std::int64_t RecentList::Now()
    {
        return std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    }

    std::string RecentList::Normalize(const fs::path& path)
    {
        if (path.empty()) return {};
        std::error_code ec;
        fs::path absolute = fs::absolute(path, ec);
        if (ec) absolute = path;
        return absolute.lexically_normal().string();
    }

    void RecentList::Add(const fs::path& path, std::string name, const std::int64_t lastOpened)
    {
        const std::string key = Normalize(path);
        if (key.empty()) return;
        if (name.empty())
        {
            if (const auto it = std::ranges::find(m_Entries, key, &RecentEntry::Path); it != m_Entries.end())
            {
                name = it->Name;
            }
        }
        std::erase_if(m_Entries, [&](const RecentEntry& e) { return e.Path == key; });
        m_Entries.insert(m_Entries.begin(), RecentEntry{key, std::move(name), lastOpened});
        if (m_Entries.size() > m_Max) m_Entries.resize(m_Max);
    }

    bool RecentList::Remove(const fs::path& path)
    {
        const std::string key = Normalize(path);
        return std::erase_if(m_Entries, [&](const RecentEntry& e) { return e.Path == key; }) > 0;
    }

    std::size_t RecentList::Prune()
    {
        return std::erase_if(m_Entries, [](const RecentEntry& e)
        {
            std::error_code ec;
            return !fs::exists(fs::path(e.Path), ec);
        });
    }

    void RecentList::Merge(const RecentList& other, const std::span<const std::string> excluded)
    {
        for (const auto& entry : other.m_Entries)
        {
            if (std::ranges::contains(excluded, entry.Path)) continue;
            if (const auto it = std::ranges::find(m_Entries, entry.Path, &RecentEntry::Path); it != m_Entries.end())
            {
                if (entry.LastOpened > it->LastOpened) it->LastOpened = entry.LastOpened;
                if (it->Name.empty()) it->Name = entry.Name;
            }
            else
            {
                m_Entries.push_back(entry);
            }
        }
        std::ranges::stable_sort(m_Entries, std::greater<>{}, &RecentEntry::LastOpened);
        if (m_Entries.size() > m_Max) m_Entries.resize(m_Max);
    }

    void RecentList::SetMaxEntries(const std::size_t maxEntries)
    {
        m_Max = std::max<std::size_t>(maxEntries, 1);
        if (m_Entries.size() > m_Max) m_Entries.resize(m_Max);
    }

    YAML::Node RecentList::ToYaml() const
    {
        YAML::Node seq(YAML::NodeType::Sequence);
        for (const auto& entry : m_Entries)
        {
            YAML::Node item;
            item["Path"] = entry.Path;
            if (!entry.Name.empty()) item["Name"] = entry.Name;
            item["LastOpened"] = entry.LastOpened;
            seq.push_back(item);
        }
        return seq;
    }

    void RecentList::FromYaml(const YAML::Node& node)
    {
        m_Entries.clear();
        if (!node || !node.IsSequence()) return;
        for (const auto& item : node)
        {
            RecentEntry entry;
            if (item.IsScalar())
            {
                entry.Path = Normalize(item.as<std::string>(""));
            }
            else if (item.IsMap())
            {
                entry.Path = Normalize(item["Path"].as<std::string>(""));
                entry.Name = item["Name"].as<std::string>("");
                entry.LastOpened = item["LastOpened"].as<std::int64_t>(0);
            }
            if (entry.Path.empty()) continue;
            if (std::ranges::contains(m_Entries, entry.Path, &RecentEntry::Path)) continue;
            m_Entries.push_back(std::move(entry));
            if (m_Entries.size() >= m_Max) break;
        }
    }

    bool RecentList::Load(const fs::path& file)
    {
        try
        {
            const YAML::Node root = YAML::LoadFile(file.string());
            FromYaml(root["Recent"]);
            return true;
        }
        catch (const std::exception&)
        {
            m_Entries.clear();
            return false;
        }
    }

    bool RecentList::Save(const fs::path& file) const
    {
        try
        {
            if (file.has_parent_path())
            {
                std::error_code ec;
                fs::create_directories(file.parent_path(), ec);
            }
            YAML::Node root;
            root["Recent"] = ToYaml();
            YAML::Emitter out;
            out << root;
            return WriteFileAtomic(file, std::string(out.c_str()) + '\n').has_value();
        }
        catch (const std::exception&)
        {
            return false;
        }
    }
}
