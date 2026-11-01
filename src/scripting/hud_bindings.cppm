export module GPP.Scripting:HudBindings;

import std;
import :Luau;

export namespace GPP
{
    enum class HudKind : std::uint8_t { Text, Line, Rect, Circle, Arc };
    enum class HudSpace : std::uint8_t { Screen, Left, Center, Right };
    enum class HudAlign : std::uint8_t { Left, Center, Right };

    struct HudCommand
    {
        HudKind Kind{HudKind::Line};
        HudSpace Space{HudSpace::Screen};
        HudAlign Align{HudAlign::Left};
        bool Filled{false};
        float X{0.0f};
        float Y{0.0f};
        float A{0.0f};
        float B{0.0f};
        float C{0.0f};
        float Thickness{1.0f};
        float Size{14.0f};
        std::array<float, 4> Color{1.0f, 1.0f, 1.0f, 1.0f};
        std::string Text;
    };

    struct HudFrame
    {
        std::vector<HudCommand> Commands;
    };

    constexpr std::size_t kHudCommandLimit = 4096;
    constexpr std::size_t kHudTextLimit = 256;

    class HudCanvas
    {
    public:
        void Add(HudCommand command);
        void Reset();
        void Clear();
        void Publish();
        [[nodiscard]] std::shared_ptr<const HudFrame> Snapshot() const;

    private:
        std::vector<HudCommand> m_Working;
        mutable std::mutex m_Mutex;
        std::shared_ptr<const HudFrame> m_Published = std::make_shared<const HudFrame>();
    };

    void RegisterHudBindings(LuauVm& vm, const std::shared_ptr<HudCanvas>& canvas);
}
