#include <catch2/catch_test_macros.hpp>

#include "../../../build/vcpkg_installed/x64-linux/include/catch2/catch_test_macros.hpp"

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
