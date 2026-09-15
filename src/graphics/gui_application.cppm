export module GPP.Graphics:Application;

import std;
import GPP.Core;

namespace GPP
{
    export struct GuiApplication : public Application
    {
        GuiApplication(ServiceProvider&& provider, std::unique_ptr<IConfiguration> configuration)
            : Application(std::move(provider), std::move(configuration))
        {
        }
    };

    export class GuiApplicationBuilder : public builder<GuiApplication>
    {
    public:
        GuiApplicationBuilder();
        std::shared_ptr<GuiApplication> Build() override;
    };
}
