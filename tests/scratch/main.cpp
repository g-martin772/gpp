import GPP;
import std;

using namespace GPP;

struct IAssetManager : public IService {
    virtual void Load() = 0;
};

class VulkanAssetManager : public IAssetManager {
public:
    using Dependencies = std::tuple<Logger>;

    VulkanAssetManager(Logger* logger)
        : m_Logger(logger) {}

    void Load() override {
        m_Logger->Info("Vulkan Asset Manager loading texture ...");
    }

private:
    Logger* m_Logger;
};

int main()
{
    Logger::LogInfo("Starting Scratch");
    Logger::LogInfo("Main Thread ID: {}", std::this_thread::get_id());

    ServiceCollection services;

    services.AddSingleton<Logger>([](ServiceProvider& _)
    {
        auto logger = std::make_unique<Logger>();
        logger->CreateConsoleLogger("DI Logger");
        logger->SetLevel(LogLevel::Info);
        logger->Info("Test");
        return logger;
    });

    services.AddSingleton<IAssetManager, VulkanAssetManager>();

    auto serviceProvider = services.Build();

    auto assetManager = serviceProvider.GetRequiredService<IAssetManager>();
    assetManager->Load();

    return 0;
}
