#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_all.hpp>

import GPP;
import std;

using namespace GPP;
using namespace std::chrono_literals;

TEST_CASE("ThreadPool Basic Functionality", "[GPP::Threading]") {
    auto& pool = ThreadPool::Instance();

    SECTION("Worker threads are initialized") {
        REQUIRE(pool.WorkerCount() > 0);
    }

    SECTION("Tasks are executed on background worker threads") {
        std::binary_semaphore sem{0};
        std::thread::id workerThreadId{};

        pool.Submit([&]() {
            workerThreadId = std::this_thread::get_id();
            sem.release();
        });

        sem.acquire();
        REQUIRE(workerThreadId != std::this_thread::get_id());
    }
}

TEST_CASE("Task<T> Promise and Future Semantics", "[GPP::Task]") {
    auto& pool = ThreadPool::Instance();

    SECTION("Task executes eagerly on ThreadPool and returns value via .get()") {
        Task<int> task = []() -> Task<int> {
            co_return 1337;
        }();

        int result = task.get();
        REQUIRE(result == 1337);
    }

    SECTION("Task correctly propagates exceptions") {
        Task<int> task = []() -> Task<int> {
            throw std::runtime_error("Database failure");
            co_return 0;
        }();

        REQUIRE_THROWS_AS(task.get(), std::runtime_error);
    }

    SECTION("Task<void> executes and completes successfully") {
        bool completed = false;
        Task<void> task = [&]() -> Task<void> {
            completed = true;
            co_return;
        }();

        task.get();
        REQUIRE(completed == true);
    }
}

TEST_CASE("TimerSubsystem & DelayAsync", "[GPP::Timer]") {
    SECTION("DelayAsync suspends execution and resumes without thread blockage") {
        auto startTime = std::chrono::steady_clock::now();

        Task<void> task = []() -> Task<void> {
            co_await DelayAsync(100ms);
        }();

        task.get();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime);

        REQUIRE(elapsed >= 100ms);
    }
}

TEST_CASE("WaitAsync Timeout and Detached Cancellation", "[GPP::WaitAsync]") {
    SECTION("WaitAsync succeeds if target completes within timeout") {
        Task<int> fastTask = []() -> Task<int> {
            co_await DelayAsync(50ms);
            co_return 42;
        }();

        Task<int> timedTask = WaitAsync(std::move(fastTask), 150ms);
        REQUIRE(timedTask.get() == 42);
    }

    SECTION("WaitAsync throws timeout exception if target exceeds duration") {
        Task<int> slowTask = []() -> Task<int> {
            co_await DelayAsync(300ms);
            co_return 100;
        }();

        Task<int> timedTask = WaitAsync(std::move(slowTask), 50ms);
        REQUIRE_THROWS_AS(timedTask.get(), std::runtime_error);
    }
}

TEST_CASE("Spawn Fire-And-Forget Protection", "[GPP::Spawn]") {
    SECTION("Spawn protects temporary tasks from RAII double-destruction segfaults") {
        bool completed = false;

        // Directly spawning a lambda. This would segfault if discarded raw,
        // but GPP::Spawn safely captures and manages its lifetime on the heap.
        Spawn([&completed]() -> Task<void> {
            co_await DelayAsync(50ms);
            completed = true;
        }());

        std::this_thread::sleep_for(100ms); // Wait for background detached task to execute
        REQUIRE(completed == true);
    }
}

TEST_CASE("Task Combinators: WhenAll and WhenAny", "[GPP::Combinators]") {
    SECTION("WhenAll resolves when all concurrent tasks complete") {
        auto makeTask = [](int value, std::chrono::milliseconds delay) -> Task<int> {
            co_await DelayAsync(delay);
            co_return value;
        };

        std::vector<Task<int>> tasks;
        tasks.push_back(makeTask(10, 20ms));
        tasks.push_back(makeTask(20, 40ms));
        tasks.push_back(makeTask(30, 10ms));

        Task<std::vector<int>> allTask = WhenAll(std::move(tasks));
        std::vector<int> results = allTask.get();

        REQUIRE(results.size() == 3);
        REQUIRE(results[0] == 10);
        REQUIRE(results[1] == 20);
        REQUIRE(results[2] == 30);
    }

    SECTION("WhenAny resolves immediately with the fastest task") {
        auto makeTask = [](int value, std::chrono::milliseconds delay) -> Task<int> {
            co_await DelayAsync(delay);
            co_return value;
        };

        std::vector<Task<int>> tasks;
        tasks.push_back(makeTask(100, 200ms)); // Slow
        tasks.push_back(makeTask(200, 10ms));  // Fast Winner
        tasks.push_back(makeTask(300, 150ms)); // Slow

        Task<WhenAnyResult<int>> anyTask = WhenAny(std::move(tasks));
        WhenAnyResult<int> winner = anyTask.get();

        REQUIRE(winner.index == 1);
        REQUIRE(winner.value == 200);
    }
}
