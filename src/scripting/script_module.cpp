module GPP.Scripting;

import std;
import GPP.Core;
import GPP.Simulation;
import :Luau;
import :Bindings;
import :Scheduler;
import :EventBus;
import :ScriptComponents;
import :ScriptHost;
import :ScriptModule;

namespace GPP
{
    struct ScriptSimulationModule::Impl
    {
        LuauVm Vm;
        std::shared_ptr<LuauTaskErrors> TaskErrors = std::make_shared<LuauTaskErrors>();
        std::shared_ptr<SceneBindings> Bindings = std::make_shared<SceneBindings>();
        std::shared_ptr<SimulationRandom> Random;
        std::shared_ptr<PhysicsSimulationModule> Physics;
        std::unique_ptr<ScriptHost> Host;
        std::vector<std::function<void(Scene&)>> Writes;
        std::mutex ErrorMutex;
        std::vector<ScriptError> Errors;

        Impl(const AssetDirectories& assets, ScriptModuleOptions& options)
        {
            RegisterScriptComponents();
            Vm.SetLimits(options.Limits);
            RegisterMathBindings(Vm);
            RegisterRandomBindings(Vm, [this] { return Random.get(); });
            RegisterSceneBindings(Vm, Bindings);
            RegisterTaskBindings(Vm, TaskErrors);
            auto context = RegisterScriptHostBindings(Vm);
            if (options.Extend) options.Extend(Vm);
            Vm.Seal();
            Host = std::make_unique<ScriptHost>(Vm, std::move(context), TaskErrors, assets);
        }

        void Run(Scene& scene, const std::function<void()>& step)
        {
            Bindings->Read = &scene;
            Bindings->Write = [this](std::function<void(Scene&)> write) { Writes.push_back(std::move(write)); };
            step();
            Bindings->Read = nullptr;
            Bindings->Write = nullptr;
            for (auto& write : std::exchange(Writes, {})) write(scene);
            if (auto errors = Host->TakeErrors(); !errors.empty())
            {
                std::scoped_lock lock(ErrorMutex);
                for (auto& error : errors) Errors.push_back(std::move(error));
            }
        }
    };

    ScriptSimulationModule::ScriptSimulationModule(const AssetDirectories& assets, ScriptModuleOptions options)
        : m_Impl(std::make_unique<Impl>(assets, options))
    {
    }

    ScriptSimulationModule::~ScriptSimulationModule() = default;

    void ScriptSimulationModule::SetRandom(std::shared_ptr<SimulationRandom> random) { m_Impl->Random = std::move(random); }

    void ScriptSimulationModule::SetPhysics(std::shared_ptr<PhysicsSimulationModule> physics)
    {
        if (m_Impl->Physics) m_Impl->Physics->SetEventCapture(false);
        m_Impl->Physics = std::move(physics);
        if (m_Impl->Physics) m_Impl->Physics->SetEventCapture(true);
    }

    void ScriptSimulationModule::OnInit(Scene& scene)
    {
        m_Impl->Run(scene, [&] { m_Impl->Host->Start(scene); });
    }

    void ScriptSimulationModule::OnTick(Scene& scene, const float deltaTime)
    {
        m_Impl->Run(scene, [&] { m_Impl->Host->Tick(scene, deltaTime); });
    }

    void ScriptSimulationModule::OnLateTick(Scene& scene, const float deltaTime)
    {
        PhysicsEventBatch events;
        if (m_Impl->Physics) events = m_Impl->Physics->TakeEvents();
        m_Impl->Run(scene, [&] { m_Impl->Host->LateTick(scene, deltaTime, events.Contacts, events.Triggers); });
    }

    void ScriptSimulationModule::OnShutdown(Scene& scene)
    {
        m_Impl->Run(scene, [&] { m_Impl->Host->Stop(scene); });
    }

    std::vector<ScriptError> ScriptSimulationModule::TakeErrors()
    {
        std::scoped_lock lock(m_Impl->ErrorMutex);
        return std::exchange(m_Impl->Errors, {});
    }

    ScriptHost& ScriptSimulationModule::Host() { return *m_Impl->Host; }
}
