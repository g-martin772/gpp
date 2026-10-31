#include <catch2/catch_test_macros.hpp>

import GPP;
import std;

using namespace GPP;
Task<int> ExampleCoroutine(std::stop_token stopToken = {})
{
    Logger::LogInfo("Inside Coroutine Part 1. Thread ID: {}", std::this_thread::get_id());
    co_await DelayAsync(std::chrono::milliseconds(200), stopToken);
    Logger::LogInfo("Inside Coroutine Part 2. Thread ID: {}", std::this_thread::get_id());
    co_return 42;
}

Task<void> RunAsync()
{
    Logger::LogInfo("[Workflow] Starting workflow on thread: {}", std::this_thread::get_id());

    Task<int> databaseTask = WaitAsync(ExampleCoroutine, std::chrono::milliseconds(100));

    Logger::LogInfo("[Workflow] Database query dispatched!");

    try
    {
        int userId = co_await databaseTask;
        Logger::LogInfo("[Workflow] Resumed with result: {} on thread: {}", userId, std::this_thread::get_id());
    }
    catch (std::exception& e)
    {
        Logger::LogError("[Workflow] Exception caught: {} on thread: {}", e.what(), std::this_thread::get_id());
    }

    co_return;
}


TEST_CASE("Task Execution", "[task][coroutine]")
{
    Logger::LogInfo("Main Thread ID: {}", std::this_thread::get_id());

    Task<int> task = []() -> Task<int>
    {
        Logger::LogInfo("Inside Lambda. Thread ID: {}", std::this_thread::get_id());
        auto result = co_await ExampleCoroutine();
        co_return result;
    }();

    int result = task.get();
    Logger::LogInfo("Result: {}", result);
}


TEST_CASE("Task Timeout", "[task][coroutine]")
{
    Task<void> appTask = RunAsync();
    Logger::LogInfo("Main thread continues");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    Logger::LogInfo("Main Thread done");
    appTask.get();
}

TEST_CASE("Task then - value continuation", "[task][continuation]")
{
    auto base = []() -> Task<int>
    {
        co_await DelayAsync(std::chrono::milliseconds(10));
        co_return 2;
    }();

    auto cont = base.then([](int x) { return x + 3; });
    REQUIRE(cont.get() == 5);
}

TEST_CASE("Task then - task-returning continuation", "[task][continuation]")
{
    auto base = []() -> Task<int>
    {
        co_await DelayAsync(std::chrono::milliseconds(10));
        co_return 2;
    }();

    auto cont = base.then([](int x) -> Task<int>
    {
        co_await DelayAsync(std::chrono::milliseconds(5));
        co_return x * 4;
    });

    REQUIRE(cont.get() == 8);
}

TEST_CASE("Task<void> then -> value", "[task][continuation]")
{
    auto base = []() -> Task<void>
    {
        co_await DelayAsync(std::chrono::milliseconds(5));
        co_return;
    }();

    auto cont = base.then([]() { return 7; });
    REQUIRE(cont.get() == 7);
}

TEST_CASE("Task then -> Task<void> continuation", "[task][continuation]")
{
    std::atomic<bool> ran = false;
    auto base = []() -> Task<int>
    {
        co_await DelayAsync(std::chrono::milliseconds(10));
        co_return 2;
    }();

    const auto step = [&](int x) -> Task<void>
    {
        co_await DelayAsync(std::chrono::milliseconds(5));
        ran = (x == 2);
        co_return;
    };
    auto cont = base.then(step);

    cont.get();
    REQUIRE(ran);
}

namespace
{
    Task<int> Immediate(const int value)
    {
        co_return value;
    }

    Task<int> AwaitImmediate(const int value)
    {
        const int inner = co_await Immediate(value);
        co_return inner + 1;
    }
}

TEST_CASE("Awaiting a task that completes concurrently neither hangs nor touches a freed frame", "[GPP::Task][stress]")
{
    constexpr int kIterations = 20000;
    auto finished = std::async(std::launch::async, [&]
    {
        long long sum = 0;
        for (int i = 0; i < kIterations; ++i) sum += AwaitImmediate(i).get();
        return sum;
    });

    REQUIRE(finished.wait_for(std::chrono::seconds(60)) == std::future_status::ready);
    CHECK(finished.get() == static_cast<long long>(kIterations) * (kIterations - 1) / 2 + kIterations);
}
