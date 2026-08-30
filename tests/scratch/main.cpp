import GPP;

using namespace GPP;

int main() {
    Logger::LogInfo("Test {}", 12);

    auto logger = new Logger();

    logger->CreateMultiLogger("MyLogger", "log.txt", true);
    logger->SetPattern("[%^%l%$] %v");
    logger->SetLevel(LogLevel::Debug);

    logger->Info("This is an info message with number: {}", 42);


    for (int i = 0; i < 10; ++i)
    {
        ThreadPool::Instance()->Submit([i]() {
            Logger::LogInfo("Task {} is running in the thread pool.", i);
        });
    }

    Logger::LogInfo("ThreadPool Worker Count: {}", ThreadPool::Instance()->WorkerCount());

    return 0;
}
