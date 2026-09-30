module GPP.Graphics;

import :Application;
import :Windowing.WindowManager;
import :Renderer;

namespace GPP
{
    void GuiApplication::OnStart()
    {
        auto wm = m_ServiceProvider.GetRequiredService<WindowManager>();
        auto renderer = m_ServiceProvider.GetRequiredService<Renderer>();

        Spawn([](GuiApplication* app, std::shared_ptr<WindowManager> wm, std::shared_ptr<Renderer> renderer) -> Task<void>
        {
            co_await ResumeOn(ThreadPool::Instance());
            co_await wm->AwaitReady();
            co_await renderer->AwaitReady();

            // check individual targets ready?
            for (auto& [target, layerStack] : app->m_LayerStacks)
            {
                if (target.Type == LayerTarget::Type::eNone)
                {
                    continue;
                }

                if (target.Type == LayerTarget::Type::eLayerTargetWindow)
                {
                    // TODO: Wait on individual window to be created
                    const auto& window = wm->GetWindow(target.Id);
                    if (!window)
                    {
                        throw std::runtime_error("Window for LayerTarget not found");
                    }

                    renderer->AttachLayerStackToWindow(layerStack, window);
                }

                if (target.Type == LayerTarget::Type::eLayerTargetBuffer)
                {
                    renderer->AttachLayerStackToBuffer(layerStack, target.Id);
                }
            }

            co_return;
        }(this, std::move(wm), std::move(renderer)));
    }

    void GuiApplication::OnUpdate(float deltaTime)
    {

    }

    void GuiApplication::OnStop()
    {
    }

    GuiApplicationBuilder::GuiApplicationBuilder()
    {
        Services.AddSingleton<VulkanContext>();
        Services.AddSingleton<InputState>();

        Services.AddHostedService<WindowManager>();
        Services.AddHostedService<ShaderAssetCatalog>();
        Services.AddHostedService<Renderer>();

        Services.Configure<WindowOptions>("GPP:Graphics:Window");
        Services.Configure<RenderOptions>("GPP:Graphics:Render");
    }

    std::shared_ptr<GuiApplication> GuiApplicationBuilder::Build()
    {
        auto config = Configuration.Build();
        Services.ApplyConfiguration(*config);
        auto sp = Services.Build();
        LayerStackTable layerStacks;

        for (auto& layerBuilder : m_LayerDescriptions)
        {
            // TODO: Verify Layer Target exists
            auto layer = layerBuilder.Build(sp);
            if (auto it = layerStacks.find(layerBuilder.Target); it != layerStacks.end())
            {
                it->second.PushLayer(layer);
            }
            else
            {
                auto layerStack = GuiLayerStack();
                layerStack.PushLayer(layer);
                layerStacks[layerBuilder.Target] = layerStack;
            }
        }

        auto app = std::make_shared<GuiApplication>(std::move(sp), std::move(config), std::move(layerStacks));

        return app;
    }
}
