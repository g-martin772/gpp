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
    }

    Application GuiApplicationBuilder::Build()
    {
        return ApplicationBuilder::Build();
    }

    GuiApplicationBuilder GuiApplication::CreateBuilder()
    {
        return GuiApplicationBuilder();
    }
}
