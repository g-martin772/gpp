#include <catch2/catch_test_macros.hpp>

import GPP;
import std;

using namespace GPP;

namespace
{
    struct FailingInitModule final : ISimulationModule
    {
        void OnInit(Scene&) override { throw std::runtime_error("init boom"); }
    };
}

TEST_CASE("A module that fails to init is reported once and the runner keeps running", "[simulation_runner]")
{
    SimulationRunner runner(Scene("InitError"), std::make_shared<FailingInitModule>());
    runner.Step(1);

    const auto errors = runner.TakeInitErrors();
    REQUIRE(errors.size() == 1);
    CHECK(errors.front() == "init boom");
    CHECK(runner.TakeInitErrors().empty());
}
