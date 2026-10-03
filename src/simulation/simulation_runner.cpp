module GPP.Simulation;

import :Runner;
import std;

namespace GPP
{
    SimulationRunner::SimulationRunner(Scene scene, std::shared_ptr<ISimulationModule> module,
                                       SimulationOptions options)
        : SimulationRunner(std::move(scene),
                           std::vector<std::shared_ptr<ISimulationModule>>{std::move(module)},
                           options)
    {
    }

    SimulationRunner::SimulationRunner(Scene scene, std::vector<std::shared_ptr<ISimulationModule>> modules,
                                       SimulationOptions options)
        : m_SimScene(std::move(scene)),
          m_RenderScene(m_SimScene.Clone()),
          m_Modules(std::move(modules)),
          m_Options(options)
    {
    }

    SimulationRunner::~SimulationRunner()
    {
        Stop();
    }

    void SimulationRunner::Start()
    {
        if (m_Running.exchange(true, std::memory_order_acq_rel)) return;
        m_Thread = std::jthread([this](std::stop_token stopToken) { ThreadMain(stopToken); });
    }

    void SimulationRunner::Stop()
    {
        if (!m_Running.exchange(false, std::memory_order_acq_rel)) return;
        if (m_Thread.joinable())
        {
            m_Thread.request_stop();
            m_Thread.join();
        }
    }

    void SimulationRunner::ThreadMain(std::stop_token stopToken)
    {
        for (const auto& module : m_Modules)
        {
            module->OnInit(m_SimScene);
        }

        auto lastTick = std::chrono::steady_clock::now();
        const auto timestep = std::chrono::duration_cast<std::chrono::steady_clock::duration>(m_Options.FixedTimestep);

        while (!stopToken.stop_requested())
        {
            if (m_Paused.load(std::memory_order_relaxed))
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                lastTick = std::chrono::steady_clock::now();
                continue;
            }

            const auto tickStart = std::chrono::steady_clock::now();
            const float deltaTime = m_Options.UseFixedTimestep
                ? m_Options.FixedTimestep.count()
                : std::chrono::duration<float>(tickStart - lastTick).count();
            lastTick = tickStart;

            try
            {
                for (const auto& module : m_Modules)
                {
                    module->OnTick(m_SimScene, deltaTime);
                }
            }
            catch (const std::exception& e)
            {
                Logger::LogError("Simulation tick for scene '{}' failed: {}", m_SimScene.Metadata().Name, e.what());
            }
            catch (...)
            {
                Logger::LogError("Simulation tick for scene '{}' failed with an unknown exception",
                                 m_SimScene.Metadata().Name);
            }

            {
                std::scoped_lock lock(m_RenderMutex);
                Scene::SyncInto(m_SimScene, m_RenderScene);
            }

            if (m_Options.UseFixedTimestep)
            {
                const auto elapsed = std::chrono::steady_clock::now() - tickStart;
                if (elapsed < timestep)
                {
                    std::this_thread::sleep_for(timestep - elapsed);
                }
            }
        }

        for (const auto& module : m_Modules)
        {
            module->OnShutdown(m_SimScene);
        }
    }
}
