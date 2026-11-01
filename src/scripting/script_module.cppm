export module GPP.Scripting:ScriptModule;

import std;
import GPP.Core;
import GPP.Simulation;
import :Luau;
import :Bindings;
import :EventBus;
import :ScriptHost;

export namespace GPP
{
    struct ScriptModuleOptions
    {
        LuauLimits Limits;
        std::function<void(LuauVm&)> Extend;
        std::function<void(LuauVm&, const std::shared_ptr<SceneBindings>&)> ExtendScene;
        bool DeferStart{false};
    };

    class ScriptSimulationModule final : public ISimulationModule
    {
    public:
        explicit ScriptSimulationModule(const AssetDirectories& assets, ScriptModuleOptions options = {});
        ~ScriptSimulationModule() override;
        ScriptSimulationModule(const ScriptSimulationModule&) = delete;
        ScriptSimulationModule& operator=(const ScriptSimulationModule&) = delete;

        void SetRandom(std::shared_ptr<SimulationRandom> random);

        void SetPhysics(std::shared_ptr<PhysicsSimulationModule> physics);

        void SetInput(std::shared_ptr<SimulationInput> input);

        void Stop(Scene& scene);

        void PostEvent(std::string name, std::vector<LuauValue> args = {});

        void SetIgnored(std::set<std::string> names);

        [[nodiscard]] std::vector<ScriptStatus> Statuses() const;

        void OnInit(Scene& scene) override;
        void OnTick(Scene& scene, float deltaTime) override;
        void OnLateTick(Scene& scene, float deltaTime) override;
        void OnShutdown(Scene& scene) override;
        [[nodiscard]] std::string SaveState() const override;
        void LoadState(const std::string& state) override;

        void RequestReload();

        [[nodiscard]] std::vector<ScriptError> TakeErrors();
        [[nodiscard]] ScriptHost& Host();

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
