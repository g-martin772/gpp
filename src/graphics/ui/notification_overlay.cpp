module GPP.Graphics;

import :UI.NotificationOverlay;
import std;

namespace GPP
{
    namespace
    {
        constexpr float kMargin = 16.0f;
        constexpr float kGap = 8.0f;
        constexpr float kFadeSeconds = 0.25f;
        constexpr float kAccentWidth = 4.0f;

        ImVec4 KindColor(const NotificationKind kind)
        {
            switch (kind)
            {
            case NotificationKind::Success: return ImVec4(0.35f, 0.80f, 0.45f, 1.0f);
            case NotificationKind::Warning: return ImVec4(0.95f, 0.75f, 0.25f, 1.0f);
            case NotificationKind::Error: return ImVec4(0.90f, 0.35f, 0.35f, 1.0f);
            default: return ImVec4(0.40f, 0.65f, 0.95f, 1.0f);
            }
        }

        ImU32 WithAlpha(ImVec4 color, const float alpha)
        {
            color.w *= alpha;
            return ImGui::GetColorU32(color);
        }

        float TextHeight(const std::string& text, const float wrap)
        {
            return text.empty() ? 0.0f : ImGui::CalcTextSize(text.c_str(), nullptr, false, wrap).y;
        }

        void DrawWrapped(ImDrawList& list, const ImVec2 position, const ImU32 color, const std::string& text, const float wrap)
        {
            if (text.empty()) return;
            list.AddText(ImGui::GetFont(), ImGui::GetFontSize(), position, color, text.c_str(), nullptr, wrap);
        }
    }

    void NotificationOverlay::Draw(NotificationCenter& center)
    {
        const auto tasks = center.Tasks();
        const auto toasts = center.Toasts();
        if (tasks.empty() && toasts.empty()) return;

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        auto& list = *ImGui::GetForegroundDrawList(const_cast<ImGuiViewport*>(viewport));
        const auto& io = ImGui::GetIO();
        const auto& style = ImGui::GetStyle();
        const float pad = style.WindowPadding.x;
        const float inner = Width - pad * 2.0f - kAccentWidth;
        const float line = ImGui::GetTextLineHeight();
        const ImVec4 background = style.Colors[ImGuiCol_PopupBg];
        const ImVec4 textColor = style.Colors[ImGuiCol_Text];
        const ImVec4 dim = style.Colors[ImGuiCol_TextDisabled];
        const float rounding = style.WindowRounding > 0.0f ? style.WindowRounding : 4.0f;

        float bottom = viewport->WorkPos.y + viewport->WorkSize.y - kMargin;
        const float right = viewport->WorkPos.x + viewport->WorkSize.x - kMargin;
        const float left = right - Width;
        std::uint64_t held = 0;

        auto card = [&](const float height, const ImVec4 accent, const float alpha, const float slide)
        {
            const ImVec2 min(left + slide, bottom - height);
            const ImVec2 max(right + slide, bottom);
            list.AddRectFilled(ImVec2(min.x + 3.0f, min.y + 3.0f), ImVec2(max.x + 3.0f, max.y + 3.0f),
                               WithAlpha(ImVec4(0, 0, 0, 0.35f), alpha), rounding);
            list.AddRectFilled(min, max, WithAlpha(ImVec4(background.x, background.y, background.z, 0.96f), alpha), rounding);
            list.AddRectFilled(min, ImVec2(min.x + kAccentWidth, max.y), WithAlpha(accent, alpha), rounding,
                               ImDrawFlags_RoundCornersLeft);
            list.AddRect(min, max, WithAlpha(style.Colors[ImGuiCol_Border], alpha), rounding);
            return min;
        };

        for (const auto& task : tasks)
        {
            const auto snapshot = task->Read();
            const float barHeight = 6.0f;
            const auto elapsed = std::format("{:.1f} s", snapshot.ElapsedSeconds);
            const float detailHeight = TextHeight(snapshot.Detail, inner);
            const float height = pad * 2.0f + line + (detailHeight > 0.0f ? detailHeight + 2.0f : 0.0f) + 6.0f + barHeight;
            const auto accent = KindColor(NotificationKind::Info);
            const auto min = card(height, accent, 1.0f, 0.0f);
            float y = min.y + pad;
            const float x = min.x + kAccentWidth + pad;
            DrawWrapped(list, ImVec2(x, y), ImGui::GetColorU32(textColor), snapshot.Title, inner);
            const auto elapsedSize = ImGui::CalcTextSize(elapsed.c_str());
            list.AddText(ImVec2(x + inner - elapsedSize.x, y), ImGui::GetColorU32(dim), elapsed.c_str());
            y += line;
            if (detailHeight > 0.0f)
            {
                y += 2.0f;
                DrawWrapped(list, ImVec2(x, y), ImGui::GetColorU32(dim), snapshot.Detail, inner);
                y += detailHeight;
            }
            y += 6.0f;
            const ImVec2 barMin(x, y);
            const ImVec2 barMax(x + inner, y + barHeight);
            list.AddRectFilled(barMin, barMax, ImGui::GetColorU32(ImVec4(1, 1, 1, 0.12f)), barHeight * 0.5f);
            if (snapshot.Fraction >= 0.0f)
            {
                list.AddRectFilled(barMin, ImVec2(barMin.x + inner * snapshot.Fraction, barMax.y), ImGui::GetColorU32(accent),
                                   barHeight * 0.5f);
            }
            else
            {
                const float segment = inner * 0.3f;
                const float phase = std::fmod(static_cast<float>(ImGui::GetTime()) * 0.9f, 1.0f);
                const float start = barMin.x - segment + (inner + segment) * phase;
                list.PushClipRect(barMin, barMax, true);
                list.AddRectFilled(ImVec2(start, barMin.y), ImVec2(start + segment, barMax.y), ImGui::GetColorU32(accent),
                                   barHeight * 0.5f);
                list.PopClipRect();
            }
            bottom -= height + kGap;
        }

        const std::size_t first = toasts.size() > MaxToasts ? toasts.size() - MaxToasts : 0;
        for (std::size_t index = toasts.size(); index-- > first;)
        {
            const auto& toast = toasts[index];
            const float fadeIn = std::clamp(toast.AgeSeconds / kFadeSeconds, 0.0f, 1.0f);
            const float fadeOut = std::clamp((toast.DurationSeconds - toast.AgeSeconds) / kFadeSeconds, 0.0f, 1.0f);
            const float alpha = std::min(fadeIn, fadeOut);
            const float slide = (1.0f - fadeIn) * 40.0f;
            const float titleHeight = TextHeight(toast.Title, inner);
            const float messageHeight = TextHeight(toast.Message, inner);
            const float height = pad * 2.0f + std::max(titleHeight, line) + (messageHeight > 0.0f ? messageHeight + 2.0f : 0.0f);
            const auto accent = KindColor(toast.Kind);
            const auto min = card(height, accent, alpha, slide);

            const ImVec2 max(right + slide, bottom);
            if (ImGui::IsMouseHoveringRect(min, max, false))
            {
                held = toast.Id;
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) center.Dismiss(toast.Id);
            }

            const float x = min.x + kAccentWidth + pad;
            float y = min.y + pad;
            DrawWrapped(list, ImVec2(x, y), WithAlpha(accent, alpha), toast.Title, inner);
            y += std::max(titleHeight, line) + 2.0f;
            DrawWrapped(list, ImVec2(x, y), WithAlpha(textColor, alpha), toast.Message, inner);

            const float remaining = std::clamp(1.0f - toast.AgeSeconds / toast.DurationSeconds, 0.0f, 1.0f);
            list.AddRectFilled(ImVec2(min.x + kAccentWidth, max.y - 2.0f),
                               ImVec2(min.x + kAccentWidth + (Width - kAccentWidth) * remaining, max.y),
                               WithAlpha(accent, alpha * 0.6f));
            bottom -= height + kGap;
        }

        center.Tick(io.DeltaTime, held);
    }
}
