export module GPP.Scripting:ScriptTest;

import std;
import GPP.Core;
import GPP.Simulation;
import :Luau;
import :Bindings;

export namespace GPP
{
    struct ScriptTestOptions
    {
        const AssetDirectories* Assets{nullptr};
        std::uint64_t Seed{1};
        double Dt{1.0 / 60.0};
        LuauLimits Limits{.MaxSafepoints = 100'000'000, .MaxSeconds = 10.0};
        std::function<std::vector<std::shared_ptr<ISimulationModule>>()> MakeModules;
        std::function<void(LuauVm&)> Extend;
        std::function<void(LuauVm&, const std::shared_ptr<SceneBindings>&)> ExtendScene;
        std::optional<std::filesystem::path> DisplayRoot;
    };

    struct ScriptTestResult
    {
        std::string Name;
        bool Passed{false};
        std::string Message;
        int Line{0};
        std::vector<std::string> Trace;
        double Ms{0.0};
    };

    struct ScriptTestFile
    {
        std::string Path;
        std::string LoadError;
        int LoadLine{0};
        std::vector<ScriptTestResult> Tests;

        [[nodiscard]] bool Ok() const;
    };

    struct ScriptTestReport
    {
        std::vector<ScriptTestFile> Files;
        std::size_t Passed{0};
        std::size_t Failed{0};

        [[nodiscard]] bool Ok() const { return Failed == 0 && !Files.empty(); }
    };

    [[nodiscard]] std::vector<std::filesystem::path> FindScriptTests(const std::filesystem::path& root);

    [[nodiscard]] ScriptTestFile RunScriptTestSource(std::string_view source, const std::string& name,
                                                     const ScriptTestOptions& options);
    [[nodiscard]] ScriptTestFile RunScriptTestFile(const std::filesystem::path& path, const ScriptTestOptions& options);
    [[nodiscard]] ScriptTestReport RunScriptTests(std::span<const std::filesystem::path> files,
                                                  const ScriptTestOptions& options);

    [[nodiscard]] std::string FormatScriptTestReport(const ScriptTestReport& report, bool verbose = false);
}
