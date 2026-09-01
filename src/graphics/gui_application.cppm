export module GPP.Graphics:Application;

import GPP.Core;

namespace GPP
{

    export class GuiApplicationBuilder : public ApplicationBuilder
    {
    public:
        GuiApplicationBuilder();
        Application Build() override;
    };

    export struct GuiApplication
    {
        static GuiApplicationBuilder CreateBuilder();
    };

}