export module GPP.Simulation:Runner;

import std;
import GPP.Core;
import :Scene;

namespace GPP
{
    export struct ISimulationModule
    {
        virtual ~ISimulationModule() = default;

        virtual void OnInit(Scene& scene) {}
        virtual void OnTick(Scene& scene, float deltaTime) {}
        virtual void OnShutdown(Scene& scene) {}
    };

    export struct SimulationOptions
    {
        std::chrono::duration<float> FixedTimestep{1.0f / 60.0f};
        bool UseFixedTimestep = true;
    };

    export class RenderSceneLock
    {
    public:
        RenderSceneLock(Scene& scene, std::mutex& mutex)
            : m_Scene(scene), m_Lock(mutex)
        {
        }

        Scene* operator->() noexcept { return &m_Scene; }
        Scene& operator*() noexcept { return m_Scene; }

    private:
        Scene& m_Scene;
        std::unique_lock<std::mutex> m_Lock;
    };

    export class SimulationRunner
    {
    public:
        SimulationRunner(Scene scene, std::shared_ptr<ISimulationModule> module,
                         SimulationOptions options = {});
        ~SimulationRunner();

        SimulationRunner(const SimulationRunner&) = delete;
        SimulationRunner& operator=(const SimulationRunner&) = delete;

        void Start();
        void Stop();

        void SetPaused(bool paused) noexcept { m_Paused.store(paused, std::memory_order_relaxed); }
        [[nodiscard]] bool IsPaused() const noexcept { return m_Paused.load(std::memory_order_relaxed); }
        [[nodiscard]] bool IsRunning() const noexcept { return m_Running.load(std::memory_order_relaxed); }

        [[nodiscard]] RenderSceneLock LockRenderScene() { return RenderSceneLock(m_RenderScene, m_RenderMutex); }
        [[nodiscard]] const SceneMetadata& Metadata() const noexcept { return m_SimScene.Metadata(); }

    private:
        void ThreadMain(std::stop_token stopToken);

        Scene m_SimScene;
        Scene m_RenderScene;
        std::mutex m_RenderMutex;
        std::shared_ptr<ISimulationModule> m_Module;
        SimulationOptions m_Options;
        std::jthread m_Thread;
        std::atomic<bool> m_Paused{false};
        std::atomic<bool> m_Running{false};
    };
}
