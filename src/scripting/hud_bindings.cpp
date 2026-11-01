module GPP.Scripting;

import std;
import :Luau;
import :HudBindings;

namespace GPP
{
    namespace
    {
        constexpr std::string_view kPrelude = R"lua(
local natives = { text = ui.__text, line = ui.__line, rect = ui.__rect, circle = ui.__circle, arc = ui.__arc, clear = ui.__clear }
ui.__text, ui.__line, ui.__rect, ui.__circle, ui.__arc, ui.__clear = nil, nil, nil, nil, nil, nil

local SPACES = { screen = 0, left = 1, center = 2, right = 3 }
local ALIGNS = { left = 0, center = 1, right = 2 }
local NAMED = {
    white = { 1, 1, 1, 1 }, black = { 0, 0, 0, 1 }, red = { 1, 0.25, 0.2, 1 }, green = { 0.3, 1, 0.4, 1 },
    blue = { 0.3, 0.55, 1, 1 }, yellow = { 1, 0.85, 0.25, 1 }, orange = { 1, 0.6, 0.15, 1 }, cyan = { 0.3, 0.9, 1, 1 },
    magenta = { 1, 0.35, 0.9, 1 }, gray = { 0.6, 0.6, 0.6, 1 },
}

local function hex(text, fn)
    local digits = string.match(text, "^#?(%x+)$")
    if digits == nil or (#digits ~= 6 and #digits ~= 8) then
        error(`ui.hud.{fn}: color string must be #rrggbb or #rrggbbaa`, 4)
    end
    local function byte(i) return tonumber(string.sub(digits, i, i + 1), 16) / 255 end
    return byte(1), byte(3), byte(5), #digits == 8 and byte(7) or 1
end

local function color(c, fn)
    local kind = typeof(c)
    if c == nil then return 1, 1, 1, 1 end
    if kind == "string" then
        local named = NAMED[c]
        if named then return named[1], named[2], named[3], named[4] end
        return hex(c, fn)
    elseif kind == "number" then
        if c > 0xFFFFFF then
            return math.floor(c / 0x1000000) % 256 / 255, math.floor(c / 0x10000) % 256 / 255, math.floor(c / 0x100) % 256 / 255, c % 256 / 255
        end
        return math.floor(c / 0x10000) % 256 / 255, math.floor(c / 0x100) % 256 / 255, c % 256 / 255, 1
    elseif kind == "vector" then
        return c.x, c.y, c.z, 1
    elseif kind == "table" then
        local r, g, b, a = c.r or c[1], c.g or c[2], c.b or c[3], c.a or c[4]
        if type(r) == "number" and type(g) == "number" and type(b) == "number" then return r, g, b, a or 1 end
    end
    error(`ui.hud.{fn}: color must be a table with r, g, b and a, a hex string, a number or a name`, 3)
end

local function number(fn, name, value)
    if type(value) ~= "number" or value ~= value or value == math.huge or value == -math.huge then
        error(`ui.hud.{fn}: {name} must be a finite number`, 3)
    end
    return value
end

local function space_of(opts, fn, default)
    local name = opts and opts.space or default
    local index = SPACES[name]
    if index == nil then error(`ui.hud.{fn}: space must be screen, left, center or right`, 3) end
    return index
end

local function make(default)
    local hud = {}

    function hud.text(x, y, text, opts)
        local align = ALIGNS[opts and opts.align or "left"]
        if align == nil then error("ui.hud.text: align must be left, center or right", 2) end
        local r, g, b, a = color(opts and opts.color, "text")
        natives.text(number("text", "x", x), number("text", "y", y), tostring(text), number("text", "size", opts and opts.size or 14), align, space_of(opts, "text", default), r, g, b, a)
    end

    function hud.line(x1, y1, x2, y2, opts)
        local r, g, b, a = color(opts and opts.color, "line")
        natives.line(number("line", "x1", x1), number("line", "y1", y1), number("line", "x2", x2), number("line", "y2", y2),
            number("line", "thickness", opts and opts.thickness or 1), space_of(opts, "line", default), r, g, b, a)
    end

    function hud.rect(x, y, w, h, opts)
        local r, g, b, a = color(opts and opts.color, "rect")
        natives.rect(number("rect", "x", x), number("rect", "y", y), number("rect", "w", w), number("rect", "h", h),
            opts and opts.filled and 1 or 0, number("rect", "thickness", opts and opts.thickness or 1), space_of(opts, "rect", default), r, g, b, a)
    end

    function hud.circle(x, y, radius, opts)
        local r, g, b, a = color(opts and opts.color, "circle")
        natives.circle(number("circle", "x", x), number("circle", "y", y), number("circle", "radius", radius),
            opts and opts.filled and 1 or 0, number("circle", "thickness", opts and opts.thickness or 1), space_of(opts, "circle", default), r, g, b, a)
    end

    function hud.arc(x, y, radius, from, to, opts)
        local r, g, b, a = color(opts and opts.color, "arc")
        natives.arc(number("arc", "x", x), number("arc", "y", y), number("arc", "radius", radius), number("arc", "from", from), number("arc", "to", to),
            number("arc", "thickness", opts and opts.thickness or 1), space_of(opts, "arc", default), r, g, b, a)
    end

    function hud.clear() natives.clear() end

    return hud
end

local hud = make("screen")
function hud.at(space)
    if SPACES[space] == nil then error("ui.hud.at: space must be screen, left, center or right", 2) end
    return make(space)
end
hud.color = function(r, g, b, a) return { r = r, g = g, b = b, a = a or 1 } end
ui.hud = hud
)lua";

        float Finite(LuauNativeCall& call, const int index, const char* what)
        {
            const double value = call.Number(index);
            if (!std::isfinite(value)) throw std::runtime_error(std::string("ui.hud.") + what + ": non-finite number");
            return static_cast<float>(value);
        }

        std::array<float, 4> ReadColor(LuauNativeCall& call, const int first)
        {
            std::array<float, 4> color;
            for (int i = 0; i < 4; ++i) color[i] = std::clamp(static_cast<float>(call.Number(first + i, 1.0)), 0.0f, 1.0f);
            return color;
        }

        HudSpace ReadSpace(LuauNativeCall& call, const int index)
        {
            return static_cast<HudSpace>(std::clamp(static_cast<int>(call.Number(index)), 0, 3));
        }
    }

    void HudCanvas::Add(HudCommand command)
    {
        if (m_Working.size() >= kHudCommandLimit)
        {
            throw std::runtime_error(std::format("ui.hud: more than {} commands in one frame", kHudCommandLimit));
        }
        m_Working.push_back(std::move(command));
    }

    void HudCanvas::Reset() { m_Working.clear(); }

    void HudCanvas::Clear()
    {
        m_Working.clear();
        std::scoped_lock lock(m_Mutex);
        m_Published = std::make_shared<const HudFrame>();
    }

    void HudCanvas::Publish()
    {
        {
            std::scoped_lock lock(m_Mutex);
            if (m_Working.empty() && m_Published->Commands.empty()) return;
        }
        auto frame = std::make_shared<const HudFrame>(HudFrame{m_Working});
        std::scoped_lock lock(m_Mutex);
        m_Published = std::move(frame);
    }

    std::shared_ptr<const HudFrame> HudCanvas::Snapshot() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_Published;
    }

    void RegisterHudBindings(LuauVm& vm, const std::shared_ptr<HudCanvas>& canvas)
    {
        vm.Register("ui", "__text", [canvas](LuauNativeCall& call)
        {
            HudCommand command;
            command.Kind = HudKind::Text;
            command.X = Finite(call, 1, "text");
            command.Y = Finite(call, 2, "text");
            command.Text = call.String(3).substr(0, kHudTextLimit);
            command.Size = std::clamp(Finite(call, 4, "text"), 4.0f, 256.0f);
            command.Align = static_cast<HudAlign>(std::clamp(static_cast<int>(call.Number(5)), 0, 2));
            command.Space = ReadSpace(call, 6);
            command.Color = ReadColor(call, 7);
            canvas->Add(std::move(command));
            return 0;
        });
        vm.Register("ui", "__line", [canvas](LuauNativeCall& call)
        {
            HudCommand command;
            command.Kind = HudKind::Line;
            command.X = Finite(call, 1, "line");
            command.Y = Finite(call, 2, "line");
            command.A = Finite(call, 3, "line");
            command.B = Finite(call, 4, "line");
            command.Thickness = std::clamp(Finite(call, 5, "line"), 0.0f, 64.0f);
            command.Space = ReadSpace(call, 6);
            command.Color = ReadColor(call, 7);
            canvas->Add(std::move(command));
            return 0;
        });
        vm.Register("ui", "__rect", [canvas](LuauNativeCall& call)
        {
            HudCommand command;
            command.Kind = HudKind::Rect;
            command.X = Finite(call, 1, "rect");
            command.Y = Finite(call, 2, "rect");
            command.A = Finite(call, 3, "rect");
            command.B = Finite(call, 4, "rect");
            command.Filled = call.Number(5) != 0.0;
            command.Thickness = std::clamp(Finite(call, 6, "rect"), 0.0f, 64.0f);
            command.Space = ReadSpace(call, 7);
            command.Color = ReadColor(call, 8);
            canvas->Add(std::move(command));
            return 0;
        });
        vm.Register("ui", "__circle", [canvas](LuauNativeCall& call)
        {
            HudCommand command;
            command.Kind = HudKind::Circle;
            command.X = Finite(call, 1, "circle");
            command.Y = Finite(call, 2, "circle");
            command.A = Finite(call, 3, "circle");
            command.Filled = call.Number(4) != 0.0;
            command.Thickness = std::clamp(Finite(call, 5, "circle"), 0.0f, 64.0f);
            command.Space = ReadSpace(call, 6);
            command.Color = ReadColor(call, 7);
            canvas->Add(std::move(command));
            return 0;
        });
        vm.Register("ui", "__arc", [canvas](LuauNativeCall& call)
        {
            HudCommand command;
            command.Kind = HudKind::Arc;
            command.X = Finite(call, 1, "arc");
            command.Y = Finite(call, 2, "arc");
            command.A = Finite(call, 3, "arc");
            command.B = Finite(call, 4, "arc");
            command.C = Finite(call, 5, "arc");
            command.Thickness = std::clamp(Finite(call, 6, "arc"), 0.0f, 64.0f);
            command.Space = ReadSpace(call, 7);
            command.Color = ReadColor(call, 8);
            canvas->Add(std::move(command));
            return 0;
        });
        vm.Register("ui", "__clear", [canvas](LuauNativeCall&)
        {
            canvas->Reset();
            return 0;
        });
        if (auto result = vm.RunTrusted(kPrelude, "=gpp_hud"); !result)
        {
            throw std::runtime_error("gpp hud prelude failed: " + result.error().Message);
        }
    }
}
