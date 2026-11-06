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
import :EventBus;
import :ScriptModule;
import :PhysicsBindings;
import :HudBindings;
import :ScriptTest;

namespace GPP
{
    namespace
    {
        constexpr std::string_view kPrelude = R"lua(
local traceback = debug.traceback
local registry = {}

local function show(v)
    if type(v) == "string" then return string.format("%q", v) end
    return tostring(v)
end

local function same(a, b)
    if a == b then return true end
    if type(a) ~= "table" or type(b) ~= "table" then return false end
    for k, v in a do if not same(v, b[k]) then return false end end
    for k in b do if a[k] == nil then return false end end
    return true
end

local function parts(v)
    local t = typeof(v)
    if t == "number" then return {v} end
    if t == "vector" or t == "dvec3" then return {v.x, v.y, v.z} end
    if t == "dquat" then return {v.x, v.y, v.z, v.w} end
    if t == "table" then return {v.x, v.y, v.z, v.w} end
    return nil
end

local function distance(a, b, relative)
    local pa, pb = parts(a), parts(b)
    if not pa or not pb or #pa ~= #pb then return nil end
    local worst = 0
    for i = 1, #pa do
        local d = math.abs(pa[i] - pb[i])
        if relative then d = d / math.max(math.abs(pb[i]), 1e-300) end
        if d ~= d then return math.huge end
        worst = math.max(worst, d)
    end
    return worst
end

local function message(base, extra)
    return extra and (extra .. ": " .. base) or base
end

function assert(condition, extra)
    if not condition then error(extra or "assertion failed", 2) end
    return condition
end

function assert_eq(actual, expected, extra)
    if not same(actual, expected) then
        error(message("expected " .. show(expected) .. ", got " .. show(actual), extra), 2)
    end
end

function assert_ne(actual, unexpected, extra)
    if same(actual, unexpected) then
        error(message("expected a value other than " .. show(unexpected), extra), 2)
    end
end

function assert_near(actual, expected, tolerance, extra)
    tolerance = tolerance or 1e-6
    local d = distance(actual, expected, false)
    if d == nil then error(message("assert_near needs numbers or vectors of the same size", extra), 2) end
    if not (d <= tolerance) then
        error(message("expected " .. show(expected) .. " within " .. tolerance .. ", got " .. show(actual) .. " (off by " .. d .. ")", extra), 2)
    end
end

function assert_rel(actual, expected, tolerance, extra)
    tolerance = tolerance or 1e-6
    local d = distance(actual, expected, true)
    if d == nil then error(message("assert_rel needs numbers or vectors of the same size", extra), 2) end
    if not (d <= tolerance) then
        error(message("expected " .. show(expected) .. " within a relative " .. tolerance .. ", got " .. show(actual) .. " (relative error " .. d .. ")", extra), 2)
    end
end

function expect_error(fn, needle)
    local ok, err = pcall(fn)
    if ok then error("expected an error, none was raised", 2) end
    err = tostring(err)
    if needle and not string.find(err, needle, 1, true) then
        error("expected an error containing " .. show(needle) .. ", got " .. show(err), 2)
    end
    return err
end

function test(name, fn)
    if type(name) ~= "string" or type(fn) ~= "function" then error("test(name, fn) expects a string and a function", 2) end
    table.insert(registry, {name = name, fn = fn})
end

local stepRaw = sim.__step
function sim.step(n)
    local err = stepRaw(n or 1)
    if err then error(err, 2) end
end

local inputRaw = sim.__input
function sim.input(action, value)
    if type(action) ~= "string" then error("sim.input(action, value?) expects the action name", 2) end
    if value == nil or value == true then value = 1 elseif value == false then value = 0 end
    inputRaw(action, value)
end

function sim.input_tap(action)
    sim.input(action, 1)
    sim.input(action, 0)
end

function sim.events(name)
    local list = {}
    for i = 1, sim.__event_count() do
        local eventName, tick, args = sim.__event(i)
        if name == nil or name == eventName then table.insert(list, {name = eventName, tick = tick, args = args}) end
    end
    return list
end

local KINDS = {[0] = "text", "line", "rect", "circle", "arc"}
local SPACES = {[0] = "screen", "left", "center", "right"}
local ALIGNS = {[0] = "left", "center", "right"}
function sim.hud()
    local list = {}
    for i = 1, sim.__hud_count() do
        local kind, space, align, filled, x, y, a, b, c, thickness, size, r, g, bl, al, text = sim.__hud(i)
        local entry = {kind = KINDS[kind], space = SPACES[space], x = x, y = y, thickness = thickness, color = {r = r, g = g, b = bl, a = al}}
        if kind == 0 then
            entry.text, entry.size, entry.align = text, size, ALIGNS[align]
        elseif kind == 1 then
            entry.x1, entry.y1, entry.x2, entry.y2 = x, y, a, b
        elseif kind == 2 then
            entry.w, entry.h, entry.filled = a, b, filled
        elseif kind == 3 then
            entry.radius, entry.filled = a, filled
        else
            entry.radius, entry.from, entry.to = a, b, c
        end
        table.insert(list, entry)
    end
    return list
end

function sim.define_script(name, source)
    local err = sim.__define_script(name, source)
    if err then error(err, 2) end
end

function sim.stop()
    local err = sim.__stop()
    if err then error(err, 2) end
end

function sim.add_component(entity, component)
    local ok, err = sim.__add_component(entity, component)
    if not ok then error(err, 2) end
end

function sim.attach(entity, script, props)
    local index, err = sim.__attach(entity, script)
    if not index then error(err, 2) end
    for name, value in props or {} do sim.__set_prop(entity, index, name, value) end
    return index
end

function sim.spawn(name, components)
    local entity = sim.create(name)
    for component, fields in components or {} do
        if component ~= "Transform" then
            local ok, err = sim.__add_component(entity, component)
            if not ok then error(err, 2) end
        end
        for field, value in fields do
            if not scene.set(entity, component, field, value) then
                error("cannot set " .. component .. "." .. field, 2)
            end
        end
    end
    return entity
end

local function handler(err)
    local lines = {type(err) == "string" and err or tostring(err)}
    for line in string.gmatch(traceback("", 2), "[^\n]+") do
        if not string.find(line, "^gpp_test:") then table.insert(lines, line) end
    end
    return table.concat(lines, "\n")
end

local testing = {}
function testing.count() return #registry end
function testing.name(i) return registry[i].name end
function testing.run(i)
    local ok, err = xpcall(registry[i].fn, handler)
    return ok and "" or err
end
gpp.testing = testing
)lua";

        std::string ReadText(const std::filesystem::path& path, bool& ok)
        {
            std::ifstream stream(path, std::ios::binary);
            ok = static_cast<bool>(stream);
            return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
        }

        std::vector<std::string> SplitLines(const std::string& text)
        {
            std::vector<std::string> lines;
            std::size_t begin = 0;
            while (begin <= text.size())
            {
                const auto end = text.find('\n', begin);
                lines.push_back(text.substr(begin, end == std::string::npos ? end : end - begin));
                if (end == std::string::npos) break;
                begin = end + 1;
            }
            return lines;
        }

        std::string DescribeScriptError(const Scene& scene, const ScriptError& error)
        {
            std::string entity = std::format("{}", error.Entity);
            const auto handle = scene.FindByGuid(error.Entity);
            if (scene.IsValid(handle))
            {
                if (const auto* metadata = scene.Registry().try_get<MetadataComponent>(handle); metadata && !metadata->Name.empty())
                {
                    entity = metadata->Name;
                }
            }
            return std::format("{} [script '{}' on entity '{}']", error.Message, error.Script, entity);
        }

        class TestWorld
        {
        public:
            explicit TestWorld(const ScriptTestOptions& options)
                : m_Dt(options.Dt), m_World("ScriptTest"),
                  m_Random(std::make_shared<SimulationRandom>(options.Seed))
            {
                RegisterBaseComponents();
                RegisterScriptComponents();

                static std::atomic<std::uint64_t> counter{0};
                m_ScriptDir = std::filesystem::temp_directory_path() /
                              std::format("gpp_script_test_{}_{}", std::chrono::steady_clock::now().time_since_epoch().count(),
                                          counter.fetch_add(1));
                std::filesystem::create_directories(m_ScriptDir);
                AssetOptions assetOptions;
                auto directories = options.Assets->Directories(kScriptAssetKind);
                directories.insert(directories.begin(), m_ScriptDir);
                assetOptions.Kinds[kScriptAssetKind] = AssetKindOptions{std::move(directories), {".lua", ".luau"}};
                m_Assets = std::make_unique<AssetDirectories>(std::move(assetOptions));
                m_Assets->Poll();

                ScriptModuleOptions moduleOptions{.Limits = options.Limits, .Extend = options.Extend, .ExtendScene = options.ExtendScene};
                m_Scripts = std::make_shared<ScriptSimulationModule>(*m_Assets, std::move(moduleOptions));
                m_Scripts->SetRandom(m_Random);
                m_Scripts->SetInput(m_Input);
                m_Modules.push_back(m_Scripts);
                if (options.MakeModules)
                {
                    for (auto& module : options.MakeModules()) m_Modules.push_back(std::move(module));
                }
                std::ranges::stable_sort(m_Modules, {}, [](const auto& module) { return module->Phase(); });
                for (const auto& module : m_Modules)
                {
                    if (auto physics = std::dynamic_pointer_cast<PhysicsSimulationModule>(module))
                    {
                        m_Physics = physics;
                        m_Scripts->SetPhysics(physics);
                    }
                }
                (void)m_Scripts->Host().Events().Subscribe({}, [this](const ScriptEvent& event)
                {
                    m_EventLog.push_back({event, m_Ticks});
                });

                m_Vm.SetLimits(options.Limits);
                m_Bindings->Read = &m_World;
                m_Bindings->Write = [this](std::function<void(Scene&)> write) { write(m_World); };
                RegisterMathBindings(m_Vm);
                RegisterRandomBindings(m_Vm, [this] { return m_Random.get(); });
                RegisterSceneBindings(m_Vm, m_Bindings);
                RegisterTaskBindings(m_Vm, m_TaskErrors);
                RegisterNatives();
                RegisterPhysicsBindings(m_Vm, m_Bindings, [this] { return m_Physics.get(); });
                if (options.Extend) options.Extend(m_Vm);
                if (options.ExtendScene) options.ExtendScene(m_Vm, m_Bindings);
                if (auto prelude = m_Vm.RunTrusted(kPrelude, "=gpp_test"); !prelude)
                {
                    throw std::runtime_error("script test prelude failed: " + prelude.error().Message);
                }
                m_Vm.Seal();
                m_Vm.SetModuleLoader(MakeScriptModuleLoader(*m_Assets));
                if (auto helper = m_Vm.Load("return gpp.testing", "=gpp_testing")) m_Helper = *helper;
            }

            ~TestWorld()
            {
                if (m_File != 0) m_Vm.Release(m_File);
                if (m_Helper != 0) m_Vm.Release(m_Helper);
                std::error_code ec;
                std::filesystem::remove_all(m_ScriptDir, ec);
            }

            TestWorld(const TestWorld&) = delete;
            TestWorld& operator=(const TestWorld&) = delete;

            [[nodiscard]] std::optional<LuauError> LoadFile(std::string_view source, const std::string& name)
            {
                auto trace = std::make_shared<LuauModuleTrace>();
                auto handle = m_Vm.Load(source, "=" + name, std::move(trace));
                if (!handle) return std::move(handle.error());
                m_File = *handle;
                if (auto errors = m_TaskErrors->Take(); !errors.empty()) return std::move(errors.front());
                return std::nullopt;
            }

            [[nodiscard]] std::size_t Count()
            {
                const auto value = m_Vm.CallWith(m_Helper, "count", {});
                if (!value) return 0;
                const auto* count = std::get_if<float>(&*value);
                return count ? static_cast<std::size_t>(*count) : 0;
            }

            [[nodiscard]] std::string Name(const std::size_t index)
            {
                const std::array<LuauValue, 1> args{static_cast<int>(index + 1)};
                const auto value = m_Vm.CallWith(m_Helper, "name", {}, args);
                const auto* name = value ? std::get_if<std::string>(&*value) : nullptr;
                return name ? *name : std::string{};
            }

            [[nodiscard]] std::string Run(const std::size_t index)
            {
                const std::array<LuauValue, 1> args{static_cast<int>(index + 1)};
                auto value = m_Vm.CallWith(m_Helper, "run", {}, args);
                if (!value) return value.error().Message;
                const auto* text = std::get_if<std::string>(&*value);
                if (text && !text->empty()) return *text;
                if (auto errors = m_TaskErrors->Take(); !errors.empty()) return "task error: " + errors.front().Message;
                return {};
            }

        private:
            std::string Step(const std::uint64_t ticks)
            {
                try
                {
                    if (!m_Initialized)
                    {
                        m_Initialized = true;
                        for (const auto& module : m_Modules) module->OnInit(m_World);
                        if (auto failure = ScriptFailure(); !failure.empty()) return failure;
                    }
                    const auto deltaTime = static_cast<float>(m_Dt);
                    if (m_Stopped) return "sim.step: the simulation was stopped";
                    for (std::uint64_t i = 0; i < ticks; ++i)
                    {
                        m_Input->Begin(m_Input->Sample());
                        for (const auto& module : m_Modules) module->OnTick(m_World, deltaTime);
                        for (const auto& module : m_Modules) module->OnLateTick(m_World, deltaTime);
                        m_World.AdvanceClock(m_Dt);
                        ++m_Ticks;
                        if (auto failure = ScriptFailure(); !failure.empty()) return failure;
                    }
                }
                catch (const std::exception& error)
                {
                    return std::string("simulation failed: ") + error.what();
                }
                m_Vm.ResetBudget();
                return {};
            }

            std::string Stop()
            {
                if (m_Stopped) return {};
                m_Stopped = true;
                if (!m_Initialized) return {};
                try
                {
                    for (const auto& module : m_Modules) module->OnShutdown(m_World);
                }
                catch (const std::exception& error)
                {
                    return std::string("simulation failed: ") + error.what();
                }
                return ScriptFailure();
            }

            std::string ScriptFailure()
            {
                auto errors = m_Scripts->TakeErrors();
                if (errors.empty()) return {};
                auto text = DescribeScriptError(m_World, errors.front());
                if (errors.size() > 1) text += std::format(" (+{} more script errors)", errors.size() - 1);
                return text;
            }

            entt::entity Find(const LuauNativeCall& call, const int index) const
            {
                const auto entity = m_World.FindByGuid(call.Entity(index));
                return m_World.IsValid(entity) ? entity : entt::entity{entt::null};
            }

            void RegisterNatives()
            {
                m_Vm.Register("sim", "create", [this](LuauNativeCall& call)
                {
                    const auto entity = m_World.CreateEntityWithGuid(m_NextGuid++, call.String(1));
                    m_World.Registry().emplace<TransformComponent>(entity);
                    call.PushEntity(m_World.GuidOf(entity));
                    return 1;
                });
                m_Vm.Register("sim", "destroy", [this](LuauNativeCall& call)
                {
                    const auto entity = Find(call, 1);
                    if (entity != entt::entity{entt::null}) m_World.DestroyEntity(entity);
                    call.PushBoolean(entity != entt::entity{entt::null});
                    return 1;
                });
                m_Vm.Register("sim", "__add_component", [this](LuauNativeCall& call)
                {
                    const auto name = call.String(2);
                    const auto entity = Find(call, 1);
                    const auto* info = ComponentRegistry::Instance().FindByName(name);
                    if (entity == entt::entity{entt::null} || !info || !info->Add)
                    {
                        call.PushNil();
                        call.PushString(entity == entt::entity{entt::null}
                                            ? "sim.add_component: no such entity"
                                            : "sim.add_component: unknown component '" + name + "'");
                        return 2;
                    }
                    if (!info->Has(m_World.Registry(), entity)) info->Add(m_World.Registry(), entity);
                    call.PushBoolean(true);
                    return 1;
                });
                m_Vm.Register("sim", "__attach", [this](LuauNativeCall& call)
                {
                    const auto entity = Find(call, 1);
                    if (entity == entt::entity{entt::null})
                    {
                        call.PushNil();
                        call.PushString("sim.attach: no such entity");
                        return 2;
                    }
                    auto& scripts = m_World.Registry().get_or_emplace<ScriptsComponent>(entity);
                    scripts.Entries.push_back(ScriptEntry{call.String(2), {}});
                    call.PushNumber(static_cast<double>(scripts.Entries.size()));
                    return 1;
                });
                m_Vm.Register("sim", "__set_prop", [this](LuauNativeCall& call)
                {
                    const auto entity = Find(call, 1);
                    const auto index = static_cast<std::size_t>(call.Number(2));
                    auto* scripts = entity != entt::entity{entt::null} ? m_World.Registry().try_get<ScriptsComponent>(entity) : nullptr;
                    if (!scripts || index == 0 || index > scripts->Entries.size()) return 0;
                    auto value = call.ToValueAuto(4);
                    if (!std::holds_alternative<std::monostate>(value)) scripts->Entries[index - 1].Props[call.String(3)] = std::move(value);
                    return 0;
                });
                m_Vm.Register("sim", "set_velocity", [this](LuauNativeCall& call)
                {
                    const auto entity = Find(call, 1);
                    if (entity == entt::entity{entt::null}) throw std::runtime_error("sim.set_velocity: no such entity");
                    auto& velocity = m_World.Registry().get_or_emplace<VelocityComponent>(entity);
                    if (const auto linear = call.ToValue(2, LuauType::DVec3); std::holds_alternative<glm::dvec3>(linear))
                    {
                        velocity.Linear = std::get<glm::dvec3>(linear);
                    }
                    if (const auto angular = call.ToValue(3, LuauType::DVec3); std::holds_alternative<glm::dvec3>(angular))
                    {
                        velocity.Angular = std::get<glm::dvec3>(angular);
                    }
                    return 0;
                });
                m_Vm.Register("sim", "__step", [this](LuauNativeCall& call)
                {
                    const auto ticks = static_cast<std::uint64_t>(std::max(0.0, call.Number(1)));
                    const auto failure = Step(ticks);
                    if (failure.empty()) return 0;
                    call.PushString(failure);
                    return 1;
                });
                m_Vm.Register("sim", "__input", [this](LuauNativeCall& call)
                {
                    m_Input->Set(call.String(1), static_cast<float>(call.Number(2)));
                    return 0;
                });
                m_Vm.Register("sim", "input_mouse", [this](LuauNativeCall& call)
                {
                    m_Input->AddMouse(glm::vec2(static_cast<float>(call.Number(1)), static_cast<float>(call.Number(2))));
                    return 0;
                });
                m_Vm.Register("sim", "input_clear", [this](LuauNativeCall&)
                {
                    m_Input->Reset();
                    return 0;
                });
                m_Vm.Register("sim", "__stop", [this](LuauNativeCall& call)
                {
                    const auto failure = Stop();
                    if (failure.empty()) return 0;
                    call.PushString(failure);
                    return 1;
                });
                m_Vm.Register("sim", "__define_script", [this](LuauNativeCall& call)
                {
                    const auto name = call.String(1);
                    const auto path = m_ScriptDir / (name + ".luau");
                    if (name.empty() || name.find("..") != std::string::npos)
                    {
                        call.PushString("sim.define_script: invalid script name '" + name + "'");
                        return 1;
                    }
                    std::filesystem::create_directories(path.parent_path());
                    {
                        std::ofstream out(path, std::ios::binary | std::ios::trunc);
                        out << call.String(2);
                    }
                    std::error_code ec;
                    std::filesystem::last_write_time(path, std::filesystem::file_time_type{} + std::chrono::seconds(++m_DefineCount), ec);
                    m_Assets->Poll();
                    return 0;
                });
                m_Vm.Register("sim", "emit", [this](LuauNativeCall& call)
                {
                    auto name = call.String(1);
                    if (name.empty()) throw std::runtime_error("sim.emit: the name must be a non-empty string");
                    std::vector<LuauValue> args;
                    for (int i = 2; i <= call.Count(); ++i)
                    {
                        args.push_back(call.IsNumber(i) ? call.ToValue(i, LuauType::Double) : call.ToValueAuto(i));
                    }
                    m_Scripts->Host().Events().Emit(std::move(name), std::move(args));
                    return 0;
                });
                m_Vm.Register("sim", "__event_count", [this](LuauNativeCall& call)
                {
                    call.PushNumber(static_cast<double>(m_EventLog.size()));
                    return 1;
                });
                m_Vm.Register("sim", "__event", [this](LuauNativeCall& call)
                {
                    const auto index = static_cast<std::size_t>(call.Number(1));
                    if (index == 0 || index > m_EventLog.size()) return 0;
                    const auto& [event, tick] = m_EventLog[index - 1];
                    call.PushString(event.Name);
                    call.PushNumber(static_cast<double>(tick));
                    call.PushValues(event.Args);
                    return 3;
                });
                m_Vm.Register("sim", "__hud_count", [this](LuauNativeCall& call)
                {
                    m_Hud = m_Scripts->HudSnapshot();
                    call.PushNumber(static_cast<double>(m_Hud->Commands.size()));
                    return 1;
                });
                m_Vm.Register("sim", "__hud", [this](LuauNativeCall& call)
                {
                    const auto index = static_cast<std::size_t>(call.Number(1));
                    if (!m_Hud || index == 0 || index > m_Hud->Commands.size()) return 0;
                    const auto& command = m_Hud->Commands[index - 1];
                    for (const double value : {static_cast<double>(command.Kind), static_cast<double>(command.Space),
                                               static_cast<double>(command.Align)})
                    {
                        call.PushNumber(value);
                    }
                    call.PushBoolean(command.Filled);
                    for (const float value : {command.X, command.Y, command.A, command.B, command.C, command.Thickness, command.Size,
                                              command.Color[0], command.Color[1], command.Color[2], command.Color[3]})
                    {
                        call.PushNumber(value);
                    }
                    call.PushString(command.Text);
                    return 16;
                });
                m_Vm.Register("sim", "dt", [this](LuauNativeCall& call)
                {
                    call.PushNumber(m_Dt);
                    return 1;
                });
                m_Vm.Register("sim", "set_dt", [this](LuauNativeCall& call)
                {
                    const double dt = call.Number(1);
                    if (!(dt > 0.0) || !std::isfinite(dt)) throw std::runtime_error("sim.set_dt: dt must be a positive number");
                    m_Dt = dt;
                    return 0;
                });
                m_Vm.Register("sim", "ticks", [this](LuauNativeCall& call)
                {
                    call.PushNumber(static_cast<double>(m_Ticks));
                    return 1;
                });
                m_Vm.Register("sim", "seed", [this](LuauNativeCall& call)
                {
                    const auto seed = static_cast<std::uint64_t>(call.Number(1));
                    if (seed == 0) throw std::runtime_error("sim.seed: seed must be a nonzero integer");
                    m_Random->Seed(seed);
                    return 0;
                });
                m_Vm.Register("sim", "state_hash", [this](LuauNativeCall& call)
                {
                    call.PushString(FormatHash(HashScene(m_World)));
                    return 1;
                });
            }

            double m_Dt;
            std::filesystem::path m_ScriptDir;
            std::unique_ptr<AssetDirectories> m_Assets;
            std::int64_t m_DefineCount{0};
            Scene m_World;
            std::shared_ptr<SimulationRandom> m_Random;
            std::shared_ptr<SimulationInput> m_Input = std::make_shared<SimulationInput>();
            std::shared_ptr<ScriptSimulationModule> m_Scripts;
            std::shared_ptr<const HudFrame> m_Hud;
            std::shared_ptr<PhysicsSimulationModule> m_Physics;
            std::vector<std::shared_ptr<ISimulationModule>> m_Modules;
            LuauVm m_Vm;
            std::shared_ptr<LuauTaskErrors> m_TaskErrors = std::make_shared<LuauTaskErrors>();
            std::shared_ptr<SceneBindings> m_Bindings = std::make_shared<SceneBindings>();
            std::uint64_t m_NextGuid{1};
            std::uint64_t m_Ticks{0};
            bool m_Initialized{false};
            bool m_Stopped{false};
            std::vector<std::pair<ScriptEvent, std::uint64_t>> m_EventLog;
            int m_Helper{0};
            int m_File{0};
        };

        std::string DisplayName(const std::filesystem::path& path, const ScriptTestOptions& options)
        {
            if (options.DisplayRoot)
            {
                std::error_code ec;
                const auto relative = std::filesystem::relative(path, *options.DisplayRoot, ec);
                if (!ec && !relative.empty() && *relative.begin() != "..") return relative.generic_string();
            }
            return path.generic_string();
        }
    }

    bool ScriptTestFile::Ok() const
    {
        return LoadError.empty() && !Tests.empty() &&
               std::ranges::all_of(Tests, [](const ScriptTestResult& test) { return test.Passed; });
    }

    std::vector<std::filesystem::path> FindScriptTests(const std::filesystem::path& root)
    {
        std::vector<std::filesystem::path> files;
        std::error_code ec;
        if (std::filesystem::is_regular_file(root, ec)) return {root};
        for (std::filesystem::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
        {
            if (it->is_regular_file(ec) && it->path().extension() == ".luau") files.push_back(it->path());
        }
        std::ranges::sort(files);
        return files;
    }

    ScriptTestFile RunScriptTestSource(const std::string_view source, const std::string& name,
                                       const ScriptTestOptions& options)
    {
        ScriptTestFile file{.Path = name};
        if (!options.Assets)
        {
            file.LoadError = "ScriptTestOptions.Assets is not set";
            return file;
        }

        const auto fail = [&](const std::string& text, const int line)
        {
            file.LoadError = text;
            file.LoadLine = line;
        };
        std::unique_ptr<TestWorld> world;
        const auto fresh = [&]
        {
            world = std::make_unique<TestWorld>(options);
            return world->LoadFile(source, name);
        };

        try
        {
            if (const auto error = fresh())
            {
                fail(error->Message, error->Line);
                return file;
            }
            const auto count = world->Count();
            if (count == 0)
            {
                fail(name + ": no tests registered, declare them with test(name, function() ... end)", 0);
                return file;
            }
            std::vector<std::string> names;
            for (std::size_t i = 0; i < count; ++i) names.push_back(world->Name(i));

            for (std::size_t i = 0; i < count; ++i)
            {
                if (i > 0 && fresh())
                {
                    fail(name + ": file failed to load again", 0);
                    return file;
                }
                const auto began = std::chrono::steady_clock::now();
                auto text = world->Run(i);
                ScriptTestResult result{.Name = names[i], .Passed = text.empty()};
                result.Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
                if (!text.empty())
                {
                    auto lines = SplitLines(text);
                    result.Message = std::move(lines.front());
                    result.Line = MakeLuauError(result.Message).Line;
                    result.Trace.assign(std::make_move_iterator(lines.begin() + 1), std::make_move_iterator(lines.end()));
                    const auto location = result.Message.substr(0, result.Message.find(':', result.Message.find(':') + 1));
                    if (!result.Trace.empty() && result.Trace.front().starts_with(location)) result.Trace.erase(result.Trace.begin());
                }
                file.Tests.push_back(std::move(result));
            }
        }
        catch (const std::exception& error)
        {
            fail(std::string("script test runner failed: ") + error.what(), 0);
        }
        return file;
    }

    ScriptTestFile RunScriptTestFile(const std::filesystem::path& path, const ScriptTestOptions& options)
    {
        const auto name = DisplayName(path, options);
        bool ok = false;
        const auto source = ReadText(path, ok);
        if (!ok)
        {
            ScriptTestFile file{.Path = name};
            file.LoadError = name + ": cannot read test file";
            return file;
        }
        return RunScriptTestSource(source, name, options);
    }

    ScriptTestReport RunScriptTests(const std::span<const std::filesystem::path> files, const ScriptTestOptions& options)
    {
        ScriptTestReport report;
        for (const auto& path : files)
        {
            auto file = RunScriptTestFile(path, options);
            if (!file.LoadError.empty()) ++report.Failed;
            for (const auto& test : file.Tests) ++(test.Passed ? report.Passed : report.Failed);
            report.Files.push_back(std::move(file));
        }
        return report;
    }

    std::string FormatScriptTestReport(const ScriptTestReport& report, const bool verbose)
    {
        std::string out;
        for (const auto& file : report.Files)
        {
            if (!file.LoadError.empty())
            {
                out += std::format("{}\n    while loading {}\n", file.LoadError, file.Path);
                continue;
            }
            for (const auto& test : file.Tests)
            {
                if (test.Passed)
                {
                    if (verbose) out += std::format("PASS {}: {} ({:.1f} ms)\n", file.Path, test.Name, test.Ms);
                    continue;
                }
                out += std::format("{}\n    in test '{}' of {}\n", test.Message, test.Name, file.Path);
                for (const auto& line : test.Trace) out += std::format("      {}\n", line);
            }
        }
        if (report.Files.empty()) out += "no test files found\n";
        out += std::format("{} passed, {} failed in {} file{}\n", report.Passed, report.Failed, report.Files.size(),
                           report.Files.size() == 1 ? "" : "s");
        return out;
    }
}
