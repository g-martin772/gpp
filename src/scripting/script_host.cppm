export module GPP.Scripting:ScriptHost;

import std;
import GPP.Core;
import GPP.Simulation;
import :Luau;
import :Bindings;
import :Scheduler;
import :ScriptComponents;

export namespace GPP
{
    // The instance being called, read by the gpp.script_entity / gpp.script_props natives.
    struct ScriptHostContext
    {
        std::uint64_t Entity{0};
        std::vector<std::pair<std::string, FieldValue>> Props;
    };

    struct ScriptError
    {
        std::uint64_t Entity{0};
        std::size_t Index{0};
        std::string Script;
        std::string Message;
        int Line{0};
    };

    // Message is empty while the instance is healthy.
    struct ScriptStatus
    {
        std::uint64_t Entity{0};
        std::size_t Index{0};
        std::string Script;
        std::string Message;
    };

    // Needs RegisterTaskBindings first; call before LuauVm::Seal().
    [[nodiscard]] std::shared_ptr<ScriptHostContext> RegisterScriptHostBindings(LuauVm& vm);

    // Runs the `.luau` scripts attached to entities through ScriptsComponent. A script returns
    // { properties = {...}, OnStart = function(self) end, OnTick = function(self, dt) end } and receives
    // self.entity, self.props (declared properties, refreshed every tick) and a private task scheduler, so OnStart may
    // task.wait(); OnTick may task.spawn(). Suspended tasks resume at the start of the tick, before OnTick. Scripts are rebuilt when their source changes; self survives the rebuild.
    class ScriptHost
    {
    public:
        ScriptHost(LuauVm& vm, std::shared_ptr<ScriptHostContext> context, std::shared_ptr<LuauTaskErrors> taskErrors,
                   const AssetDirectories& assets);
        ~ScriptHost();
        ScriptHost(const ScriptHost&) = delete;
        ScriptHost& operator=(const ScriptHost&) = delete;

        // Entries whose name this predicate accepts belong to someone else (e.g. component graphs).
        void SetIgnored(std::function<bool(const std::string&)> ignored);

        void Start(const Scene& scene);
        void Tick(const Scene& scene, float deltaTime);

        [[nodiscard]] std::vector<ScriptError> TakeErrors();
        [[nodiscard]] std::vector<ScriptStatus> Statuses() const;
        [[nodiscard]] std::size_t InstanceCount() const;
        [[nodiscard]] ScriptCatalog& Catalog();

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
