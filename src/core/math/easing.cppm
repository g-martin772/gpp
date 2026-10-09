export module GPP.Core:Easing;

import std;

export namespace GPP
{
    enum class EaseKind { Linear, EaseIn, EaseOut, EaseInOut, CubicIn, CubicOut, CubicInOut, Smoothstep };

    constexpr std::array<EaseKind, 8> kEaseKinds = {EaseKind::Linear,  EaseKind::EaseIn,    EaseKind::EaseOut,
                                                    EaseKind::EaseInOut, EaseKind::CubicIn, EaseKind::CubicOut,
                                                    EaseKind::CubicInOut, EaseKind::Smoothstep};

    // t is clamped to [0, 1]; every curve maps 0 to 0 and 1 to 1.
    [[nodiscard]] constexpr float Ease(const EaseKind kind, float t)
    {
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        switch (kind)
        {
        case EaseKind::Linear: return t;
        case EaseKind::EaseIn: return t * t;
        case EaseKind::EaseOut: return 1.0f - (1.0f - t) * (1.0f - t);
        case EaseKind::EaseInOut: return t < 0.5f ? 2.0f * t * t : 1.0f - 2.0f * (1.0f - t) * (1.0f - t);
        case EaseKind::CubicIn: return t * t * t;
        case EaseKind::CubicOut:
        {
            const float u = 1.0f - t;
            return 1.0f - u * u * u;
        }
        case EaseKind::CubicInOut:
        {
            const float u = 1.0f - t;
            return t < 0.5f ? 4.0f * t * t * t : 1.0f - 4.0f * u * u * u;
        }
        case EaseKind::Smoothstep: return t * t * (3.0f - 2.0f * t);
        }
        return t;
    }

    [[nodiscard]] constexpr std::string_view EaseName(const EaseKind kind)
    {
        switch (kind)
        {
        case EaseKind::Linear: return "linear";
        case EaseKind::EaseIn: return "easeIn";
        case EaseKind::EaseOut: return "easeOut";
        case EaseKind::EaseInOut: return "easeInOut";
        case EaseKind::CubicIn: return "cubicIn";
        case EaseKind::CubicOut: return "cubicOut";
        case EaseKind::CubicInOut: return "cubicInOut";
        case EaseKind::Smoothstep: return "smoothstep";
        }
        return "linear";
    }

    [[nodiscard]] constexpr std::optional<EaseKind> EaseFromName(const std::string_view name)
    {
        for (const auto kind : kEaseKinds)
        {
            if (EaseName(kind) == name) return kind;
        }
        return std::nullopt;
    }

    // Frame-rate independent exponential smoothing factor: the fraction of the remaining distance covered in dt
    // when the remaining distance halves every halfLife seconds.
    [[nodiscard]] inline float SmoothingFactor(const float halfLife, const float dt)
    {
        if (halfLife <= 0.0f) return 1.0f;
        return 1.0f - std::exp2(-dt / halfLife);
    }
}
