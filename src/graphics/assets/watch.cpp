module GPP.Graphics;

import std;
import GPP.Core;
import :Shader;
import :Assets.Watch;

namespace GPP
{
    AssetFileWatch::AssetFileWatch(std::shared_ptr<IFileSystem> fileSystem, std::shared_ptr<Logger> logger)
        : m_FileSystem(std::move(fileSystem)),
          m_Dispatcher(std::make_shared<EventDispatcher>()),
          m_Watcher(m_FileSystem, m_Dispatcher, std::move(logger))
    {
        m_Subscription = m_Dispatcher->Subscribe<ShaderFileChangedEvent>(
            [pending = m_Pending](const ShaderFileChangedEvent& event)
            {
                std::scoped_lock lock(pending->Mutex);
                pending->Changed.insert(event.path.lexically_normal());
            },
            EventDelivery::Immediate);
        m_Watcher.StartAsync().get();
    }

    AssetFileWatch::~AssetFileWatch()
    {
        m_Subscription.Reset();
        m_Watcher.StopAsync().get();
    }

    std::filesystem::path AssetFileWatch::Watch(const std::filesystem::path& path)
    {
        auto resolved = m_FileSystem->ResolvePath(path).lexically_normal();
        {
            std::scoped_lock lock(m_Mutex);
            if (!m_Watched.insert(resolved).second)
            {
                return resolved;
            }
        }
        m_Watcher.Watch(resolved);
        return resolved;
    }

    std::vector<std::filesystem::path> AssetFileWatch::TakeChanged()
    {
        std::scoped_lock lock(m_Pending->Mutex);
        std::vector<std::filesystem::path> out(m_Pending->Changed.begin(), m_Pending->Changed.end());
        m_Pending->Changed.clear();
        return out;
    }

    void AssetFileWatch::PollNow()
    {
        m_Watcher.PollNow();
    }
}
