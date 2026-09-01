module GPP.Graphics;

import :Application;

namespace GPP
{
    GuiApplicationBuilder::GuiApplicationBuilder()
    {
        Services.AddHostedService<WindowManager>();
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
