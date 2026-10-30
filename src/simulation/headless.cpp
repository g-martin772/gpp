module GPP.Simulation;

import :Headless;
import std;

namespace GPP
{
    std::expected<HeadlessResult, std::string> RunHeadless(Scene scene,
                                                           std::vector<std::shared_ptr<ISimulationModule>> modules,
                                                           const HeadlessOptions& options)
    {
        SimulationOptions simulation;
        if (options.Dt)
        {
            const double dt = *options.Dt;
            const double rate = dt > 0.0 && std::isfinite(dt) ? 1.0 / dt : 0.0;
            if (!(rate >= SimulationRunner::kMinTickRate && rate <= SimulationRunner::kMaxTickRate))
            {
                return std::unexpected(std::format("dt must be between {:g} and {:g} seconds, got {:g}",
                                                   1.0 / SimulationRunner::kMaxTickRate,
                                                   1.0 / SimulationRunner::kMinTickRate, dt));
            }
            simulation.FixedTimestep = std::chrono::duration<float>(static_cast<float>(dt));
        }

        const auto sceneSeed = scene.Metadata().Seed;
        SimulationRunner runner(std::move(scene), std::move(modules), simulation);
        runner.Random()->Seed(options.Seed ? *options.Seed : (sceneSeed != 0 ? sceneSeed : kHeadlessDefaultSeed));
        if (options.OnRunnerCreated) options.OnRunnerCreated(runner);
        runner.StepAndWait(options.Ticks);

        const auto snapshot = runner.AcquireSnapshot();
        HeadlessResult result{.Final = snapshot->Clone()};
        result.Ticks = runner.Tick();
        result.Hash = runner.StateHash();
        result.TickRate = runner.GetTickRate();
        result.SimTime = result.Final.Clock().Time;
        result.Elapsed = result.Final.Clock().Elapsed;
        result.InitErrors = runner.TakeInitErrors();
        return result;
    }
}
