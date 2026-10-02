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
                    const auto window = !target.Name.empty()
                                            ? wm->GetWindow(target.Name)
                                            : wm->GetWindow(target.Id);
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
        Services.AddSingleton<WindowDefinitions>(
            [this](ServiceProvider&) -> std::shared_ptr<IService>
            {
                return std::make_shared<WindowDefinitions>(m_WindowDefinitions);
            });

        Services.AddHostedService<WindowManager>();
        Services.AddHostedService<ShaderAssetCatalog>();
        Services.AddHostedService<Renderer>();

        Services.AddSingleton<HotReloadLayerRegistrations>(
            [this](ServiceProvider& provider) -> std::shared_ptr<IService>
            {
                auto registrations = std::make_shared<HotReloadLayerRegistrations>();
                registrations->Items.reserve(m_HotReloadLayerDescriptions.size());
                for (auto& layerBuilder : m_HotReloadLayerDescriptions)
                {
                    registrations->Items.push_back(HotReloadLayerRegistration{
                        layerBuilder.Description, ActivateService<HotReloadLayerProxy>(provider)});
                }
                return registrations;
            });
        Services.AddHostedService<HotReloadLayerManager>();

        Services.Configure<WindowOptions>("GPP:Graphics:Window");
        Services.Configure<RenderOptions>("GPP:Graphics:Render");
        Services.Configure<LayerHotReloadOptions>("GPP:Graphics:LayerHotReload");
    }

    std::shared_ptr<GuiApplication> GuiApplicationBuilder::Build()
    {
        auto config = Configuration.Build();
        Services.ApplyConfiguration(*config);
        if (const auto windows = config->GetSection("GPP:Graphics:Windows"))
        {
            for (std::size_t index = 0;; ++index)
            {
                const auto section = windows->GetSection(std::to_string(index));
                std::string name;
                if (!section->TryGetValue("Name", name) || name.empty())
                {
                    break;
                }

                auto options = WindowOptions::FromConfig(*section);
                m_WindowDefinitions.Items.push_back(WindowDefinition{
                    .Name = std::move(name),
                    .Options = std::move(options)
                });
            }
        }
        if (m_Headless)
        {
            auto options = WindowOptions::FromConfig(*config->GetSection("GPP:Graphics:Window"));
            options.Headless = true;
            Services.AddSingleton<WindowOptions>(
                [options](ServiceProvider&) -> std::shared_ptr<IService>
                {
                    return std::make_shared<WindowOptions>(options);
                });
        }
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

        if (!m_HotReloadLayerDescriptions.empty())
        {
            const auto registrations = sp.GetRequiredService<HotReloadLayerRegistrations>();
            for (auto& registration : registrations->Items)
            {
                const auto& target = registration.Description.Target;
                if (auto it = layerStacks.find(target); it != layerStacks.end())
                {
                    it->second.PushLayer(registration.Proxy);
                }
                else
                {
                    auto layerStack = GuiLayerStack();
                    layerStack.PushLayer(registration.Proxy);
                    layerStacks[target] = layerStack;
                }
            }
        }

        auto app = std::make_shared<GuiApplication>(std::move(sp), std::move(config), std::move(layerStacks));

        return app;
    }
}
