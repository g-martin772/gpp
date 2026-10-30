export module GPP.Graphics:Assets.Watch;

import std;
import GPP.Core;
import :Shader;

namespace GPP
{
    export template <typename Key>
    class AssetPathIndex
    {
    public:
        void Add(const std::filesystem::path& path, const Key& key)
        {
            m_Keys[path.lexically_normal()].insert(key);
        }

        void Remove(const Key& key)
        {
            for (auto it = m_Keys.begin(); it != m_Keys.end();)
            {
                it->second.erase(key);
                it = it->second.empty() ? m_Keys.erase(it) : std::next(it);
            }
        }

        [[nodiscard]] std::vector<Key> Find(const std::filesystem::path& path) const
        {
            const auto it = m_Keys.find(path.lexically_normal());
            return it == m_Keys.end() ? std::vector<Key>{} : std::vector<Key>(it->second.begin(), it->second.end());
        }

    private:
        std::map<std::filesystem::path, std::set<Key>> m_Keys;
    };

    export class AssetFileWatch
    {
    public:
        AssetFileWatch(std::shared_ptr<IFileSystem> fileSystem, std::shared_ptr<Logger> logger);
        ~AssetFileWatch();
        AssetFileWatch(const AssetFileWatch&) = delete;
        AssetFileWatch& operator=(const AssetFileWatch&) = delete;

        std::filesystem::path Watch(const std::filesystem::path& path);
        [[nodiscard]] std::vector<std::filesystem::path> TakeChanged();
        void PollNow();

    private:
        std::shared_ptr<IFileSystem> m_FileSystem;
        std::shared_ptr<EventDispatcher> m_Dispatcher;
        ShaderFileWatcher m_Watcher;
        EventSubscription m_Subscription;
        struct Pending
        {
            std::mutex Mutex;
            std::set<std::filesystem::path> Changed;
        };

        std::shared_ptr<Pending> m_Pending = std::make_shared<Pending>();
        std::mutex m_Mutex;
        std::set<std::filesystem::path> m_Watched;
    };
}
