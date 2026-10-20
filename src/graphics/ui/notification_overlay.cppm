export module GPP.Graphics:UI.NotificationOverlay;

import std;
import GPP.Core;

namespace GPP
{
    export class NotificationOverlay
    {
    public:
        void Draw(NotificationCenter& center);

        float Width = 360.0f;
        std::size_t MaxToasts = 5;
    };
}
