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

local SKIP = { entity = true, props = true, __sched = true, __on = true }
local MAXDEPTH = 12
local function enc(v, out, depth, seen)
    local t = type(v)
    if t == "number" then
        if v ~= v then out[#out + 1] = "x;"
        elseif v == math.huge then out[#out + 1] = "i;"
        elseif v == -math.huge then out[#out + 1] = "j;"
        else out[#out + 1] = "n" .. string.format("%.17g", v) .. ";" end
    elseif t == "boolean" then out[#out + 1] = v and "t" or "f"
    elseif t == "string" then out[#out + 1] = "s" .. #v .. ":" .. v
    elseif t == "vector" then out[#out + 1] = string.format("v%.9g,%.9g,%.9g;", v.x, v.y, v.z)
    elseif t == "userdata" then out[#out + 1] = "e" .. gpp.entity_guid(v) .. ";"
    elseif t == "table" then
        local mt = getmetatable(v)
        if mt == vec_meta.vec2 then out[#out + 1] = string.format("a%.9g,%.9g;", v.x, v.y)
        elseif mt == vec_meta.vec4 then out[#out + 1] = string.format("b%.9g,%.9g,%.9g,%.9g;", v.x, v.y, v.z, v.w)
        elseif depth >= MAXDEPTH or seen[v] then out[#out + 1] = "z"
        else
            seen[v] = true
            local items = {}
            for k, value in v do
                local kt, vt = type(k), type(value)
                if (kt == "string" or kt == "number") and vt ~= "function" and vt ~= "thread" and not (depth == 0 and SKIP[k]) then
                    local key = {}
                    enc(k, key, depth + 1, seen)
                    local body = {}
                    enc(value, body, depth + 1, seen)
                    items[#items + 1] = { table.concat(key), table.concat(body) }
                end
            end
            table.sort(items, function(a, b) return a[1] < b[1] end)
            out[#out + 1] = "{"
            for _, item in items do
                out[#out + 1] = item[1]
                out[#out + 1] = item[2]
            end
            out[#out + 1] = "}"
            seen[v] = nil
        end
    else out[#out + 1] = "z" end
end
function instance.save(self)
    local out = {}
    enc(self, out, 0, {})
    return table.concat(out)
end
local function dec(s, pos)
    local c = string.sub(s, pos, pos)
    if c == "t" then return true, pos + 1
    elseif c == "f" then return false, pos + 1
    elseif c == "z" then return nil, pos + 1
    elseif c == "x" then return 0 / 0, pos + 2
    elseif c == "i" then return math.huge, pos + 2
    elseif c == "j" then return -math.huge, pos + 2
    elseif c == "n" then
        local e = string.find(s, ";", pos, true)
        return tonumber(string.sub(s, pos + 1, e - 1)), e + 1
    elseif c == "s" then
        local colon = string.find(s, ":", pos, true)
        local len = tonumber(string.sub(s, pos + 1, colon - 1))
        return string.sub(s, colon + 1, colon + len), colon + len + 1
    elseif c == "v" or c == "a" or c == "b" then
        local e = string.find(s, ";", pos, true)
        local p = {}
        for n in string.gmatch(string.sub(s, pos + 1, e - 1), "[^,]+") do p[#p + 1] = tonumber(n) end
        if c == "v" then return vector.create(p[1], p[2], p[3]), e + 1 end
        if c == "a" then return vec2(p[1], p[2]), e + 1 end
        return vec4(p[1], p[2], p[3], p[4]), e + 1
    elseif c == "e" then
        local e = string.find(s, ";", pos, true)
        return gpp.entity_from(string.sub(s, pos + 1, e - 1)), e + 1
    elseif c == "{" then
        local t = {}
        pos += 1
        while string.sub(s, pos, pos) ~= "}" do
            local k, v
            k, pos = dec(s, pos)
            v, pos = dec(s, pos)
            if k ~= nil then t[k] = v end
        end
        return t, pos + 1
    end
    error("corrupt script state at " .. pos)
end
function instance.restore(self, data)
    for k in self do
        if not SKIP[k] then self[k] = nil end
    end
    local t = dec(data, 1)
    for k, v in t do self[k] = v end
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
        vm.Register("gpp", "entity_guid", [](LuauNativeCall& call)
        {
            call.PushString(std::to_string(call.Entity(1)));
            return 1;
        });
        vm.Register("gpp", "entity_from", [](LuauNativeCall& call)
        {
            std::uint64_t guid = 0;
            const auto text = call.String(1);
            std::from_chars(text.data(), text.data() + text.size(), guid);
            call.PushEntity(guid);
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

        struct SavedInstance
        {
            bool Started{false};
            std::string Script;
            std::string Data;
        };
        std::map<std::pair<std::uint64_t, std::size_t>, SavedInstance> Pending;

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

        void LoadSelf(Instance& instance, const std::string& data)
        {
            const std::array<int, 1> self{instance.Self};
            const std::array<LuauValue, 1> args{data};
            auto result = Vm.CallWith(Helper, "restore", self, args);
            if (!result) Fail(instance, std::move(result.error()));
        }

        void TakePending(Instance& instance)
        {
            const auto it = Pending.find({instance.Entity, instance.Index});
            if (it == Pending.end()) return;
            if (it->second.Script == instance.Script)
            {
                instance.Started = it->second.Started;
                LoadSelf(instance, it->second.Data);
            }
            Pending.erase(it);
        }

        [[nodiscard]] std::string Save() const
        {
            std::map<std::pair<std::uint64_t, std::size_t>, std::string> chunks;
            const auto write = [&](const std::pair<std::uint64_t, std::size_t>& key, const bool started, const std::string& script,
                                   const std::string& data)
            {
                chunks[key] = std::format("{} {} {} {} {} {}\n", key.first, key.second, started ? 1 : 0, script.size(), data.size(), script) + data + '\n';
            };
            for (const auto& [key, saved] : Pending) write(key, saved.Started, saved.Script, saved.Data);
            for (const auto& [key, instance] : Instances)
            {
                if (instance.Self == 0) continue;
                const std::array<int, 1> self{instance.Self};
                auto saved = Vm.CallWith(Helper, "save", self);
                const auto* data = saved ? std::get_if<std::string>(&*saved) : nullptr;
                if (data) write(key, instance.Started, instance.Script, *data);
            }
            std::string text;
            for (const auto& [key, chunk] : chunks) text += chunk;
            return text;
        }

        void Load(const std::string& text)
        {
            Pending.clear();
            std::size_t pos = 0;
            while (pos < text.size())
            {
                const auto eol = text.find('\n', pos);
                if (eol == std::string::npos) break;
                std::uint64_t entity = 0, index = 0, started = 0, scriptLength = 0, dataLength = 0;
                const char* cursor = text.data() + pos;
                const char* end = text.data() + eol;
                const auto next = [&](std::uint64_t& value)
                {
                    const auto [stop, ec] = std::from_chars(cursor, end, value);
                    if (ec != std::errc{}) return false;
                    cursor = stop < end ? stop + 1 : stop;
                    return true;
                };
                if (!next(entity) || !next(index) || !next(started) || !next(scriptLength) || !next(dataLength)) return;
                if (cursor + scriptLength != end || eol + 1 + dataLength > text.size()) return;
                SavedInstance saved{started != 0, std::string(cursor, scriptLength), text.substr(eol + 1, dataLength)};
                pos = eol + 1 + dataLength + 1;
                const auto it = Instances.find({entity, index});
                if (it != Instances.end() && it->second.Self != 0 && it->second.Script == saved.Script)
                {
                    it->second.Started = saved.Started;
                    LoadSelf(it->second, saved.Data);
                }
                else Pending[{entity, index}] = std::move(saved);
            }
        }

        void StopAll()
        {
            Pending.clear();
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
                    TakePending(instance);
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
    void ScriptHost::ForceReload() { m_Impl->SeenVersion = ~0ull; }
    std::string ScriptHost::SaveState() const { return m_Impl->Save(); }
    void ScriptHost::LoadState(const std::string& state) { m_Impl->Load(state); }
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
