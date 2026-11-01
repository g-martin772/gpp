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
import :PhysicsBindings;
import :HudBindings;

namespace GPP
{
    struct ScriptSimulationModule::Impl
    {
        LuauVm Vm;
        std::shared_ptr<LuauTaskErrors> TaskErrors = std::make_shared<LuauTaskErrors>();
        std::shared_ptr<SceneBindings> Bindings = std::make_shared<SceneBindings>();
        std::shared_ptr<SimulationRandom> Random;
        std::shared_ptr<PhysicsSimulationModule> Physics;
        std::shared_ptr<SimulationInput> Input;
        std::shared_ptr<HudCanvas> Hud = std::make_shared<HudCanvas>();
        bool DeferStart{false};
        std::unique_ptr<ScriptHost> Host;
        std::mutex SharedMutex;
        std::vector<ScriptEvent> Posted;
        std::set<std::string> Ignored;
        std::vector<ScriptStatus> Statuses;
        std::atomic<bool> Reload{false};
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
            RegisterInputBindings();
            RegisterPhysicsBindings(Vm, Bindings, [this] { return Physics.get(); });
            RegisterHudBindings(Vm, Hud);
            auto context = RegisterScriptHostBindings(Vm);
            if (options.Extend) options.Extend(Vm);
            if (options.ExtendScene) options.ExtendScene(Vm, Bindings);
            Vm.Seal();
            DeferStart = options.DeferStart;
            Host = std::make_unique<ScriptHost>(Vm, std::move(context), TaskErrors, assets);
            Host->SetIgnored([this](const std::string& name)
            {
                std::scoped_lock lock(SharedMutex);
                return Ignored.contains(name);
            });
        }

        void RegisterInputBindings()
        {
            auto& vm = Vm;
            vm.Register("input", "axis", [this](LuauNativeCall& call)
            {
                call.PushNumber(Input ? Input->Axis(call.String(1)) : 0.0);
                return 1;
            });
            vm.Register("input", "held", [this](LuauNativeCall& call)
            {
                call.PushBoolean(Input && Input->Held(call.String(1)));
                return 1;
            });
            vm.Register("input", "pressed", [this](LuauNativeCall& call)
            {
                call.PushBoolean(Input && Input->Pressed(call.String(1)));
                return 1;
            });
            vm.Register("input", "released", [this](LuauNativeCall& call)
            {
                call.PushBoolean(Input && Input->Released(call.String(1)));
                return 1;
            });
            vm.Register("input", "mouse_delta", [this](LuauNativeCall& call)
            {
                call.PushValue(Input ? Input->MouseDelta() : glm::vec2(0.0f));
                return 1;
            });
        }

        void Publish()
        {
            auto statuses = Host->Statuses();
            std::scoped_lock lock(SharedMutex);
            Statuses = std::move(statuses);
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

    void ScriptSimulationModule::SetInput(std::shared_ptr<SimulationInput> input) { m_Impl->Input = std::move(input); }

    void ScriptSimulationModule::Stop(Scene& scene)
    {
        m_Impl->Run(scene, [&] { m_Impl->Host->Stop(scene); });
        m_Impl->Hud->Clear();
        m_Impl->Publish();
        {
            std::scoped_lock lock(m_Impl->SharedMutex);
            m_Impl->Posted.clear();
        }
        if (m_Impl->Input) m_Impl->Input->ResetUsed();
    }

    void ScriptSimulationModule::PostEvent(std::string name, std::vector<LuauValue> args)
    {
        std::scoped_lock lock(m_Impl->SharedMutex);
        m_Impl->Posted.push_back(ScriptEvent{std::move(name), std::move(args)});
    }

    void ScriptSimulationModule::SetIgnored(std::set<std::string> names)
    {
        std::scoped_lock lock(m_Impl->SharedMutex);
        m_Impl->Ignored = std::move(names);
    }

    std::vector<ScriptStatus> ScriptSimulationModule::Statuses() const
    {
        std::scoped_lock lock(m_Impl->SharedMutex);
        return m_Impl->Statuses;
    }

    void ScriptSimulationModule::OnInit(Scene& scene)
    {
        if (m_Impl->DeferStart) return;
        m_Impl->Run(scene, [&] { m_Impl->Host->Start(scene); });
    }

    void ScriptSimulationModule::OnTick(Scene& scene, const float deltaTime)
    {
        m_Impl->Hud->Reset();
        if (m_Impl->Reload.exchange(false)) m_Impl->Host->ForceReload();
        std::vector<ScriptEvent> posted;
        {
            std::scoped_lock lock(m_Impl->SharedMutex);
            posted.swap(m_Impl->Posted);
        }
        for (auto& event : posted) m_Impl->Host->Events().Emit(std::move(event.Name), std::move(event.Args));
        m_Impl->Run(scene, [&] { m_Impl->Host->Tick(scene, deltaTime); });
        m_Impl->Publish();
    }

    void ScriptSimulationModule::OnLateTick(Scene& scene, const float deltaTime)
    {
        PhysicsEventBatch events;
        if (m_Impl->Physics) events = m_Impl->Physics->TakeEvents();
        m_Impl->Run(scene, [&] { m_Impl->Host->LateTick(scene, deltaTime, events.Contacts, events.Triggers); });
        m_Impl->Hud->Publish();
    }

    void ScriptSimulationModule::RequestReload() { m_Impl->Reload.store(true); }
    std::string ScriptSimulationModule::SaveState() const { return m_Impl->Host->SaveState(); }
    void ScriptSimulationModule::LoadState(const std::string& state) { m_Impl->Host->LoadState(state); }

    std::shared_ptr<const HudFrame> ScriptSimulationModule::HudSnapshot() const { return m_Impl->Hud->Snapshot(); }

    void ScriptSimulationModule::OnShutdown(Scene& scene)
    {
        m_Impl->Run(scene, [&] { m_Impl->Host->Stop(scene); });
        m_Impl->Hud->Clear();
    }

    std::vector<ScriptError> ScriptSimulationModule::TakeErrors()
    {
        std::scoped_lock lock(m_Impl->ErrorMutex);
        return std::exchange(m_Impl->Errors, {});
    }

    ScriptHost& ScriptSimulationModule::Host() { return *m_Impl->Host; }
}
