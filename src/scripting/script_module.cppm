export module GPP.Scripting:ScriptModule;

import std;
import GPP.Core;
import GPP.Simulation;
import :Luau;
import :EventBus;
import :ScriptHost;

export namespace GPP
{
    struct ScriptModuleOptions
    {
        LuauLimits Limits;
        std::function<void(LuauVm&)> Extend;
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

        void OnInit(Scene& scene) override;
        void OnTick(Scene& scene, float deltaTime) override;
        void OnLateTick(Scene& scene, float deltaTime) override;
        void OnShutdown(Scene& scene) override;

        [[nodiscard]] std::vector<ScriptError> TakeErrors();
        [[nodiscard]] ScriptHost& Host();

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
