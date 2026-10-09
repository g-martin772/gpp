export module GPP.Scripting:Scheduler;

import std;
import :Luau;

export namespace GPP
{
    // Errors raised inside tasks are collected here instead of unwinding through the host that resumed them.
    struct LuauTaskErrors
    {
        std::vector<LuauError> Items;

        [[nodiscard]] std::vector<LuauError> Take() { return std::exchange(Items, {}); }
    };

    // Installs the `task` library (wait, wait_until, wait_event, frame, spawn, signal, cancel, scheduler) and its error
    // sink. Call before LuauVm::Seal().
    void RegisterTaskBindings(LuauVm& vm, const std::shared_ptr<LuauTaskErrors>& errors);

    // A set of coroutines resumed by Tick: tasks suspend on wait(seconds), wait_until(predicate), wait_event(name) or
    // frame(), and the clock only advances when the host ticks, so pausing the host pauses every task.
    class LuauScheduler
    {
    public:
        LuauScheduler(LuauVm& vm, std::shared_ptr<LuauTaskErrors> errors);
        ~LuauScheduler();
        LuauScheduler(const LuauScheduler&) = delete;
        LuauScheduler& operator=(const LuauScheduler&) = delete;

        [[nodiscard]] bool Ok() const { return m_Handle != 0; }
        // Reference to the Lua-side scheduler object (spawn, signal, tick, cancel_all, ...).
        [[nodiscard]] int Handle() const { return m_Handle; }

        void Tick(float deltaTime);
        void Signal(std::string_view event);
        void CancelAll();
        [[nodiscard]] std::size_t Active() const;
        [[nodiscard]] double Time() const;
        [[nodiscard]] const std::shared_ptr<LuauTaskErrors>& Errors() const { return m_Errors; }

    private:
        LuauVm& m_Vm;
        std::shared_ptr<LuauTaskErrors> m_Errors;
        int m_Handle{0};
    };
}
