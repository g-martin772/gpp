export module GPP.Simulation:SceneManager;

import std;
import GPP.Core;
import :Scene;
import :Runner;

namespace GPP
{
    export class SceneManager : public IService
    {
    public:
        using Dependencies = std::tuple<Logger, IFileSystem>;
        SceneManager(std::shared_ptr<Logger> logger, std::shared_ptr<IFileSystem> fileSystem);
        ~SceneManager() override;

        Scene& CreateScene(std::string name);
        [[nodiscard]] Scene* FindScene(const std::string& name);
        void DestroyScene(const std::string& name);

        std::shared_ptr<SimulationRunner> CreateSimulation(const std::string& sceneName,
                                                            std::shared_ptr<ISimulationModule> module,
                                                            SimulationOptions options = {});
        std::shared_ptr<SimulationRunner> CreateSimulation(Scene scene,
                                                            std::shared_ptr<ISimulationModule> module,
                                                            SimulationOptions options = {});
        std::shared_ptr<SimulationRunner> CreateSimulation(const std::string& sceneName,
                                                            std::vector<std::shared_ptr<ISimulationModule>> modules,
                                                            SimulationOptions options = {});
        std::shared_ptr<SimulationRunner> CreateSimulation(Scene scene,
                                                            std::vector<std::shared_ptr<ISimulationModule>> modules,
                                                            SimulationOptions options = {});

        [[nodiscard]] std::shared_ptr<SimulationRunner> GetSimulation(const std::string& name) const;
        void DestroySimulation(const std::string& name);

        void SaveSceneToFile(const std::string& name, const std::filesystem::path& path);
        Scene& LoadSceneFromFile(const std::filesystem::path& path);

    private:
        std::shared_ptr<Logger> m_Logger;
        std::shared_ptr<IFileSystem> m_FileSystem;
        mutable std::mutex m_Mutex;
        std::unordered_map<std::string, Scene> m_Scenes;
        std::unordered_map<std::string, std::shared_ptr<SimulationRunner>> m_Simulations;
    };
}
