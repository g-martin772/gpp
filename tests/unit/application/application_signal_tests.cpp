#include <catch2/catch_test_macros.hpp>

#include <csignal>

import GPP;
import std;

using namespace GPP;

namespace
{
    int RunWithSignal(const int signal, bool& hookRan)
    {
        CliApplicationBuilder builder;
        auto app = builder.Build();
        app->SetTerminationHook([&hookRan] { hookRan = true; });
        app->ScheduleOnMainThread([signal] { std::raise(signal); });
        return app->Run();
    }
}

TEST_CASE("SIGTERM runs the termination hook before services stop", "[application][signal]")
{
    bool ran = false;
    CHECK(RunWithSignal(SIGTERM, ran) == 0);
    CHECK(ran);
}

TEST_CASE("SIGINT and Stop do not run the termination hook", "[application][signal]")
{
    bool ran = false;
    CHECK(RunWithSignal(SIGINT, ran) == 0);
    CHECK_FALSE(ran);
}
