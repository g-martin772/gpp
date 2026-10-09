export module GPP.Core:Assets;

import std;
import :DI.Service;
import :Application.Config;

namespace GPP
{
    export struct AssetKindOptions
    {
        std::vector<std::filesystem::path> Directories;
        std::vector<std::string> Extensions;
    };

    export struct AssetOptions : public IService
    {
        std::map<std::string, AssetKindOptions> Kinds;

        AssetOptions()
        {
            Kinds["Scripts"] = {{"scripts"}, {".lua", ".luau"}};
            Kinds["Combos"] = {{"combos"}, {".yaml", ".yml"}};
            Kinds["Graphs"] = {{"graphs"}, {".yaml", ".yml"}};
        }

        static AssetOptions FromConfig(const IConfigurationSection& config)
        {
            AssetOptions options;
            std::vector<std::string> names;
            for (const auto& [name, _] : options.Kinds)
            {
                names.push_back(name);
            }
            const auto extra = config.GetSection("Kinds");
            for (std::size_t index = 0;; ++index)
            {
                std::string value;
                if (!extra->TryGetValue(std::to_string(index), value))
                {
                    break;
                }
                if (!value.empty())
                {
                    names.push_back(std::move(value));
                }
            }

            for (const auto& name : names)
            {
                const auto section = config.GetSection(name);
                auto& kind = options.Kinds[name];
                if (auto dirs = ReadArray(*section, "Directories"))
                {
                    kind.Directories.assign(dirs->begin(), dirs->end());
                }
                if (auto extensions = ReadArray(*section, "Extensions"))
                {
                    kind.Extensions = std::move(*extensions);
                }
                if (kind.Directories.empty())
                {
                    kind.Directories.emplace_back(ToLower(name));
                }
            }
            return options;
        }

    private:
        static std::optional<std::vector<std::string>> ReadArray(const IConfigurationSection& section,
                                                                 const std::string& key)
        {
            const auto array = section.GetSection(key);
            std::optional<std::vector<std::string>> values;
            for (std::size_t index = 0;; ++index)
            {
                std::string value;
                if (!array->TryGetValue(std::to_string(index), value))
                {
                    break;
                }
                if (!values)
                {
                    values.emplace();
                }
                if (!value.empty())
                {
                    values->push_back(std::move(value));
                }
            }
            return values;
        }

        static std::string ToLower(std::string text)
        {
            std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        }
    };

    export struct AssetChange
    {
        std::string Kind;
        std::string Name;
    };

    export class AssetDirectories : public IService
    {
    public:
        using Dependencies = std::tuple<AssetOptions>;
        using ChangeCallback = std::function<void(const AssetChange&)>;

        explicit AssetDirectories(const std::shared_ptr<AssetOptions>& options)
            : m_Kinds(options->Kinds)
        {
        }

        explicit AssetDirectories(AssetOptions options)
            : m_Kinds(std::move(options.Kinds))
        {
        }

        ~AssetDirectories() override
        {
            StopPolling();
        }

        AssetDirectories(const AssetDirectories&) = delete;
        AssetDirectories& operator=(const AssetDirectories&) = delete;

        void SetKind(const std::string& kind, AssetKindOptions options)
        {
            std::scoped_lock lock(m_Mutex);
            m_Kinds[kind] = std::move(options);
        }

        [[nodiscard]] bool HasKind(const std::string& kind) const
        {
            std::scoped_lock lock(m_Mutex);
            return m_Kinds.contains(kind);
        }

        [[nodiscard]] std::optional<std::filesystem::path> Resolve(const std::string& kind,
                                                                   const std::string& name) const
        {
            AssetKindOptions options;
            if (!CopyKind(kind, options))
            {
                return std::nullopt;
            }
            return ResolveIn(options, name);
        }

        [[nodiscard]] std::vector<std::string> List(const std::string& kind) const
        {
            AssetKindOptions options;
            if (!CopyKind(kind, options))
            {
                return {};
            }
            std::vector<std::string> names;
            for (const auto& [name, _] : Scan(options))
            {
                names.push_back(name);
            }
            return names;
        }

        [[nodiscard]] std::optional<std::string> Read(const std::string& kind, const std::string& name) const
        {
            const auto path = Resolve(kind, name);
            if (!path)
            {
                return std::nullopt;
            }
            std::ifstream stream(*path, std::ios::binary);
            if (!stream)
            {
                return std::nullopt;
            }
            std::ostringstream buffer;
            buffer << stream.rdbuf();
            return buffer.str();
        }

        std::size_t Subscribe(ChangeCallback callback)
        {
            std::scoped_lock lock(m_Mutex);
            const auto id = ++m_NextSubscription;
            m_Subscribers[id] = std::move(callback);
            return id;
        }

        void Unsubscribe(std::size_t id)
        {
            std::scoped_lock lock(m_Mutex);
            m_Subscribers.erase(id);
        }

        [[nodiscard]] std::uint64_t Version() const noexcept
        {
            return m_Version.load();
        }

        // First call records the baseline and reports nothing.
        std::vector<AssetChange> Poll()
        {
            std::map<std::string, AssetKindOptions> kinds;
            {
                std::scoped_lock lock(m_Mutex);
                kinds = m_Kinds;
            }

            Snapshot current;
            for (const auto& [kind, options] : kinds)
            {
                for (auto& [name, entry] : Scan(options))
                {
                    current[{kind, name}] = std::move(entry);
                }
            }

            std::vector<AssetChange> changes;
            std::vector<ChangeCallback> callbacks;
            {
                std::scoped_lock lock(m_Mutex);
                if (m_HasBaseline)
                {
                    for (const auto& [key, entry] : current)
                    {
                        const auto it = m_Snapshot.find(key);
                        if (it == m_Snapshot.end() || it->second.Path != entry.Path ||
                            it->second.WriteTime != entry.WriteTime)
                        {
                            changes.push_back({key.first, key.second});
                        }
                    }
                    for (const auto& [key, _] : m_Snapshot)
                    {
                        if (!current.contains(key))
                        {
                            changes.push_back({key.first, key.second});
                        }
                    }
                }
                m_HasBaseline = true;
                m_Snapshot = std::move(current);
                if (!changes.empty())
                {
                    m_Version.fetch_add(1);
                    for (const auto& [_, callback] : m_Subscribers)
                    {
                        callbacks.push_back(callback);
                    }
                }
            }
            for (const auto& change : changes)
            {
                for (const auto& callback : callbacks)
                {
                    callback(change);
                }
            }
            return changes;
        }

        void StartPolling(std::chrono::milliseconds interval = std::chrono::milliseconds(250))
        {
            StopPolling();
            m_Poller = std::jthread([this, interval](std::stop_token stop)
            {
                while (!stop.stop_requested())
                {
                    Poll();
                    std::this_thread::sleep_for(interval);
                }
            });
        }

        void StopPolling()
        {
            if (m_Poller.joinable())
            {
                m_Poller.request_stop();
                m_Poller.join();
            }
        }

    private:
        struct Entry
        {
            std::filesystem::path Path;
            std::filesystem::file_time_type WriteTime{};
        };

        using Snapshot = std::map<std::pair<std::string, std::string>, Entry>;

        bool CopyKind(const std::string& kind, AssetKindOptions& out) const
        {
            std::scoped_lock lock(m_Mutex);
            const auto it = m_Kinds.find(kind);
            if (it == m_Kinds.end())
            {
                return false;
            }
            out = it->second;
            return true;
        }

        static bool ExtensionAllowed(const AssetKindOptions& options, const std::filesystem::path& path)
        {
            return options.Extensions.empty() ||
                std::ranges::find(options.Extensions, path.extension().string()) != options.Extensions.end();
        }

        static std::optional<std::filesystem::path> ResolveIn(const AssetKindOptions& options,
                                                              const std::string& name)
        {
            const std::filesystem::path relative(name);
            if (name.empty() || relative.is_absolute() ||
                std::ranges::any_of(relative, [](const auto& part) { return part == ".."; }))
            {
                return std::nullopt;
            }
            std::error_code ec;
            for (const auto& root : options.Directories)
            {
                for (const auto& extension : options.Extensions)
                {
                    auto candidate = root / relative;
                    candidate += extension;
                    if (std::filesystem::is_regular_file(candidate, ec))
                    {
                        return candidate;
                    }
                }
                const auto exact = root / relative;
                if (relative.has_extension() && ExtensionAllowed(options, exact) &&
                    std::filesystem::is_regular_file(exact, ec))
                {
                    return exact;
                }
                if (options.Extensions.empty() && std::filesystem::is_regular_file(exact, ec))
                {
                    return exact;
                }
            }
            return std::nullopt;
        }

        static std::map<std::string, Entry> Scan(const AssetKindOptions& options)
        {
            std::map<std::string, Entry> found;
            std::error_code ec;
            for (const auto& root : options.Directories)
            {
                if (!std::filesystem::is_directory(root, ec))
                {
                    continue;
                }
                std::vector<std::filesystem::path> files;
                for (std::filesystem::recursive_directory_iterator it(root, ec), end; !ec && it != end;
                     it.increment(ec))
                {
                    if (it->is_regular_file(ec) && ExtensionAllowed(options, it->path()))
                    {
                        files.push_back(it->path());
                    }
                }
                std::ranges::sort(files);
                for (const auto& file : files)
                {
                    auto relative = std::filesystem::relative(file, root, ec);
                    if (!options.Extensions.empty())
                    {
                        relative.replace_extension();
                    }
                    const auto name = relative.generic_string();
                    if (found.contains(name))
                    {
                        continue;
                    }
                    // First root wins; later roots only fill names it lacks.
                    const auto winner = ResolveIn(options, name);
                    if (!winner || std::filesystem::equivalent(*winner, file, ec))
                    {
                        found[name] = Entry{file, std::filesystem::last_write_time(file, ec)};
                    }
                }
            }
            return found;
        }

        mutable std::mutex m_Mutex;
        std::map<std::string, AssetKindOptions> m_Kinds;
        std::map<std::size_t, ChangeCallback> m_Subscribers;
        std::size_t m_NextSubscription = 0;
        Snapshot m_Snapshot;
        bool m_HasBaseline = false;
        std::atomic<std::uint64_t> m_Version{0};
        std::jthread m_Poller;
    };
}
