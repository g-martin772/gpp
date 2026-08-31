import GPP;
import std;

using namespace GPP;

struct IAssetManager : public IService {
    virtual void Load() = 0;
};

class VulkanAssetManager : public IAssetManager {
public:
    using Dependencies = std::tuple<Logger>;

    VulkanAssetManager(std::shared_ptr<Logger> logger)
        : m_Logger(logger) {}

    void Load() override {
        m_Logger->Info("Vulkan Asset Manager loading texture ...");
    }

private:
    std::shared_ptr<Logger> m_Logger;
};

int main()
{
    Logger::LogInfo("Starting Scratch");
    Logger::LogInfo("Main Thread ID: {}", std::this_thread::get_id());

    auto builder = App::CreateCliBuilder();

    builder.Services.AddSingleton<IAssetManager, VulkanAssetManager>();

    auto app = builder.Build();

    auto assetManager = app.GetServiceProvider().GetRequiredService<IAssetManager>();
    assetManager->Load();

    return app.Run();
}
