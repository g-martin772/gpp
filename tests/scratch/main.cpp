import GPP;
import std;

using namespace GPP;

Task<int> ExampleCoroutine(std::stop_token stopToken = {})
{
    co_await ResumeOn(ThreadPool::Instance());
    Logger::LogInfo("Inside Coroutine Part 1. Thread ID: {}", std::this_thread::get_id());
    co_await DelayAsync(std::chrono::milliseconds(200), stopToken);
    Logger::LogInfo("Inside Coroutine Part 2. Thread ID: {}", std::this_thread::get_id());
    co_return 42;
}


int main()
{
    Logger::LogInfo("Starting Scratch");
    Logger::LogInfo("Main Thread ID: {}", std::this_thread::get_id());


    Spawn(ExampleCoroutine());

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    return 0;
}
