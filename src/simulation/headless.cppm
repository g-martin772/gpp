export module GPP.Simulation:Headless;

import std;
import :Scene;
import :Runner;

namespace GPP
{
    export constexpr std::uint64_t kHeadlessDefaultSeed = 1;

    export struct HeadlessOptions
    {
        std::uint64_t Ticks{0};
        std::optional<double> Dt;
        std::optional<std::uint64_t> Seed;
        std::function<void(SimulationRunner&)> OnRunnerCreated;
    };

    export struct HeadlessResult
    {
        Scene Final;
        std::uint64_t Ticks{0};
        std::uint64_t Hash{0};
        double TickRate{0.0};
        double SimTime{0.0};
        double Elapsed{0.0};
        std::vector<std::string> InitErrors;
    };

    export [[nodiscard]] std::expected<HeadlessResult, std::string> RunHeadless(
        Scene scene, std::vector<std::shared_ptr<ISimulationModule>> modules, const HeadlessOptions& options);
}
