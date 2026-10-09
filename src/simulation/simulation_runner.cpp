module GPP.Simulation;

import :Runner;
import std;

namespace GPP
{
    namespace
    {
        constexpr std::size_t kMaxSnapshotBuffers = 8;

        std::int64_t ToNs(const std::chrono::steady_clock::time_point t) noexcept
        {
            return std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch()).count();
        }

        std::chrono::steady_clock::duration PeriodFor(const double hertz) noexcept
        {
            return std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(1.0 / hertz));
        }
    }

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
          m_Modules(std::move(modules)),
          m_Options(options),
          m_Metadata(m_SimScene.Metadata())
    {
        std::ranges::stable_sort(m_Modules, {}, [](const auto& module) { return module->Phase(); });

        const double requested = options.FixedTimestep.count() > 0.0f
                                     ? 1.0 / static_cast<double>(options.FixedTimestep.count())
                                     : 60.0;
        m_TickRate.store(std::clamp(requested, kMinTickRate, kMaxTickRate), std::memory_order_relaxed);

        auto initial = std::make_shared<SceneSnapshot::Data>(m_SimScene.Clone(), ++m_Generation);
        m_Pool.push_back({initial, {}});
        m_Published.store(std::move(initial), std::memory_order_release);
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

    void SimulationRunner::RequestStop()
    {
        if (m_Thread.joinable())
        {
            m_Thread.request_stop();
        }
        Wake();
    }

    void SimulationRunner::Stop()
    {
        if (!m_Running.exchange(false, std::memory_order_acq_rel)) return;
        if (m_Thread.joinable())
        {
            m_Thread.request_stop();
            Wake();
            m_Thread.join();
        }
    }

    void SimulationRunner::SetPaused(const bool paused)
    {
        m_Paused.store(paused, std::memory_order_release);
        Wake();
    }

    void SimulationRunner::SetTickRate(const double hertz)
    {
        const double clamped = std::isfinite(hertz) ? std::clamp(hertz, kMinTickRate, kMaxTickRate) : 60.0;
        m_TickRate.store(clamped, std::memory_order_relaxed);
        Wake();
    }

    SceneMetadata SimulationRunner::Metadata() const
    {
        std::scoped_lock lock(m_MetadataMutex);
        return m_Metadata;
    }

    void SimulationRunner::EnqueueEdit(std::move_only_function<void(Scene&)> edit)
    {
        Enqueue(std::move(edit), false);
    }

    void SimulationRunner::EnqueueTrackedEdit(std::move_only_function<void(Scene&)> edit)
    {
        Enqueue(std::move(edit), true);
    }

    void SimulationRunner::Enqueue(std::move_only_function<void(Scene&)> edit, const bool tracked)
    {
        {
            std::scoped_lock lock(m_EditMutex);
            m_PendingEdits.push(PendingEdit{std::move(edit), tracked});
        }
        Wake();
    }

    void SimulationRunner::Wake()
    {
        {
            std::scoped_lock lock(m_WakeMutex);
            ++m_WakeSignal;
        }
        m_Wake.notify_all();
    }

    SimulationStats SimulationRunner::GetStats() const noexcept
    {
        const auto now = Clock::now();
        SimulationStats stats;
        stats.TargetTickRate = m_TickRate.load(std::memory_order_relaxed);
        stats.Paused = m_Paused.load(std::memory_order_acquire);
        stats.TicksPerSecond = stats.Paused ? 0.0 : m_TickMeter.PerSecond(now);
        stats.LastTickMs = m_TickMeter.LastMs();
        stats.AverageTickMs = m_TickMeter.AverageMs();
        stats.MaxTickMs = m_TickMeter.MaxMs();
        stats.TickCount = m_TickMeter.Total();

        const auto tickStartNs = m_TickStartNs.load(std::memory_order_acquire);
        if (tickStartNs != 0)
        {
            stats.CurrentTickMs = std::max(0.0, static_cast<double>(ToNs(now) - tickStartNs) * 1e-6);
        }
        stats.Stalled = stats.CurrentTickMs > m_Options.StallThresholdMs;
        stats.Overrunning = !stats.Paused && stats.AverageTickMs > 1000.0 / stats.TargetTickRate;
        if (const auto published = m_Published.load(std::memory_order_acquire))
        {
            stats.SnapshotGeneration = published->Generation;
        }
        return stats;
    }

    bool SimulationRunner::DrainEdits()
    {
        std::queue<PendingEdit> edits;
        {
            std::scoped_lock lock(m_EditMutex);
            if (m_PendingEdits.empty()) return false;
            edits.swap(m_PendingEdits);
        }
        while (!edits.empty())
        {
            try
            {
                edits.front().Fn(m_SimScene);
            }
            catch (const std::exception& e)
            {
                Logger::LogError("Edit for scene '{}' failed: {}", m_SimScene.Metadata().Name, e.what());
            }
            catch (...)
            {
                Logger::LogError("Edit for scene '{}' failed with an unknown exception",
                                 m_SimScene.Metadata().Name);
            }
            if (!edits.front().Tracked) m_SimScene.MarkAllDirty();
            edits.pop();
        }
        {
            std::scoped_lock lock(m_MetadataMutex);
            m_Metadata = m_SimScene.Metadata();
        }
        return true;
    }

    void SimulationRunner::RunTick(const float deltaTime)
    {
        const auto start = Clock::now();
        m_TickStartNs.store(ToNs(start), std::memory_order_release);
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
        if (std::ranges::any_of(m_Modules, [](const auto& module) { return !module->ReportsChanges(); }))
        {
            m_SimScene.MarkAllDirty();
        }
        const auto end = Clock::now();
        m_TickStartNs.store(0, std::memory_order_release);
        m_TickMeter.Tick(std::chrono::duration<double, std::milli>(end - start).count(), end);
    }

    bool SimulationRunner::Publish()
    {
        const auto changes = m_SimScene.TakeChanges();
        for (auto& entry : m_Pool)
        {
            entry.Pending.Merge(changes);
        }

        std::shared_ptr<SceneSnapshot::Data> target;
        ChangeSet* pending = nullptr;
        for (auto& entry : m_Pool)
        {
            if (entry.Data.use_count() == 1)
            {
                target = entry.Data;
                pending = &entry.Pending;
                break;
            }
        }

        if (!target)
        {
            if (m_Pool.size() >= kMaxSnapshotBuffers)
            {
                return false; // readers are holding every buffer; try again next time, never block
            }
            target = std::make_shared<SceneSnapshot::Data>(m_SimScene.Clone(), 0);
            m_Pool.push_back({target, {}});
        }
        else
        {
            Scene::SyncChanges(m_SimScene, target->Value, *pending);
            pending->Clear();
        }

        target->Generation = ++m_Generation;
        m_Published.store(std::shared_ptr<const SceneSnapshot::Data>(std::move(target)),
                          std::memory_order_release);
        return true;
    }

    void SimulationRunner::ThreadMain(std::stop_token stopToken)
    {
        m_SimScene.MarkAllDirty();
        for (const auto& module : m_Modules)
        {
            try
            {
                module->OnInit(m_SimScene);
            }
            catch (const std::exception& e)
            {
                Logger::LogError("Simulation init for scene '{}' failed: {}", m_SimScene.Metadata().Name, e.what());
            }
            catch (...)
            {
                Logger::LogError("Simulation init for scene '{}' failed with an unknown exception",
                                 m_SimScene.Metadata().Name);
            }
        }

        const bool fixedRate = m_Options.UseFixedTimestep;
        const auto maxCatchUp = std::max(1u, m_Options.MaxCatchUpTicks);
        const auto publishInterval = PeriodFor(std::max(m_Options.MaxPublishRate, 1.0));

        auto lastTick = Clock::now();
        auto next = lastTick;
        auto lastPublish = lastTick - publishInterval;
        bool dirty = true;
        bool wasPaused = true;
        std::uint64_t seenSignal = 0;

        while (!stopToken.stop_requested())
        {
            if (DrainEdits())
            {
                dirty = true;
            }

            const bool paused = m_Paused.load(std::memory_order_acquire);
            if (paused)
            {
                wasPaused = true;
            }
            else
            {
                auto tickNow = Clock::now();
                if (wasPaused)
                {
                    wasPaused = false;
                    next = tickNow;
                    lastTick = tickNow;
                }

                const auto period = PeriodFor(m_TickRate.load(std::memory_order_relaxed));
                std::uint32_t ticks = 0;
                while (!stopToken.stop_requested() && ticks < maxCatchUp && (!fixedRate || next <= tickNow))
                {
                    const float deltaTime = fixedRate
                                                ? std::chrono::duration<float>(period).count()
                                                : std::chrono::duration<float>(tickNow - lastTick).count();
                    lastTick = tickNow;
                    RunTick(deltaTime);
                    dirty = true;
                    ++ticks;
                    tickNow = Clock::now();
                    if (!fixedRate) break;
                    next += period;
                }
                if (fixedRate && next + period * maxCatchUp < tickNow)
                {
                    next = tickNow;
                }
            }

            const auto now = Clock::now();
            if (dirty && now - lastPublish >= publishInterval)
            {
                try
                {
                    if (Publish())
                    {
                        dirty = false;
                    }
                }
                catch (const std::exception& e)
                {
                    Logger::LogError("Publishing scene '{}' failed: {}", m_SimScene.Metadata().Name, e.what());
                    dirty = false;
                }
                lastPublish = now;
            }

            if (!paused && !fixedRate)
            {
                std::this_thread::yield();
                continue;
            }

            std::optional<Clock::time_point> deadline;
            if (!paused)
            {
                deadline = next;
            }
            if (dirty)
            {
                const auto publishAt = lastPublish + publishInterval;
                deadline = deadline ? std::min(*deadline, publishAt) : publishAt;
            }

            std::unique_lock lock(m_WakeMutex);
            const auto changed = [&] { return m_WakeSignal != seenSignal; };
            if (deadline)
            {
                m_Wake.wait_until(lock, stopToken, *deadline, changed);
            }
            else
            {
                m_Wake.wait(lock, stopToken, changed);
            }
            seenSignal = m_WakeSignal;
        }

        for (const auto& module : m_Modules)
        {
            try
            {
                module->OnShutdown(m_SimScene);
            }
            catch (...)
            {
                Logger::LogError("Simulation shutdown for scene '{}' failed", m_SimScene.Metadata().Name);
            }
        }
    }
}
