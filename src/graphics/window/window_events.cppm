export module GPP.Graphics:Windowing.Events;

import std;
import GPP.Core;

namespace GPP
{
    export using WindowId = std::uint32_t;

    export enum class KeyCode : std::uint32_t
    {
        Unknown = 0x00000000u,
        Return = 0x0000000du, Escape = 0x0000001bu, Backspace = 0x00000008u,
        Tab = 0x00000009u, Space = 0x00000020u,
        A = 0x00000061u, B = 0x00000062u, C = 0x00000063u, D = 0x00000064u,
        E = 0x00000065u, F = 0x00000066u, G = 0x00000067u, H = 0x00000068u,
        I = 0x00000069u, J = 0x0000006au, K = 0x0000006bu, L = 0x0000006cu,
        M = 0x0000006du, N = 0x0000006eu, O = 0x0000006fu, P = 0x00000070u,
        Q = 0x00000071u, R = 0x00000072u, S = 0x00000073u, T = 0x00000074u,
        U = 0x00000075u, V = 0x00000076u, W = 0x00000077u, X = 0x00000078u,
        Y = 0x00000079u, Z = 0x0000007au,
        Num0 = 0x00000030u, Num1 = 0x00000031u, Num2 = 0x00000032u,
        Num3 = 0x00000033u, Num4 = 0x00000034u, Num5 = 0x00000035u,
        Num6 = 0x00000036u, Num7 = 0x00000037u, Num8 = 0x00000038u,
        Num9 = 0x00000039u,
        F1 = 0x4000003au, F2 = 0x4000003bu, F3 = 0x4000003cu, F4 = 0x4000003du,
        F5 = 0x4000003eu, F6 = 0x4000003fu, F7 = 0x40000040u, F8 = 0x40000041u,
        F9 = 0x40000042u, F10 = 0x40000043u, F11 = 0x40000044u, F12 = 0x40000045u,
        Insert = 0x40000049u, Delete = 0x0000007fu, Home = 0x4000004au,
        End = 0x4000004du, PageUp = 0x4000004bu, PageDown = 0x4000004eu,
        Left = 0x40000050u, Right = 0x4000004fu, Up = 0x40000052u, Down = 0x40000051u,
        CapsLock = 0x40000039u, NumLock = 0x40000053u, ScrollLock = 0x40000047u,
        PrintScreen = 0x40000046u, Pause = 0x40000048u,
        LeftBracket = 0x0000005bu, RightBracket = 0x0000005du,
        Backslash = 0x0000005cu, Semicolon = 0x0000003bu, Apostrophe = 0x00000027u,
        Comma = 0x0000002cu, Period = 0x0000002eu, Slash = 0x0000002fu,
        Minus = 0x0000002du, Equals = 0x0000003du, Grave = 0x00000060u
    };

    export enum class ScanCode : std::uint32_t
    {
        Unknown = 0, A = 4, B = 5, C = 6, D = 7, E = 8, F = 9, G = 10, H = 11,
        I = 12, J = 13, K = 14, L = 15, M = 16, N = 17, O = 18, P = 19,
        Q = 20, R = 21, S = 22, T = 23, U = 24, V = 25, W = 26, X = 27,
        Y = 28, Z = 29,
        Num1 = 30, Num2 = 31, Num3 = 32, Num4 = 33, Num5 = 34, Num6 = 35,
        Num7 = 36, Num8 = 37, Num9 = 38, Num0 = 39,
        Return = 40, Escape = 41, Backspace = 42, Tab = 43, Space = 44,
        Minus = 45, Equals = 46, LeftBracket = 47, RightBracket = 48,
        Backslash = 49, Semicolon = 51, Apostrophe = 52, Grave = 53,
        Comma = 54, Period = 55, Slash = 56, CapsLock = 57,
        F1 = 58, F2 = 59, F3 = 60, F4 = 61, F5 = 62, F6 = 63,
        F7 = 64, F8 = 65, F9 = 66, F10 = 67, F11 = 68, F12 = 69,
        PrintScreen = 70, ScrollLock = 71, Pause = 72, Insert = 73, Home = 74,
        PageUp = 75, Delete = 76, End = 77, PageDown = 78,
        Right = 79, Left = 80, Down = 81, Up = 82,
        NumLock = 83, KeypadDivide = 84, KeypadMultiply = 85, KeypadMinus = 86,
        KeypadPlus = 87, KeypadEnter = 88, Keypad1 = 89, Keypad2 = 90,
        Keypad3 = 91, Keypad4 = 92, Keypad5 = 93, Keypad6 = 94,
        Keypad7 = 95, Keypad8 = 96, Keypad9 = 97, Keypad0 = 98,
        KeypadPeriod = 99, Application = 101, Power = 102,
        F13 = 104, F14 = 105, F15 = 106, F16 = 107, F17 = 108, F18 = 109,
        F19 = 110, F20 = 111, F21 = 112, F22 = 113, F23 = 114, F24 = 115,
        LeftControl = 224, LeftShift = 225, LeftAlt = 226, LeftGui = 227,
        RightControl = 228, RightShift = 229, RightAlt = 230, RightGui = 231
    };

    export enum class MouseButton : std::uint8_t
    {
        Left = 1, Middle = 2, Right = 3, X1 = 4, X2 = 5
    };

    export struct QuitEvent
    {
    };

    export struct WindowCloseRequestedEvent
    {
        WindowId Window{};
    };

    export struct WindowResizedEvent
    {
        WindowId Window{};
        int Width{};
        int Height{};
    };

    export struct KeyEvent
    {
        WindowId Window{};
        KeyCode Key{};
        ScanCode Scan{};
        bool Down{};
        bool Repeat{};
    };

    export struct MouseButtonEvent
    {
        WindowId Window{};
        MouseButton Button{};
        bool Down{};
    };

    export struct MouseMotionEvent
    {
        WindowId Window{};
        float X{};
        float Y{};
        float DeltaX{};
        float DeltaY{};
    };

    export struct MouseWheelEvent
    {
        WindowId Window{};
        float X{};
        float Y{};
    };

    export struct TextInputEvent
    {
        WindowId Window{};
        std::string Text;
    };

    export class InputState : public IService
    {
    public:
        using Dependencies = std::tuple<EventDispatcher>;

        explicit InputState(const std::shared_ptr<EventDispatcher>& d)
        {
            auto& dispatcher = *d;
            m_Subscriptions.push_back(dispatcher.Subscribe<KeyEvent>(
                [this](const KeyEvent& event)
                {
                    std::scoped_lock lock(m_Mutex);
                    const bool wasDown = m_Keys.contains(event.Key) && m_Keys.at(event.Key);
                    m_Keys[event.Key] = event.Down;
                    if (event.Down && !wasDown) m_Pressed[event.Key] = true;
                    if (!event.Down && wasDown) m_Released[event.Key] = true;
                    const bool wasScanDown = m_Scans.contains(event.Scan) && m_Scans.at(event.Scan);
                    m_Scans[event.Scan] = event.Down;
                    if (event.Down && !wasScanDown) m_PressedScans[event.Scan] = true;
                    if (!event.Down && wasScanDown) m_ReleasedScans[event.Scan] = true;
                }));
            m_Subscriptions.push_back(dispatcher.Subscribe<MouseButtonEvent>(
                [this](const MouseButtonEvent& event)
                {
                    std::scoped_lock lock(m_Mutex);
                    const bool wasDown = m_Buttons.contains(event.Button) && m_Buttons.at(event.Button);
                    m_Buttons[event.Button] = event.Down;
                    if (event.Down && !wasDown) m_PressedButtons[event.Button] = true;
                    if (!event.Down && wasDown) m_ReleasedButtons[event.Button] = true;
                }));
            m_Subscriptions.push_back(dispatcher.Subscribe<MouseMotionEvent>(
                [this](const MouseMotionEvent& event)
                {
                    std::scoped_lock lock(m_Mutex);
                    m_MouseX = event.X;
                    m_MouseY = event.Y;
                }));
        }

        bool IsKeyDown(KeyCode key) const
        {
            std::scoped_lock lock(m_Mutex);
            return m_Keys.contains(key) && m_Keys.at(key);
        }

        bool WasKeyPressed(KeyCode key) const
        {
            std::scoped_lock lock(m_Mutex);
            return m_Pressed.contains(key) && m_Pressed.at(key);
        }

        bool WasKeyReleased(KeyCode key) const
        {
            std::scoped_lock lock(m_Mutex);
            return m_Released.contains(key) && m_Released.at(key);
        }

        bool IsKeyDown(ScanCode scan) const
        {
            std::scoped_lock lock(m_Mutex);
            return m_Scans.contains(scan) && m_Scans.at(scan);
        }

        bool WasKeyPressed(ScanCode scan) const
        {
            std::scoped_lock lock(m_Mutex);
            return m_PressedScans.contains(scan) && m_PressedScans.at(scan);
        }

        bool WasKeyReleased(ScanCode scan) const
        {
            std::scoped_lock lock(m_Mutex);
            return m_ReleasedScans.contains(scan) && m_ReleasedScans.at(scan);
        }

        bool IsMouseButtonDown(MouseButton button) const
        {
            std::scoped_lock lock(m_Mutex);
            return m_Buttons.contains(button) && m_Buttons.at(button);
        }

        bool WasMouseButtonPressed(MouseButton button) const
        {
            std::scoped_lock lock(m_Mutex);
            return m_PressedButtons.contains(button) && m_PressedButtons.at(button);
        }

        bool WasMouseButtonReleased(MouseButton button) const
        {
            std::scoped_lock lock(m_Mutex);
            return m_ReleasedButtons.contains(button) && m_ReleasedButtons.at(button);
        }

        float MouseX() const
        {
            std::scoped_lock lock(m_Mutex);
            return m_MouseX;
        }

        float MouseY() const
        {
            std::scoped_lock lock(m_Mutex);
            return m_MouseY;
        }

        void BeginFrame()
        {
            std::scoped_lock lock(m_Mutex);
            m_Pressed.clear();
            m_Released.clear();
            m_PressedScans.clear();
            m_ReleasedScans.clear();
            m_PressedButtons.clear();
            m_ReleasedButtons.clear();
        }

    private:
        mutable std::mutex m_Mutex{};
        std::unordered_map<KeyCode, bool> m_Keys{}, m_Pressed{}, m_Released{};
        std::unordered_map<ScanCode, bool> m_Scans{}, m_PressedScans{}, m_ReleasedScans{};
        std::unordered_map<MouseButton, bool> m_Buttons{}, m_PressedButtons{}, m_ReleasedButtons{};
        float m_MouseX{}, m_MouseY{};
        std::vector<EventSubscription> m_Subscriptions{};
    };
}
