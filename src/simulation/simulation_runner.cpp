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
        m_Random = std::make_shared<SimulationRandom>(m_SimScene.Metadata().Seed);

        auto initial = std::make_shared<SceneSnapshot::Data>(m_SimScene.Clone(), ++m_Generation);
        m_Pool.push_back({initial, {}});
        m_Published.store(std::move(initial), std::memory_order_release);
    }

    SimulationRunner::~SimulationRunner()
    {
        Stop();
        ShutdownModules();
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
        m_StepDone.notify_all();
    }

    void SimulationRunner::SetPaused(const bool paused)
    {
        m_Paused.store(paused, std::memory_order_release);
        Wake();
    }

    void SimulationRunner::SetManualStepping(const bool manual)
    {
        m_Manual.store(manual, std::memory_order_release);
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
            m_PendingEdits.push(PendingEdit{std::move(edit), tracked, {}, {}});
        }
        Wake();
    }

    void SimulationRunner::EnqueueCommand(Command command, CommandOptions options)
    {
        std::vector<Command> commands;
        commands.push_back(std::move(command));
        EnqueueCommands(std::move(commands), std::move(options));
    }

    void SimulationRunner::EnqueueCommands(std::vector<Command> commands, CommandOptions options)
    {
        if (commands.empty()) return;
        {
            std::scoped_lock lock(m_EditMutex);
            m_PendingEdits.push(PendingEdit{nullptr, true, std::move(commands), std::move(options)});
        }
        Wake();
    }

    std::vector<CommandRecord> SimulationRunner::CommandLog() const
    {
        std::scoped_lock lock(m_LogMutex);
        return {m_CommandLog.begin(), m_CommandLog.end()};
    }

    bool SimulationRunner::Undo()
    {
        auto entry = m_History.TakeUndo();
        if (!entry) return false;
        EnqueueCommands(entry->Undo);
        m_History.PushRedo(std::move(*entry));
        return true;
    }

    bool SimulationRunner::Redo()
    {
        auto entry = m_History.TakeRedo();
        if (!entry) return false;
        EnqueueCommands(entry->Redo);
        m_History.PushUndo(std::move(*entry));
        return true;
    }

    void SimulationRunner::ApplyCommands(PendingEdit& edit)
    {
        const auto tick = Tick();
        std::vector<CommandRecord> applied;
        for (auto& command : edit.Commands)
        {
            Command inverse = DestroyEntityCommand{};
            bool ok = false;
            try
            {
                ok = ApplyCommand(m_SimScene, command, &inverse);
            }
            catch (const std::exception& e)
            {
                Logger::LogError("Command for scene '{}' failed: {}", m_SimScene.Metadata().Name, e.what());
            }
            if (ok) applied.push_back(CommandRecord{tick, std::move(command), std::move(inverse)});
        }
        if (applied.empty()) return;

        if (edit.Options.Undoable)
        {
            UndoEntry entry;
            entry.Label = std::move(edit.Options.Label);
            entry.CoalesceKey = std::move(edit.Options.CoalesceKey);
            entry.Time = Clock::now();
            for (const auto& record : applied) entry.Redo.push_back(record.Applied);
            for (auto it = applied.rbegin(); it != applied.rend(); ++it) entry.Undo.push_back(*it->Inverse);
            m_History.Record(std::move(entry));
        }

        if (m_Recording)
        {
            m_Recording->Commands.insert(m_Recording->Commands.end(), applied.begin(), applied.end());
        }
        std::scoped_lock lock(m_LogMutex);
        for (auto& record : applied) m_CommandLog.push_back(std::move(record));
        while (m_CommandLog.size() > m_Options.CommandLogCapacity) m_CommandLog.pop_front();
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
        stats.CurrentTick = Tick();

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
        std::queue<PendingEdit> incoming;
        {
            std::scoped_lock lock(m_EditMutex);
            incoming.swap(m_PendingEdits);
        }
        if (incoming.empty() && m_Deferred.empty()) return false;

        std::vector<PendingEdit> work = std::exchange(m_Deferred, {});
        while (!incoming.empty())
        {
            work.push_back(std::move(incoming.front()));
            incoming.pop();
        }

        const auto tick = Tick();
        bool applied = false;
        for (auto& edit : work)
        {
            if (edit.Options.TargetTick && *edit.Options.TargetTick > tick)
            {
                m_Deferred.push_back(std::move(edit));
                continue;
            }
            if (edit.Options.OnlyWhilePlaying && IsPaused()) continue;
            applied = true;
            if (!edit.Commands.empty())
            {
                ApplyCommands(edit);
                continue;
            }
            if (!edit.Fn) continue;
            try
            {
                edit.Fn(m_SimScene);
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
            if (!edit.Tracked) m_SimScene.MarkAllDirty();
        }
        if (!applied) return false;
        {
            std::scoped_lock lock(m_MetadataMutex);
            m_Metadata = m_SimScene.Metadata();
        }
        return true;
    }

    float SimulationRunner::StepDeltaTime() const
    {
        return std::chrono::duration<float>(PeriodFor(m_TickRate.load(std::memory_order_relaxed))).count();
    }

    void SimulationRunner::StepOne(const float deltaTime)
    {
        DrainEdits();
        RunTick(deltaTime);
    }

    void SimulationRunner::PublishNow()
    {
        for (int attempt = 0; attempt < 2000; ++attempt)
        {
            try
            {
                if (Publish()) return;
            }
            catch (const std::exception& e)
            {
                Logger::LogError("Publishing scene '{}' failed: {}", m_SimScene.Metadata().Name, e.what());
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        Logger::LogError("Publishing scene '{}' timed out: readers hold every snapshot buffer", m_SimScene.Metadata().Name);
    }

    void SimulationRunner::ExecuteSteps(const std::uint64_t ticks)
    {
        EnsureInitialized();
        const float deltaTime = StepDeltaTime();
        for (std::uint64_t i = 0; i < ticks; ++i) StepOne(deltaTime);
        DrainEdits();
        PublishNow();
    }

    void SimulationRunner::Step(const std::uint64_t ticks)
    {
        if (ticks == 0) return;
        if (m_Running.load(std::memory_order_acquire))
        {
            m_StepsRequested.fetch_add(ticks, std::memory_order_acq_rel);
            Wake();
            return;
        }
        std::scoped_lock lock(m_InlineMutex);
        ExecuteSteps(ticks);
    }

    void SimulationRunner::StepAndWait(const std::uint64_t ticks)
    {
        if (ticks == 0) return;
        if (!m_Running.load(std::memory_order_acquire))
        {
            std::scoped_lock lock(m_InlineMutex);
            ExecuteSteps(ticks);
            return;
        }
        const auto target = m_StepsRequested.fetch_add(ticks, std::memory_order_acq_rel) + ticks;
        Wake();
        std::unique_lock lock(m_StepMutex);
        m_StepDone.wait(lock, [&] { return m_StepsCompleted >= target || !m_Running.load(std::memory_order_acquire); });
    }

    void SimulationRunner::EnsureInitialized()
    {
        if (m_Initialized) return;
        m_Initialized = true;
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
    }

    void SimulationRunner::ShutdownModules()
    {
        if (!m_Initialized) return;
        m_Initialized = false;
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

    void SimulationRunner::RunOnSimThread(std::function<void()> fn)
    {
        if (!m_Running.load(std::memory_order_acquire))
        {
            std::scoped_lock lock(m_InlineMutex);
            EnsureInitialized();
            fn();
            return;
        }
        auto done = std::make_shared<std::promise<void>>();
        auto future = done->get_future();
        EnqueueTrackedEdit([fn = std::move(fn), done](Scene&)
        {
            try { fn(); } catch (...) {}
            done->set_value();
        });
        while (future.wait_for(std::chrono::milliseconds(10)) != std::future_status::ready)
        {
            if (!m_Running.load(std::memory_order_acquire)) return;
        }
    }

    RunnerState SimulationRunner::CaptureState() const
    {
        RunnerState state;
        for (const auto& module : m_Modules) state.Modules.push_back(module->SaveState());
        state.RngState = m_Random->State();
        return state;
    }

    void SimulationRunner::RestoreState(const RunnerState& state)
    {
        for (std::size_t i = 0; i < m_Modules.size() && i < state.Modules.size(); ++i)
        {
            if (!state.Modules[i].empty()) m_Modules[i]->LoadState(state.Modules[i]);
        }
        m_Random->SetState(state.RngState);
    }

    void SimulationRunner::BeginRecording()
    {
        RunOnSimThread([this]
        {
            SimulationRecording recording;
            recording.Initial = m_SimScene.Clone();
            recording.TickRate = m_TickRate.load(std::memory_order_relaxed);
            recording.StartTick = Tick();
            recording.State = CaptureState();
            m_Recording = std::move(recording);
        });
    }

    SimulationRecording SimulationRunner::EndRecording()
    {
        SimulationRecording result;
        RunOnSimThread([this, &result]
        {
            if (!m_Recording) return;
            result = std::move(*m_Recording);
            m_Recording.reset();
            result.EndTick = Tick();
        });
        return result;
    }

    void SimulationRunner::PrepareReplay(const SimulationRecording& recording)
    {
        std::scoped_lock lock(m_InlineMutex);
        ShutdownModules();
        m_SimScene = recording.Initial;
        m_SimScene.MarkAllDirty();
        {
            std::scoped_lock metadata(m_MetadataMutex);
            m_Metadata = m_SimScene.Metadata();
        }
        SetTickRate(recording.TickRate);
        m_Tick.store(recording.StartTick, std::memory_order_release);
        EnsureInitialized();
        RestoreState(recording.State);
        for (const auto& record : recording.Commands)
        {
            CommandOptions options;
            options.TargetTick = record.Tick;
            EnqueueCommand(record.Applied, std::move(options));
        }
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
        m_Tick.fetch_add(1, std::memory_order_acq_rel);
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
        {
            std::scoped_lock lock(m_InlineMutex);
            EnsureInitialized();
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

            const auto requested = m_StepsRequested.load(std::memory_order_acquire);
            std::uint64_t completed;
            {
                std::scoped_lock lock(m_StepMutex);
                completed = m_StepsCompleted;
            }
            if (requested > completed)
            {
                ExecuteSteps(requested - completed);
                {
                    std::scoped_lock lock(m_StepMutex);
                    m_StepsCompleted = requested;
                }
                m_StepDone.notify_all();
                dirty = false;
                lastPublish = Clock::now();
                lastTick = lastPublish;
            }

            const bool paused = m_Paused.load(std::memory_order_acquire) || m_Manual.load(std::memory_order_acquire);
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
                    StepOne(deltaTime);
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

        {
            std::scoped_lock lock(m_InlineMutex);
            ShutdownModules();
        }
        {
            std::scoped_lock lock(m_StepMutex);
            m_StepsCompleted = m_StepsRequested.load(std::memory_order_acquire);
        }
        m_StepDone.notify_all();
    }
}
