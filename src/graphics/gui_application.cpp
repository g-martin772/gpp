module GPP.Graphics;

import :Application;
import :Windowing.WindowManager;
import :Renderer;

namespace GPP
{
    GuiApplicationBuilder::GuiApplicationBuilder()
    {
        Services.AddSingleton<VulkanContext>();
        Services.AddSingleton<InputState>();

        Services.AddHostedService<WindowManager>();
        Services.AddHostedService<Renderer>();

        Services.Configure<WindowOptions>("GPP:Graphics:Window");
    }

    std::shared_ptr<GuiApplication> GuiApplicationBuilder::Build()
    {
        auto config = Configuration.Build();
        Services.ApplyConfiguration(*config);
        auto app = std::make_shared<GuiApplication>(Services.Build(), std::move(config));
        return app;
    }
}
