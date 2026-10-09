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
        std::uint32_t MaxCatchUpTicks = 3;
        double MaxPublishRate = 240.0;
        double StallThresholdMs = 1000.0;
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
            Scene Value;
            std::uint64_t Generation;
        };

        SceneSnapshot() = default;
        explicit SceneSnapshot(std::shared_ptr<const Data> data) noexcept : m_Data(std::move(data)) {}

        const Scene* operator->() const noexcept { return &m_Data->Value; }
        const Scene& operator*() const noexcept { return m_Data->Value; }
        explicit operator bool() const noexcept { return m_Data != nullptr; }

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
        [[nodiscard]] bool IsRunning() const noexcept { return m_Running.load(std::memory_order_acquire); }

        void SetTickRate(double hertz);
        [[nodiscard]] double GetTickRate() const noexcept { return m_TickRate.load(std::memory_order_relaxed); }
        static constexpr double kMinTickRate = 1.0;
        static constexpr double kMaxTickRate = 2000.0;

        [[nodiscard]] SimulationStats GetStats() const noexcept;

        [[nodiscard]] SceneSnapshot AcquireSnapshot() const { return SceneSnapshot(m_Published.load(std::memory_order_acquire)); }
        [[nodiscard]] SceneSnapshot LockRenderScene() const { return AcquireSnapshot(); }
        [[nodiscard]] const SceneMetadata& Metadata() const noexcept { return m_SimScene.Metadata(); }

        void EnqueueEdit(std::move_only_function<void(Scene&)> edit);

    private:
        using Clock = std::chrono::steady_clock;

        void ThreadMain(std::stop_token stopToken);
        void Wake();
        bool DrainEdits();
        void RunTick(float deltaTime);
        bool Publish();

        Scene m_SimScene;
        std::vector<std::shared_ptr<ISimulationModule>> m_Modules;
        SimulationOptions m_Options;
        std::jthread m_Thread;

        std::atomic<bool> m_Paused{false};
        std::atomic<bool> m_Running{false};
        std::atomic<double> m_TickRate{60.0};

        std::mutex m_EditMutex;
        std::queue<std::move_only_function<void(Scene&)>> m_PendingEdits;

        std::mutex m_WakeMutex;
        std::condition_variable_any m_Wake;
        std::uint64_t m_WakeSignal = 0;

        std::vector<std::shared_ptr<SceneSnapshot::Data>> m_Pool;
        std::atomic<std::shared_ptr<const SceneSnapshot::Data>> m_Published;
        std::uint64_t m_Generation = 0;

        RateMeter m_TickMeter;
        std::atomic<std::int64_t> m_TickStartNs{0};
    };
}
