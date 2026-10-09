module;
#include <fstream>
module GPP.Simulation;

import :SceneManager;
import std;

namespace GPP
{
    SceneManager::SceneManager(std::shared_ptr<Logger> logger, std::shared_ptr<IFileSystem> fileSystem)
        : m_Logger(std::move(logger)), m_FileSystem(std::move(fileSystem))
    {
    }

    SceneManager::~SceneManager()
    {
        std::vector<std::shared_ptr<SimulationRunner>> runners;
        {
            std::scoped_lock lock(m_Mutex);
            for (auto& [name, runner] : m_Simulations) runners.push_back(runner);
        }
        for (auto& runner : runners) runner->Stop();
    }

    Scene& SceneManager::CreateScene(std::string name)
    {
        std::scoped_lock lock(m_Mutex);
        const auto [it, inserted] = m_Scenes.try_emplace(name, Scene(name));
        if (!inserted)
        {
            m_Logger->Warn("Scene '{}' already exists; returning the existing instance", name);
        }
        return it->second;
    }

    Scene* SceneManager::FindScene(const std::string& name)
    {
        std::scoped_lock lock(m_Mutex);
        const auto it = m_Scenes.find(name);
        return it != m_Scenes.end() ? &it->second : nullptr;
    }

    void SceneManager::DestroyScene(const std::string& name)
    {
        std::scoped_lock lock(m_Mutex);
        m_Scenes.erase(name);
    }

    std::shared_ptr<SimulationRunner> SceneManager::CreateSimulation(
        const std::string& sceneName, std::shared_ptr<ISimulationModule> module, SimulationOptions options)
    {
        Scene scene;
        {
            std::scoped_lock lock(m_Mutex);
            const auto it = m_Scenes.find(sceneName);
            if (it == m_Scenes.end())
            {
                throw std::runtime_error("SceneManager: unknown scene '" + sceneName + "'");
            }
            scene = it->second.Clone();
        }
        return CreateSimulation(std::move(scene), std::move(module), options);
    }

    std::shared_ptr<SimulationRunner> SceneManager::CreateSimulation(
        Scene scene, std::shared_ptr<ISimulationModule> module, SimulationOptions options)
    {
        const auto name = scene.Metadata().Name;
        auto runner = std::make_shared<SimulationRunner>(std::move(scene), std::move(module), options);

        std::shared_ptr<SimulationRunner> replaced;
        {
            std::scoped_lock lock(m_Mutex);
            auto& slot = m_Simulations[name];
            replaced = std::move(slot);
            slot = runner;
        }
        if (replaced) replaced->Stop();
        return runner;
    }

    std::shared_ptr<SimulationRunner> SceneManager::CreateSimulation(
        const std::string& sceneName, std::vector<std::shared_ptr<ISimulationModule>> modules,
        SimulationOptions options)
    {
        Scene scene;
        {
            std::scoped_lock lock(m_Mutex);
            const auto it = m_Scenes.find(sceneName);
            if (it == m_Scenes.end())
            {
                throw std::runtime_error("SceneManager: unknown scene '" + sceneName + "'");
            }
            scene = it->second.Clone();
        }
        return CreateSimulation(std::move(scene), std::move(modules), options);
    }

    std::shared_ptr<SimulationRunner> SceneManager::CreateSimulation(
        Scene scene, std::vector<std::shared_ptr<ISimulationModule>> modules, SimulationOptions options)
    {
        const auto name = scene.Metadata().Name;
        auto runner = std::make_shared<SimulationRunner>(std::move(scene), std::move(modules), options);

        std::shared_ptr<SimulationRunner> replaced;
        {
            std::scoped_lock lock(m_Mutex);
            auto& slot = m_Simulations[name];
            replaced = std::move(slot);
            slot = runner;
        }
        if (replaced) replaced->Stop();
        return runner;
    }

    std::shared_ptr<SimulationRunner> SceneManager::GetSimulation(const std::string& name) const
    {
        std::scoped_lock lock(m_Mutex);
        const auto it = m_Simulations.find(name);
        return it != m_Simulations.end() ? it->second : nullptr;
    }

    void SceneManager::DestroySimulation(const std::string& name)
    {
        std::shared_ptr<SimulationRunner> runner;
        {
            std::scoped_lock lock(m_Mutex);
            const auto it = m_Simulations.find(name);
            if (it == m_Simulations.end()) return;
            runner = std::move(it->second);
            m_Simulations.erase(it);
        }
        runner->Stop();
    }

    void SceneManager::DestroySimulationAsync(const std::string& name)
    {
        std::shared_ptr<SimulationRunner> runner;
        {
            std::scoped_lock lock(m_Mutex);
            const auto it = m_Simulations.find(name);
            if (it == m_Simulations.end()) return;
            runner = std::move(it->second);
            m_Simulations.erase(it);
        }
        runner->RequestStop();
        ThreadPool::Instance().Submit([runner = std::move(runner)]() mutable
        {
            runner->Stop();
            runner.reset();
        });
    }

    void SceneManager::SaveSceneToFile(const std::string& name, const std::filesystem::path& path)
    {
        std::string yaml;
        {
            std::scoped_lock lock(m_Mutex);
            const auto it = m_Scenes.find(name);
            if (it == m_Scenes.end())
            {
                throw std::runtime_error("SceneManager: unknown scene '" + name + "'");
            }
            yaml = it->second.SerializeToYaml();
        }

        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            throw std::runtime_error("SceneManager: failed to open file for writing: " + path.string());
        }
        file << yaml;
    }

    Scene& SceneManager::LoadSceneFromFile(const std::filesystem::path& path)
    {
        const auto text = m_FileSystem->ReadAllText(path);

        Scene scene;
        scene.DeserializeFromYaml(text);
        const auto name = scene.Metadata().Name;

        std::scoped_lock lock(m_Mutex);
        auto [it, _] = m_Scenes.insert_or_assign(name, std::move(scene));
        return it->second;
    }
}
