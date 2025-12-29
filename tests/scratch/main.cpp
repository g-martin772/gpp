import Core;

int main() {
    $Logger::LogInfo("Test {}", 12);

    auto logger = new $Logger();

    logger->CreateMultiLogger("MyLogger", "log.txt", true);
    logger->SetPattern("[%^%l%$] %v");
    logger->SetLevel($LogLevel::Debug);

    logger->Info("This is an info message with number: {}", 42);

    return 0;
}