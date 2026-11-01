module;
#include <entt/entt.hpp>
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

namespace GPP
{
    namespace
    {
        constexpr std::string_view kInstancePrelude = R"lua(
local instance = {}
local current = nil
function instance.bind(self)
    self.entity = gpp.script_entity()
    self.props = gpp.script_props()
    self.__sched = task.scheduler()
end
function instance.callbacks(mod)
    local bits = 0
    if type(mod.OnLateTick) == "function" then bits += 4 end
    if type(mod.OnEvent) == "function" then bits += 16 end
    if type(mod.OnContact) == "function" then bits += 32 end
    if type(mod.OnTrigger) == "function" then bits += 64 end
    return bits
end
function instance.start(mod, self)
    current = self
    self.props = gpp.script_props()
    local f = mod.OnStart
    if type(f) == "function" then self.__sched.spawn(f, self) end
    current = nil
end
function instance.tick(mod, self, dt)
    current = self
    self.props = gpp.script_props()
    local scheduler = self.__sched
    task.use(scheduler)
    scheduler.tick(dt)
    local fixed = mod.OnFixedTick
    if type(fixed) == "function" then fixed(self, dt) end
    local f = mod.OnTick
    if type(f) == "function" then f(self, dt) end
    current = nil
end
function instance.late(mod, self, dt)
    current = self
    task.use(self.__sched)
    mod.OnLateTick(self, dt)
    current = nil
end
function instance.event(mod, self, name, ...)
    current = self
    task.use(self.__sched)
    local on = self.__on
    local handlers = on and on[name]
    if handlers then
        for i = 1, #handlers do handlers[i](...) end
    end
    local f = mod.OnEvent
    if type(f) == "function" then f(self, name, ...) end
    current = nil
end
function instance.contact(mod, self, ...)
    current = self
    task.use(self.__sched)
    mod.OnContact(self, ...)
    current = nil
end
function instance.trigger(mod, self, ...)
    current = self
    task.use(self.__sched)
    mod.OnTrigger(self, ...)
    current = nil
end
function instance.stop(mod, self)
    current = self
    task.use(self.__sched)
    local f = mod.OnStop
    local ok, err = true, nil
    if type(f) == "function" then ok, err = pcall(f, self) end
    self.__sched.cancel_all()
    self.__on = nil
    current = nil
    if not ok then error(err, 0) end
end
function instance.cancel(self)
    self.__sched.cancel_all()
    self.__on = nil
end
gpp.instance = instance

function events.on(name, fn)
    if current == nil then error("events.on must be called from a script callback", 2) end
    if type(name) ~= "string" or type(fn) ~= "function" then error("events.on(name, fn) expects a string and a function", 2) end
    local on = current.__on
    if not on then on = {} current.__on = on end
    local list = on[name]
    if not list then list = {} on[name] = list end
    list[#list + 1] = fn
    gpp.listen()
end
function events.off(name, fn)
    if current == nil then error("events.off must be called from a script callback", 2) end
    local on = current.__on
    local list = on and on[name]
    if not list then return end
    local kept = {}
    for i = 1, #list do if list[i] ~= fn then kept[#kept + 1] = list[i] end end
    on[name] = kept
end
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
        vm.Register("gpp", "listen", [context](LuauNativeCall&)
        {
            context->Listening = true;
            return 0;
        });
        vm.Register("events", "emit", [context](LuauNativeCall& call)
        {
            auto name = call.String(1);
            if (name.empty()) throw std::runtime_error("events.emit: the name must be a non-empty string");
            std::vector<LuauValue> args;
            for (int i = 2; i <= call.Count(); ++i)
            {
                args.push_back(call.IsNumber(i) ? call.ToValue(i, LuauType::Double) : call.ToValueAuto(i));
            }
            context->Events->Emit(std::move(name), std::move(args));
            return 0;
        });
        if (auto result = vm.RunTrusted(kInstancePrelude, "=gpp_instance"); !result)
        {
            throw std::runtime_error("gpp instance prelude failed: " + result.error().Message);
        }
        return context;
    }

    struct ScriptHost::Impl
    {
        static constexpr int kHasLateTick = 4;
        static constexpr int kHasEvent = 16;
        static constexpr int kHasContact = 32;
        static constexpr int kHasTrigger = 64;

        struct Module
        {
            int Ref{0};
            std::size_t Hash{0};
            std::shared_ptr<LuauModuleTrace> Trace;
            int Callbacks{0};
        };

        struct Instance
        {
            std::uint64_t Entity{0};
            std::size_t Index{0};
            std::string Script;
            int Self{0};
            bool Started{false};
            bool Seen{false};
            bool Listening{false};
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
        ScriptEventBus::Token EventToken{0};
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
            EventToken = Context->Events->Subscribe({}, [this](const ScriptEvent& event) { DeliverEvent(event); });
        }

        ~Impl()
        {
            Context->Events->Unsubscribe(EventToken);
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
            Context->Listening = false;
            auto result = Vm.CallWith(Helper, function, refs, args);
            if (Context->Listening) instance.Listening = true;
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
                for (auto& [key, instance] : Instances)
                {
                    if (instance.Script != name || !instance.Started || instance.Self == 0) continue;
                    (void)Invoke(instance, "stop", {});
                }
                Vm.Release(module.Ref);
                module = Module{*handle, std::hash<std::string>{}(*source), std::move(trace), CallbackMask(*handle)};
                for (auto& [key, instance] : Instances)
                {
                    if (instance.Script != name) continue;
                    instance.Profile = {};
                    instance.Started = false;
                    instance.Listening = false;
                    instance.LastError.clear();
                }
            }
        }

        [[nodiscard]] int CallbackMask(const int moduleRef)
        {
            const std::array<int, 1> refs{moduleRef};
            const auto mask = Vm.CallWith(Helper, "callbacks", refs);
            const auto* bits = mask ? std::get_if<float>(&*mask) : nullptr;
            return bits ? static_cast<int>(*bits) : 0;
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
            Modules[name] = Module{*handle, std::hash<std::string>{}(*source), std::move(trace), CallbackMask(*handle)};
            return true;
        }

        void Destroy(Instance& instance)
        {
            if (instance.Self == 0) return;
            if (instance.Started && Modules.contains(instance.Script)) (void)Invoke(instance, "stop", {});
            else
            {
                const std::array<int, 1> self{instance.Self};
                (void)Vm.CallWith(Helper, "cancel", self);
            }
            Vm.Release(instance.Self);
            instance.Self = 0;
        }

        void Drain()
        {
            try
            {
                Context->Events->Dispatch();
            }
            catch (const std::exception& error)
            {
                Errors.push_back(ScriptError{0, 0, "events", error.what(), 0});
            }
            if (Context->Events->TakeOverflow())
            {
                Errors.push_back(ScriptError{0, 0, "events", "too many events emitted in one pass, the rest were dropped", 0});
            }
        }

        void DeliverEvent(const ScriptEvent& event)
        {
            std::vector<LuauValue> args;
            args.reserve(event.Args.size() + 1);
            args.emplace_back(event.Name);
            args.insert(args.end(), event.Args.begin(), event.Args.end());
            for (auto& [key, instance] : Instances)
            {
                const auto module = Modules.find(instance.Script);
                if (module == Modules.end() || !instance.Started || instance.Self == 0) continue;
                if (instance.Listening || (module->second.Callbacks & kHasEvent)) (void)Invoke(instance, "event", args);
            }
        }

        template <typename Visit>
        void ForInstancesOf(const std::uint64_t guid, const int needs, Visit&& visit)
        {
            for (auto it = Instances.lower_bound({guid, 0}); it != Instances.end() && it->first.first == guid; ++it)
            {
                const auto module = Modules.find(it->second.Script);
                if (module == Modules.end() || !it->second.Started || it->second.Self == 0) continue;
                if (module->second.Callbacks & needs) visit(it->second);
            }
        }

        void DeliverContact(const Scene& scene, const std::uint64_t self, const std::uint64_t other, const glm::dvec3& normal,
                            const glm::dvec3& velocity, const PhysicsContactEvent& event, const int material, const int otherMaterial)
        {
            if (!scene.IsValid(scene.FindByGuid(self))) return;
            const std::array<LuauValue, 7> args{other, event.Point, normal, event.Impulse, velocity, material, otherMaterial};
            ForInstancesOf(self, kHasContact, [&](Instance& instance) { (void)Invoke(instance, "contact", args); });
        }

        void DeliverTrigger(const Scene& scene, const std::uint64_t self, const std::uint64_t other, const bool entered)
        {
            if (!scene.IsValid(scene.FindByGuid(self))) return;
            const std::array<LuauValue, 2> args{other, entered};
            ForInstancesOf(self, kHasTrigger, [&](Instance& instance) { (void)Invoke(instance, "trigger", args); });
        }

        void Late(const Scene& scene, const float deltaTime, const std::span<const PhysicsContactEvent> contacts,
                  const std::span<const PhysicsTriggerEvent> triggers)
        {
            if (Helper == 0) return;
            for (const auto& event : contacts)
            {
                DeliverContact(scene, event.GuidA, event.GuidB, event.Normal, event.RelativeVelocity, event, event.MaterialA,
                               event.MaterialB);
                DeliverContact(scene, event.GuidB, event.GuidA, -event.Normal, -event.RelativeVelocity, event, event.MaterialB,
                               event.MaterialA);
            }
            for (const auto& event : triggers)
            {
                DeliverTrigger(scene, event.TriggerGuid, event.OtherGuid, event.Entered);
                DeliverTrigger(scene, event.OtherGuid, event.TriggerGuid, event.Entered);
            }
            Drain();
            const std::array<LuauValue, 1> args{static_cast<float>(deltaTime)};
            for (auto& [key, instance] : Instances)
            {
                const auto module = Modules.find(instance.Script);
                if (module == Modules.end() || !instance.Started || instance.Self == 0) continue;
                if (!(module->second.Callbacks & kHasLateTick) || !scene.IsValid(scene.FindByGuid(instance.Entity))) continue;
                (void)Invoke(instance, "late", args);
            }
            Drain();
        }

        void StopAll()
        {
            for (auto& [key, instance] : Instances) Destroy(instance);
            Instances.clear();
            Drain();
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

            std::ranges::sort(live, {}, &Pending::Key);

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
            Drain();
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
    void ScriptHost::Stop(const Scene&) { m_Impl->StopAll(); }
    ScriptEventBus& ScriptHost::Events() { return *m_Impl->Context->Events; }

    void ScriptHost::LateTick(const Scene& scene, const float deltaTime, const std::span<const PhysicsContactEvent> contacts,
                              const std::span<const PhysicsTriggerEvent> triggers)
    {
        m_Impl->Late(scene, deltaTime, contacts, triggers);
    }
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
