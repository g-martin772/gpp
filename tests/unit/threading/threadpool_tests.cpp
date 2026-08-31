#include <catch2/catch_test_macros.hpp>

import GPP;
import std;

using namespace GPP;
using namespace std::chrono_literals;

TEST_CASE("ThreadPool resumes coroutines through ResumeOn", "[threadpool][resume]") {
    ThreadPool pool(1);
    std::mutex mutex;
    std::condition_variable condition;
    bool resumed = false;

    auto task = [&]() -> AsyncVoid {
        co_await ResumeOn(pool);
        {
            std::scoped_lock lock(mutex);
            resumed = true;
        }
        condition.notify_one();
    };

    task();

    std::unique_lock lock(mutex);
    const bool resumedOnPool = condition.wait_for(lock, std::chrono::seconds(2), [&] { return resumed; });
    CHECK(resumedOnPool);
}

TEST_CASE("Application lifecycle and main-thread scheduling", "[application][main-thread]") {
    auto builder = App::CreateCliBuilder();
    auto app = builder.Build();

    std::thread::id appThreadId{};
    std::thread::id queuedTaskThreadId{};
    std::thread::id resumeTaskThreadId{};
    std::binary_semaphore appStarted{0};
    std::binary_semaphore queuedTaskRan{0};
    std::binary_semaphore resumeTaskRan{0};

    std::thread appThread([&]() {
        appThreadId = std::this_thread::get_id();
        app.Run();
    });

    std::this_thread::sleep_for(100ms);
    Application::Instance().ScheduleOnMainThread([&]() {
        queuedTaskThreadId = std::this_thread::get_id();
        appStarted.release();
    });

    REQUIRE(appStarted.try_acquire_for(2s));

    auto resumeTask = [&]() -> AsyncVoid {
        co_await ResumeOn(Application::Instance());
        resumeTaskThreadId = std::this_thread::get_id();
        resumeTaskRan.release();
    };
    resumeTask();

    REQUIRE(resumeTaskRan.try_acquire_for(2s));
    REQUIRE(queuedTaskThreadId == appThreadId);
    REQUIRE(resumeTaskThreadId == appThreadId);
    REQUIRE(appThreadId != std::this_thread::get_id());

    Application::Instance().Stop();
    appThread.join();

    REQUIRE(appThread.joinable() == false);
}

AsyncVoid TestAsync($string query) {
    Logger::LogInfo("[Caller Thread ID: {}] Starting query coroutine...", std::this_thread::get_id());

    co_await GPP::ResumeOn(ThreadPool::Instance());

    Logger::LogInfo("[Worker Thread ID: {}] Processing query: '{}'...", std::this_thread::get_id(), query);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    Logger::LogInfo("[Worker Thread ID: {}] Finished operations.", std::this_thread::get_id());

    co_return;
}

TEST_CASE("ThreadPool async", "[threadpool][async]") {
    ThreadPool pool(2);

    std::vector<std::string> queries = {"SELECT * FROM users;", "UPDATE users SET name='Alice' WHERE id=1;", "DELETE FROM users WHERE id=2;"};

    for (const auto& query : queries) {
        TestAsync(query);
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));

    for (int i = 0; i < 10; ++i)
    {
        ThreadPool::Instance().Submit([i]() {
            Logger::LogInfo("Task {} is running in the thread pool.", i);
        });
    }

    Logger::LogInfo("ThreadPool Worker Count: {}", ThreadPool::Instance().WorkerCount());

}