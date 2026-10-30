module;
#include <entt/entt.hpp>
module GPP.Scripting;

import std;
import GPP.Core;
import GPP.Simulation;
import :Luau;
import :Bindings;
import :Scheduler;
import :ScriptComponents;
import :ScriptHost;

namespace GPP
{
    namespace
    {
        constexpr std::string_view kInstancePrelude = R"lua(
local instance = {}
function instance.bind(self)
    self.entity = gpp.script_entity()
    self.props = gpp.script_props()
    self.__sched = task.scheduler()
end
function instance.start(mod, self)
    self.props = gpp.script_props()
    local f = mod.OnStart
    if type(f) == "function" then self.__sched.spawn(f, self) end
end
function instance.tick(mod, self, dt)
    self.props = gpp.script_props()
    local scheduler = self.__sched
    task.use(scheduler)
    scheduler.tick(dt)
    local f = mod.OnTick
    if type(f) == "function" then f(self, dt) end
end
function instance.stop(self) self.__sched.cancel_all() end
gpp.instance = instance
)lua";
    }

    std::shared_ptr<ScriptHostContext> RegisterScriptHostBindings(LuauVm& vm)
    {
        auto context = std::make_shared<ScriptHostContext>();
        vm.Register("gpp", "script_entity", [context](LuauNativeCall& call)
        {
            call.PushEntity(context->Entity);
            return 1;
        });
        vm.Register("gpp", "script_props", [context](LuauNativeCall& call)
        {
            call.PushMap(context->Props);
            return 1;
        });
        if (auto result = vm.RunTrusted(kInstancePrelude, "=gpp_instance"); !result)
        {
            throw std::runtime_error("gpp instance prelude failed: " + result.error().Message);
        }
        return context;
    }

    struct ScriptHost::Impl
    {
        struct Module
        {
            int Ref{0};
            std::size_t Hash{0};
            std::shared_ptr<LuauModuleTrace> Trace;
        };

        struct Instance
        {
            std::uint64_t Entity{0};
            std::size_t Index{0};
            std::string Script;
            int Self{0};
            bool Started{false};
            bool Seen{false};
            std::string LastError;
            ScriptProfile Profile;
        };

        LuauVm& Vm;
        std::shared_ptr<ScriptHostContext> Context;
        std::shared_ptr<LuauTaskErrors> TaskErrors;
        const AssetDirectories& Assets;
        ScriptCatalog Catalog;
        std::function<bool(const std::string&)> Ignored;
        int Helper{0};
        std::map<std::string, Module> Modules;
        std::map<std::pair<std::uint64_t, std::size_t>, Instance> Instances;
        std::map<std::string, std::string> ModuleErrors;
        std::map<std::string, std::shared_ptr<const ScriptDescriptor>> Descriptors;
        std::vector<ScriptError> Errors;
        std::uint64_t SeenVersion{~0ull};

        Impl(LuauVm& vm, std::shared_ptr<ScriptHostContext> context, std::shared_ptr<LuauTaskErrors> taskErrors,
             const AssetDirectories& assets)
            : Vm(vm), Context(std::move(context)), TaskErrors(std::move(taskErrors)), Assets(assets),
              Catalog(assets, std::chrono::milliseconds(0))
        {
            Vm.SetModuleLoader(MakeScriptModuleLoader(assets));
            if (auto helper = Vm.Load("return gpp.instance", "=instance")) Helper = *helper;
        }

        ~Impl()
        {
            for (auto& [key, instance] : Instances) Vm.Release(instance.Self);
            for (auto& [name, module] : Modules) Vm.Release(module.Ref);
            if (Helper != 0) Vm.Release(Helper);
        }

        void Fail(Instance& instance, LuauError error)
        {
            if (error.Message == instance.LastError) return;
            instance.LastError = error.Message;
            Errors.push_back(ScriptError{instance.Entity, instance.Index, instance.Script, std::move(error.Message), error.Line});
        }

        void Prepare(const Instance& instance, const ScriptEntry& entry)
        {
            Context->Entity = instance.Entity;
            auto& descriptor = Descriptors[instance.Script];
            if (!descriptor) descriptor = Catalog.Describe(instance.Script);
            Context->Props = ResolveProps(*descriptor, entry);
        }

        void Record(Instance& instance, const std::chrono::steady_clock::time_point began, const LuauAllocStats before)
        {
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
            const auto after = Vm.AllocStats();
            auto& profile = instance.Profile;
            profile.LastAllocs = after.Allocations - before.Allocations;
            profile.LastBytes = after.Bytes - before.Bytes;
            profile.LastMs = ms;
            profile.MaxMs = std::max(profile.MaxMs, ms);
            constexpr double kBlend = 0.1;
            const bool first = profile.Ticks++ == 0;
            profile.AvgMs = first ? ms : profile.AvgMs + (ms - profile.AvgMs) * kBlend;
            profile.AvgBytes = first ? static_cast<double>(profile.LastBytes)
                                     : profile.AvgBytes + (static_cast<double>(profile.LastBytes) - profile.AvgBytes) * kBlend;
            profile.VmBytes = Vm.BytesUsed();
        }

        bool Invoke(Instance& instance, const std::string_view function, const std::span<const LuauValue> args)
        {
            const std::array<int, 2> refs{Modules.at(instance.Script).Ref, instance.Self};
            auto result = Vm.CallWith(Helper, function, refs, args);
            auto asyncErrors = TaskErrors->Take();
            const bool clean = result.has_value() && asyncErrors.empty();
            if (!result) Fail(instance, std::move(result.error()));
            for (auto& error : asyncErrors) Fail(instance, std::move(error));
            return clean;
        }

        void ReloadChanged()
        {
            if (Assets.Version() == SeenVersion) return;
            SeenVersion = Assets.Version();
            Descriptors.clear();
            for (auto& [name, module] : Modules)
            {
                const auto source = Assets.Read(kScriptAssetKind, name);
                if (!source) continue;
                if (std::hash<std::string>{}(*source) == module.Hash && !Vm.ModulesStale(*module.Trace)) continue;
                auto trace = std::make_shared<LuauModuleTrace>();
                auto handle = Vm.Load(*source, "=" + name, trace);
                if (!handle)
                {
                    ModuleErrors[name] = handle.error().Message;
                    continue;
                }
                ModuleErrors.erase(name);
                Vm.Release(module.Ref);
                module = Module{*handle, std::hash<std::string>{}(*source), std::move(trace)};
                for (auto& [key, instance] : Instances)
                {
                    if (instance.Script != name) continue;
                    instance.Profile = {};
                    const std::array<int, 1> self{instance.Self};
                    (void)Vm.CallWith(Helper, "stop", self);
                    instance.Started = false;
                    instance.LastError.clear();
                }
            }
        }

        bool EnsureModule(const std::string& name, std::string& error)
        {
            if (Modules.contains(name)) return true;
            const auto source = Assets.Read(kScriptAssetKind, name);
            if (!source)
            {
                error = "script '" + name + "' not found";
                return false;
            }
            auto trace = std::make_shared<LuauModuleTrace>();
            auto handle = Vm.Load(*source, "=" + name, trace);
            if (!handle)
            {
                error = handle.error().Message;
                return false;
            }
            Modules[name] = Module{*handle, std::hash<std::string>{}(*source), std::move(trace)};
            return true;
        }

        void Destroy(Instance& instance)
        {
            if (instance.Self == 0) return;
            const std::array<int, 1> self{instance.Self};
            (void)Vm.CallWith(Helper, "stop", self);
            Vm.Release(instance.Self);
            instance.Self = 0;
        }

        void Update(const Scene& scene, const float deltaTime, const bool tick)
        {
            if (Helper == 0) return;
            ReloadChanged();
            for (auto& [key, instance] : Instances) instance.Seen = false;

            struct Pending
            {
                std::pair<std::uint64_t, std::size_t> Key;
                const ScriptEntry* Entry;
            };
            std::vector<Pending> live;
            for (auto [entity, scripts, metadata] : scene.Registry().view<const ScriptsComponent, const MetadataComponent>().each())
            {
                for (std::size_t i = 0; i < scripts.Entries.size(); ++i)
                {
                    const auto& entry = scripts.Entries[i];
                    if (entry.Name.empty() || (Ignored && Ignored(entry.Name))) continue;
                    live.push_back({{metadata.Guid, i}, &entry});
                }
            }

            for (const auto& item : live)
            {
                auto it = Instances.find(item.Key);
                if (it != Instances.end() && it->second.Script != item.Entry->Name)
                {
                    Destroy(it->second);
                    Instances.erase(it);
                    it = Instances.end();
                }
                if (it == Instances.end())
                {
                    Instance created;
                    created.Entity = item.Key.first;
                    created.Index = item.Key.second;
                    created.Script = item.Entry->Name;
                    it = Instances.emplace(item.Key, std::move(created)).first;
                }
                it->second.Seen = true;
            }
            for (auto it = Instances.begin(); it != Instances.end();)
            {
                if (it->second.Seen) { ++it; continue; }
                Destroy(it->second);
                it = Instances.erase(it);
            }

            for (const auto& item : live)
            {
                auto& instance = Instances.at(item.Key);
                std::string loadError;
                if (!EnsureModule(instance.Script, loadError))
                {
                    Fail(instance, MakeLuauError(loadError, instance.Script));
                    continue;
                }
                if (const auto reload = ModuleErrors.find(instance.Script); reload != ModuleErrors.end())
                {
                    Fail(instance, MakeLuauError(reload->second, instance.Script));
                }
                Prepare(instance, *item.Entry);
                if (instance.Self == 0)
                {
                    instance.Self = Vm.NewTable();
                    const std::array<int, 1> self{instance.Self};
                    auto bound = Vm.CallWith(Helper, "bind", self);
                    if (!bound) { Fail(instance, std::move(bound.error())); continue; }
                }
                bool healthy = true;
                if (!instance.Started)
                {
                    instance.Started = true;
                    healthy = Invoke(instance, "start", {});
                }
                if (tick && healthy)
                {
                    const std::array<LuauValue, 1> args{static_cast<float>(deltaTime)};
                    const auto allocsBefore = Vm.AllocStats();
                    const auto began = std::chrono::steady_clock::now();
                    healthy = Invoke(instance, "tick", args);
                    Record(instance, began, allocsBefore);
                }
                if (healthy && !ModuleErrors.contains(instance.Script)) instance.LastError.clear();
            }
        }
    };

    ScriptHost::ScriptHost(LuauVm& vm, std::shared_ptr<ScriptHostContext> context,
                           std::shared_ptr<LuauTaskErrors> taskErrors, const AssetDirectories& assets)
        : m_Impl(std::make_unique<Impl>(vm, std::move(context), std::move(taskErrors), assets))
    {
    }

    ScriptHost::~ScriptHost() = default;

    void ScriptHost::SetIgnored(std::function<bool(const std::string&)> ignored) { m_Impl->Ignored = std::move(ignored); }
    void ScriptHost::Start(const Scene& scene) { m_Impl->Update(scene, 0.0f, false); }
    void ScriptHost::Tick(const Scene& scene, const float deltaTime) { m_Impl->Update(scene, deltaTime, true); }
    std::vector<ScriptError> ScriptHost::TakeErrors() { return std::exchange(m_Impl->Errors, {}); }
    std::size_t ScriptHost::InstanceCount() const { return m_Impl->Instances.size(); }
    ScriptCatalog& ScriptHost::Catalog() { return m_Impl->Catalog; }

    std::vector<ScriptStatus> ScriptHost::Statuses() const
    {
        std::vector<ScriptStatus> result;
        for (const auto& [key, instance] : m_Impl->Instances)
        {
            result.push_back({instance.Entity, instance.Index, instance.Script, instance.LastError, instance.Profile});
        }
        return result;
    }
}
