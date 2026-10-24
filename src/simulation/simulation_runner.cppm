export module GPP.Simulation:Runner;

import std;
import GPP.Core;
import :Scene;
import :Commands;
import :ContentRevision;
import :Random;
import :StateHash;

namespace GPP
{
    export enum class SimulationPhase
    {
        PreSim,
        Forces,
        Integrate,
        PostSim
    };

    export struct ISimulationModule
    {
        virtual ~ISimulationModule() = default;

        [[nodiscard]] virtual SimulationPhase Phase() const noexcept { return SimulationPhase::PreSim; }
        [[nodiscard]] virtual bool ReportsChanges() const noexcept { return false; }

        virtual void OnInit(Scene& scene) {}
        virtual void OnTick(Scene& scene, float deltaTime) {}
        virtual void OnShutdown(Scene& scene) {}

        // Hidden state that lives outside the scene; included in Play/Stop restore and recordings. Empty means none.
        [[nodiscard]] virtual std::string SaveState() const { return {}; }
        virtual void LoadState(const std::string& state) { (void)state; }
    };

    export struct SimulationOptions
    {
        std::chrono::duration<float> FixedTimestep{1.0f / 60.0f};
        bool UseFixedTimestep = true;
        std::uint32_t MaxCatchUpTicks = 3;
        double MaxPublishRate = 240.0;
        double StallThresholdMs = 1000.0;
        std::size_t CommandLogCapacity = 4096;
    };

    export struct CommandOptions
    {
        std::optional<std::uint64_t> TargetTick; // applied once this many ticks have run (replay)
        bool Undoable = false;
        std::string Label;
        std::string CoalesceKey;
        bool OnlyWhilePlaying = false; // dropped if the runner is paused when it would be applied
        std::optional<RevisionMove> Move; // set by Undo/Redo: the content revision the history step restores
    };

    export struct RunnerState
    {
        std::vector<std::string> Modules;
        std::uint64_t RngState{0};
    };

    export struct StateCheckpoint
    {
        std::uint64_t Tick{0};
        std::uint64_t Hash{0};
    };

    export struct SimulationRecording
    {
        Scene Initial;
        double TickRate{60.0};
        std::uint64_t StartTick{0};
        std::uint64_t EndTick{0};
        RunnerState State;
        std::vector<CommandRecord> Commands;
        // State hash after the tick completes (before that tick's edits), every CheckpointInterval ticks from StartTick,
        // plus the start state itself. FinalHash is taken once everything recorded has been applied.
        std::uint64_t CheckpointInterval{0};
        std::vector<StateCheckpoint> Checkpoints;
        std::optional<std::uint64_t> FinalHash;

        [[nodiscard]] std::uint64_t Ticks() const noexcept { return EndTick - StartTick; }
    };

    export struct SimulationStats
    {
        double TargetTickRate = 0.0;
        double TicksPerSecond = 0.0;
        double LastTickMs = 0.0;
        double AverageTickMs = 0.0;
        double MaxTickMs = 0.0;
        double CurrentTickMs = 0.0;
        std::uint64_t TickCount = 0;
        std::uint64_t CurrentTick = 0;
        std::uint64_t SnapshotGeneration = 0;
        bool Paused = false;
        bool Stalled = false;
        bool Overrunning = false;
    };

    export class SceneSnapshot
    {
    public:
        struct Data
        {
            Data(Scene scene, std::uint64_t generation) : Value(std::move(scene)), Generation(generation) {}
            std::uint64_t ContentRevision{0};
            Scene Value;
            std::uint64_t Generation;
        };

        SceneSnapshot() = default;
        explicit SceneSnapshot(std::shared_ptr<const Data> data) noexcept : m_Data(std::move(data)) {}

        const Scene* operator->() const noexcept { return &m_Data->Value; }
        const Scene& operator*() const noexcept { return m_Data->Value; }
        explicit operator bool() const noexcept { return m_Data != nullptr; }
        // Identifies the user-visible scene content (edits, not simulation ticks); equal revisions mean equal content.
        [[nodiscard]] std::uint64_t ContentRevision() const noexcept { return m_Data ? m_Data->ContentRevision : 0; }

        [[nodiscard]] std::uint64_t Generation() const noexcept { return m_Data ? m_Data->Generation : 0; }

    private:
        std::shared_ptr<const Data> m_Data;
    };

    export using RenderSceneLock = SceneSnapshot;

    export class SimulationRunner
    {
    public:
        SimulationRunner(Scene scene, std::shared_ptr<ISimulationModule> module,
                         SimulationOptions options = {});
        SimulationRunner(Scene scene, std::vector<std::shared_ptr<ISimulationModule>> modules,
                         SimulationOptions options = {});
        ~SimulationRunner();

        SimulationRunner(const SimulationRunner&) = delete;
        SimulationRunner& operator=(const SimulationRunner&) = delete;

        void Start();
        void RequestStop();
        void Stop();

        void SetPaused(bool paused);
        [[nodiscard]] bool IsPaused() const noexcept { return m_Paused.load(std::memory_order_acquire); }
        // While set the realtime loop stops ticking (IsPaused is unaffected); only Step/StepAndWait advance the sim.
        void SetManualStepping(bool manual);
        [[nodiscard]] bool IsManualStepping() const noexcept { return m_Manual.load(std::memory_order_acquire); }
        [[nodiscard]] bool IsRunning() const noexcept { return m_Running.load(std::memory_order_acquire); }

        void SetTickRate(double hertz);
        [[nodiscard]] double GetTickRate() const noexcept { return m_TickRate.load(std::memory_order_relaxed); }
        static constexpr double kMinTickRate = 1.0;
        static constexpr double kMaxTickRate = 2000.0;

        [[nodiscard]] SimulationStats GetStats() const noexcept;

        [[nodiscard]] SceneSnapshot AcquireSnapshot() const
        {
            ProfileZone zone("Snapshot.Acquire");
            return SceneSnapshot(m_Published.load(std::memory_order_acquire));
        }
        [[nodiscard]] SceneSnapshot LockRenderScene() const { return AcquireSnapshot(); }
        [[nodiscard]] SceneMetadata Metadata() const;

        template <typename T>
        [[nodiscard]] std::shared_ptr<T> GetModule() const
        [[nodiscard]] std::vector<std::string> TakeInitErrors();
        {
            for (const auto& module : m_Modules)
            {
                if (auto typed = std::dynamic_pointer_cast<T>(module)) return typed;
            }
            return nullptr;
        }

        void EnqueueEdit(std::move_only_function<void(Scene&)> edit);
        void EnqueueTrackedEdit(std::move_only_function<void(Scene&)> edit);

        // Applied on the sim thread through the tracked path, stamped with the tick and kept in the command log.
        // Like EnqueueTrackedEdit, but not a content change (play/stop bookkeeping, queries).
        void EnqueueSystemEdit(std::move_only_function<void(Scene&)> edit, bool tracked = true);
        void EnqueueCommand(Command command, CommandOptions options = {});
        void EnqueueCommands(std::vector<Command> commands, CommandOptions options = {});
        [[nodiscard]] std::vector<CommandRecord> CommandLog() const;

        // Undoable commands applied between Begin and End merge into one undo step (a nested Begin ends the open one).
        void BeginTransaction(std::string label = {}, std::string coalesceKey = {});
        void EndTransaction();

        [[nodiscard]] bool CanUndo() const { return m_History.CanUndo(); }
        [[nodiscard]] bool CanRedo() const { return m_History.CanRedo(); }
        [[nodiscard]] std::string UndoLabel() const { return m_History.UndoLabel(); }
        [[nodiscard]] std::string RedoLabel() const { return m_History.RedoLabel(); }
        bool Undo();
        bool Redo();
        void ClearHistory() { m_History.Clear(); }

        // Ticks completed so far.
        [[nodiscard]] std::uint64_t Tick() const noexcept { return m_Tick.load(std::memory_order_acquire); }
        [[nodiscard]] std::uint64_t ContentRevision() const noexcept { return m_Revision.Current(); }
        // Sim-thread only (inside an enqueued edit).
        void MarkContentChanged() noexcept { m_Revision.Changed(); }
        void RestoreContentRevision(std::uint64_t revision) noexcept { m_Revision.Restore(revision); }

        // Advances exactly n fixed ticks regardless of pause or wall-clock; runs inline when the thread is not started.
        void Step(std::uint64_t ticks);
        // Same, but returns once the ticks ran and the resulting snapshot is published.
        void StepAndWait(std::uint64_t ticks);

        // Sets Scene::Clock().Time on the sim thread and returns once a snapshot carrying it is published.
        void SetSimTime(double seconds);

        [[nodiscard]] std::shared_ptr<SimulationRandom> Random() const noexcept { return m_Random; }

        // Sim-thread only (call from inside an enqueued edit) or while the thread is not running.
        [[nodiscard]] RunnerState CaptureState() const;
        void RestoreState(const RunnerState& state);

        // Scene, runner random state and module state hashed together; marshals onto the sim thread and waits.
        [[nodiscard]] std::uint64_t StateHash();
        // Checkpoints taken since PrepareReplay.
        [[nodiscard]] std::vector<StateCheckpoint> ReplayCheckpoints();

        // Both marshal onto the sim thread and wait. checkpointInterval 0 records no hashes.
        void BeginRecording(std::uint64_t checkpointInterval = 60);
        [[nodiscard]] SimulationRecording EndRecording();
        // Loads the recording into this fresh, not yet started runner; then StepAndWait(recording.Ticks()).
        void PrepareReplay(const SimulationRecording& recording);

    private:
        using Clock = std::chrono::steady_clock;

        enum class TransactionMarker : std::uint8_t { None, Begin, End };

        struct PendingEdit
        {
            std::move_only_function<void(Scene&)> Fn;
            bool Tracked = false;
            std::vector<Command> Commands;
            CommandOptions Options;
            bool System = false;
            TransactionMarker Marker = TransactionMarker::None;
        };

        struct PoolEntry
        {
            std::shared_ptr<SceneSnapshot::Data> Data;
            ChangeSet Pending;
        };

        void Enqueue(std::move_only_function<void(Scene&)> edit, bool tracked, bool system = false);
        void ThreadMain(std::stop_token stopToken);
        void Wake();
        bool DrainEdits();
        void ApplyCommands(PendingEdit& edit);
        void EnqueueMarker(TransactionMarker marker, CommandOptions options);
        void FlushTransaction();
        [[nodiscard]] std::uint64_t ComputeStateHash() const;
        void AddCheckpoint();
        void RunTick(float deltaTime, double clockDt);
        void StepOne(float deltaTime, double clockDt);
        void ExecuteSteps(std::uint64_t ticks);
        void PublishNow();
        [[nodiscard]] float StepDeltaTime() const;
        void EnsureInitialized();
        void ShutdownModules();
        void RunOnSimThread(std::function<void()> fn);
        bool Publish();

        Scene m_SimScene;
        std::vector<std::shared_ptr<ISimulationModule>> m_Modules;
        std::vector<const char*> m_ModuleZones;
        SimulationOptions m_Options;
        std::jthread m_Thread;

        std::atomic<bool> m_Paused{false};
        std::atomic<bool> m_Manual{false};
        void RecordInitError(std::string message);
        std::atomic<bool> m_Running{false};
        std::atomic<double> m_TickRate{60.0};

        std::mutex m_EditMutex;
        std::queue<PendingEdit> m_PendingEdits;
        std::vector<PendingEdit> m_Deferred;

        std::atomic<std::uint64_t> m_Tick{0};
        bool m_Initialized = false;
        std::mutex m_InlineMutex;
        std::atomic<std::uint64_t> m_StepsRequested{0};
        std::mutex m_StepMutex;
        std::condition_variable m_StepDone;
        std::uint64_t m_StepsCompleted = 0;

        std::shared_ptr<SimulationRandom> m_Random;
        CommandHistory m_History;
        std::optional<UndoEntry> m_Transaction; // sim thread only
        mutable std::mutex m_LogMutex;
        std::deque<CommandRecord> m_CommandLog;
        std::optional<SimulationRecording> m_Recording;
        std::mutex m_InitErrorMutex;
        RevisionCounter m_Revision;
        std::vector<std::string> m_InitErrors;

        struct CheckpointLog
        {
            std::uint64_t Interval{0};
            std::uint64_t Origin{0};
            std::vector<StateCheckpoint> Entries;
        };
        std::optional<CheckpointLog> m_Checkpoints; // sim thread only

        std::mutex m_WakeMutex;
        std::condition_variable_any m_Wake;
        std::atomic<std::uint64_t> m_WakeSignal{0};

        std::vector<PoolEntry> m_Pool;
        mutable std::mutex m_MetadataMutex;
        SceneMetadata m_Metadata;
        std::atomic<std::shared_ptr<const SceneSnapshot::Data>> m_Published;
        std::uint64_t m_Generation = 0;

        RateMeter m_TickMeter;
        std::atomic<std::int64_t> m_TickStartNs{0};
    };

    export class ScopedTransaction
    {
    public:
        explicit ScopedTransaction(SimulationRunner& runner, std::string label = {}, std::string coalesceKey = {})
            : m_Runner(runner)
        {
            m_Runner.BeginTransaction(std::move(label), std::move(coalesceKey));
        }
        ~ScopedTransaction() { m_Runner.EndTransaction(); }

        ScopedTransaction(const ScopedTransaction&) = delete;
        ScopedTransaction& operator=(const ScopedTransaction&) = delete;

    private:
        SimulationRunner& m_Runner;
    };
}
