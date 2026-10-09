module GPP.Scripting;

import std;
import :Luau;
import :Scheduler;

namespace GPP
{
    namespace
    {
        constexpr std::string_view kTaskPrelude = R"lua(
local resume, status, create, yield, isyieldable = coroutine.resume, coroutine.status, coroutine.create, coroutine.yield, coroutine.isyieldable
local report, budget = gpp.report, gpp.budget
local current = nil

local function suspend(kind, arg)
    if not isyieldable() then error("task.wait functions can only be used inside a task (use task.spawn)", 3) end
    return yield(kind, arg)
end

local task = {}
function task.wait(seconds) return suspend("time", type(seconds) == "number" and seconds or 0) end
function task.frame() return suspend("frame") end
function task.wait_until(predicate) return suspend("until", predicate) end
function task.wait_event(name) return suspend("event", name) end

function task.scheduler()
    local S = {}
    local time = 0
    local tasks = {}
    local nextId = 1

    local function step(t, ...)
        local previous = current
        current = S
        budget()
        local ok, kind, arg = resume(t.co, ...)
        budget()
        current = previous
        if not ok then
            tasks[t.id] = nil
            report(tostring(kind))
            return
        end
        if status(t.co) == "dead" then
            tasks[t.id] = nil
            return
        end
        t.wake, t.cond, t.event = nil, nil, nil
        if kind == "time" then t.wake = time + arg
        elseif kind == "frame" then t.wake = time
        elseif kind == "until" then t.cond = arg
        elseif kind == "event" then t.event = arg
        else t.wake = time end
    end

    local function snapshot(filter)
        local ids = {}
        for id, t in tasks do
            if filter(t) then ids[#ids + 1] = id end
        end
        table.sort(ids)
        return ids
    end

    local function launch(owner, fn, ...)
        local t = {id = nextId, co = create(fn), owner = owner}
        nextId += 1
        tasks[t.id] = t
        step(t, ...)
        return t.id
    end

    function S.spawn(fn, ...) return launch(nil, fn, ...) end
    function S.spawn_owned(owner, fn, ...) return launch(owner, fn, ...) end

    function S.tick(dt)
        time += dt
        for _, id in snapshot(function(t) return t.wake ~= nil or t.cond ~= nil end) do
            local t = tasks[id]
            if t then
                if t.wake ~= nil then
                    if time + 1e-9 >= t.wake then step(t, dt) end
                elseif t.cond ~= nil then
                    local previous = current
                    current = S
                    local ok, result = pcall(t.cond)
                    current = previous
                    if not ok then
                        tasks[id] = nil
                        report(tostring(result))
                    elseif result then
                        step(t, dt)
                    end
                end
            end
        end
    end

    function S.signal(name, ...)
        for _, id in snapshot(function(t) return t.event == name end) do
            local t = tasks[id]
            if t and t.event == name then step(t, ...) end
        end
    end

    function S.cancel(id) tasks[id] = nil end
    function S.cancel_owner(owner)
        for _, id in snapshot(function(t) return t.owner == owner end) do tasks[id] = nil end
    end
    function S.cancel_all() tasks = {} end
    function S.count()
        local n = 0
        for _ in tasks do n += 1 end
        return n
    end
    function S.time() return time end
    function S.use()
        local previous = current
        current = S
        return previous
    end
    return S
end

local function active()
    if not current then error("no active task scheduler", 3) end
    return current
end
function task.spawn(fn, ...) return active().spawn(fn, ...) end
function task.signal(name, ...) return active().signal(name, ...) end
function task.cancel(id) return active().cancel(id) end
function task.use(scheduler)
    local previous = current
    current = scheduler
    return previous
end

_G.task = task
)lua";
    }

    void RegisterTaskBindings(LuauVm& vm, const std::shared_ptr<LuauTaskErrors>& errors)
    {
        vm.Register("gpp", "budget", [&vm](LuauNativeCall&)
        {
            vm.ResetBudget();
            return 0;
        });
        vm.Register("gpp", "report", [errors](LuauNativeCall& call)
        {
            errors->Items.push_back(MakeLuauError(call.String(1)));
            return 0;
        });
        if (auto result = vm.RunTrusted(kTaskPrelude, "=gpp_task"); !result)
        {
            throw std::runtime_error("gpp task prelude failed: " + result.error().Message);
        }
    }

    LuauScheduler::LuauScheduler(LuauVm& vm, std::shared_ptr<LuauTaskErrors> errors)
        : m_Vm(vm), m_Errors(std::move(errors))
    {
        if (auto handle = m_Vm.Load("return task.scheduler()", "=scheduler")) m_Handle = *handle;
    }

    LuauScheduler::~LuauScheduler()
    {
        if (m_Handle != 0) m_Vm.Release(m_Handle);
    }

    void LuauScheduler::Tick(const float deltaTime)
    {
        const std::array<double, 1> args{deltaTime};
        (void)m_Vm.Call(m_Handle, "tick", args);
    }

    void LuauScheduler::Signal(const std::string_view event)
    {
        const std::array<LuauValue, 1> args{std::string(event)};
        (void)m_Vm.CallWith(m_Handle, "signal", {}, args);
    }

    void LuauScheduler::CancelAll()
    {
        (void)m_Vm.Call(m_Handle, "cancel_all");
    }

    std::size_t LuauScheduler::Active() const
    {
        const auto result = m_Vm.CallWith(m_Handle, "count", {});
        const auto* count = result ? std::get_if<float>(&*result) : nullptr;
        return count ? static_cast<std::size_t>(*count) : 0;
    }

    double LuauScheduler::Time() const
    {
        const auto result = m_Vm.CallWith(m_Handle, "time", {});
        const auto* time = result ? std::get_if<float>(&*result) : nullptr;
        return time ? *time : 0.0;
    }
}
