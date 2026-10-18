export module GPP.Graphics:Pbr;

import std;
import glm;

export namespace GPP::Pbr
{
    inline constexpr double kPi = std::numbers::pi;
    inline constexpr double kMinRoughness = 0.045;
    inline constexpr int kDfgLutWidth = 64;
    inline constexpr int kDfgLutHeight = 32;

    struct Surface
    {
        glm::dvec3 BaseColor{1.0};
        double Metallic = 0.0;
        double Roughness = 0.5;
        double Ior = 1.5;
        glm::dvec3 SpecularColor{1.0};
        double SpecularFactor = 1.0;
    };

    [[nodiscard]] inline double F0FromIor(const double ior)
    {
        const double r = (ior - 1.0) / (ior + 1.0);
        return r * r;
    }

    [[nodiscard]] inline double Ggx(const double noh, const double alpha)
    {
        const double a2 = alpha * alpha;
        const double d = noh * noh * (a2 - 1.0) + 1.0;
        return a2 / (kPi * d * d);
    }

    [[nodiscard]] inline double SmithVisibility(const double nov, const double nol, const double alpha)
    {
        const double a2 = alpha * alpha;
        const double v = nol * std::sqrt(nov * nov * (1.0 - a2) + a2);
        const double l = nov * std::sqrt(nol * nol * (1.0 - a2) + a2);
        return 0.5 / (v + l);
    }

    [[nodiscard]] inline double SmithG1(const double nov, const double alpha)
    {
        const double a2 = alpha * alpha;
        return 2.0 * nov / (nov + std::sqrt(a2 + (1.0 - a2) * nov * nov));
    }

    [[nodiscard]] inline double FresnelWeight(const double voh)
    {
        const double m = std::clamp(1.0 - voh, 0.0, 1.0);
        const double m2 = m * m;
        return m2 * m2 * m;
    }

    [[nodiscard]] inline glm::dvec2 Hammersley(const std::uint32_t i, const std::uint32_t n)
    {
        std::uint32_t bits = i;
        bits = (bits << 16u) | (bits >> 16u);
        bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
        bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
        bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
        bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
        return {(static_cast<double>(i) + 0.5) / static_cast<double>(n), static_cast<double>(bits) * 2.3283064365386963e-10};
    }

    // Local frame, N = +Z. Heitz 2018 visible-normal sampling of the GGX distribution.
    [[nodiscard]] inline glm::dvec3 SampleVisibleNormal(const glm::dvec3& v, const double alpha, const double u1, const double u2)
    {
        const glm::dvec3 vh = glm::normalize(glm::dvec3(alpha * v.x, alpha * v.y, v.z));
        const double lensq = vh.x * vh.x + vh.y * vh.y;
        const glm::dvec3 t1 = lensq > 0.0 ? glm::dvec3(-vh.y, vh.x, 0.0) / std::sqrt(lensq) : glm::dvec3(1.0, 0.0, 0.0);
        const glm::dvec3 t2 = glm::cross(vh, t1);
        const double r = std::sqrt(u1);
        const double phi = 2.0 * kPi * u2;
        const double a = r * std::cos(phi);
        double b = r * std::sin(phi);
        const double s = 0.5 * (1.0 + vh.z);
        b = (1.0 - s) * std::sqrt(std::max(0.0, 1.0 - a * a)) + s * b;
        const glm::dvec3 nh = a * t1 + b * t2 + std::sqrt(std::max(0.0, 1.0 - a * a - b * b)) * vh;
        return glm::normalize(glm::dvec3(alpha * nh.x, alpha * nh.y, std::max(0.0, nh.z)));
    }

    // x = A (weight of F0), y = B (weight of F90), z = A averaged over the cosine-weighted hemisphere, w = B averaged.
    class DfgTable
    {
    public:
        DfgTable(const int width, const int height, std::vector<glm::dvec4> texels)
            : m_Width(width), m_Height(height), m_Texels(std::move(texels))
        {
        }

        [[nodiscard]] int Width() const noexcept { return m_Width; }
        [[nodiscard]] int Height() const noexcept { return m_Height; }
        [[nodiscard]] const std::vector<glm::dvec4>& Texels() const noexcept { return m_Texels; }

        [[nodiscard]] glm::dvec4 Lookup(const double nov, const double roughness) const
        {
            const double x = std::clamp(nov, 0.0, 1.0) * (m_Width - 1);
            const double y = std::clamp(roughness, 0.0, 1.0) * (m_Height - 1);
            const int x0 = std::min(static_cast<int>(x), m_Width - 2);
            const int y0 = std::min(static_cast<int>(y), m_Height - 2);
            const double fx = x - x0;
            const double fy = y - y0;
            const auto at = [&](const int ix, const int iy) { return m_Texels[static_cast<std::size_t>(iy * m_Width + ix)]; };
            return glm::mix(glm::mix(at(x0, y0), at(x0 + 1, y0), fx), glm::mix(at(x0, y0 + 1), at(x0 + 1, y0 + 1), fx), fy);
        }

    private:
        int m_Width;
        int m_Height;
        std::vector<glm::dvec4> m_Texels;
    };

    [[nodiscard]] inline glm::dvec2 IntegrateDfg(const double nov, const double roughness, const std::uint32_t samples)
    {
        const double mu = std::max(nov, 1e-3);
        const double alpha = std::max(roughness, kMinRoughness) * std::max(roughness, kMinRoughness);
        const glm::dvec3 v(std::sqrt(1.0 - mu * mu), 0.0, mu);
        glm::dvec2 sum(0.0);
        for (std::uint32_t i = 0; i < samples; ++i)
        {
            const auto u = Hammersley(i, samples);
            const auto h = SampleVisibleNormal(v, alpha, u.x, u.y);
            const double voh = glm::dot(v, h);
            const glm::dvec3 l = 2.0 * voh * h - v;
            if (l.z <= 0.0 || voh <= 0.0) continue;
            const double g2 = SmithVisibility(mu, l.z, alpha) * 4.0 * mu * l.z;
            const double ratio = g2 / SmithG1(mu, alpha);
            const double fc = FresnelWeight(voh);
            sum += glm::dvec2((1.0 - fc) * ratio, fc * ratio);
        }
        return sum / static_cast<double>(samples);
    }

    [[nodiscard]] inline DfgTable BuildDfgTable(const int width = kDfgLutWidth, const int height = kDfgLutHeight,
                                                const std::uint32_t samples = 1024)
    {
        std::vector<glm::dvec4> texels(static_cast<std::size_t>(width) * height);
        constexpr int averageSteps = 64;
        for (int y = 0; y < height; ++y)
        {
            const double roughness = static_cast<double>(y) / (height - 1);
            glm::dvec2 average(0.0);
            for (int i = 0; i < averageSteps; ++i)
            {
                const double mu = (i + 0.5) / averageSteps;
                average += 2.0 * mu * IntegrateDfg(mu, roughness, samples) / static_cast<double>(averageSteps);
            }
            for (int x = 0; x < width; ++x)
            {
                const double nov = static_cast<double>(x) / (width - 1);
                const auto ab = IntegrateDfg(nov, roughness, samples);
                texels[static_cast<std::size_t>(y) * width + x] = {ab.x, ab.y, average.x, average.y};
            }
        }
        return {width, height, std::move(texels)};
    }

    inline constexpr int kBuiltinDfgWidth = 32;
    inline constexpr int kBuiltinDfgHeight = 32;

    // Source of shaders/pbr/dfg_table.glsl, the built-in lookup used when PBR_DFG_LOOKUP is not defined.
    [[nodiscard]] inline std::string FormatDfgTableGlsl(const DfgTable& table)
    {
        std::string out = "// Generated from GPP::Pbr::BuildDfgTable; the pbr_shader_tests regenerate it with\n"
                          "// GPP_REGENERATE_SHADER_TABLES=1 and fail when it drifts.\n";
        out += std::format("#define PBR_DFG_TABLE_WIDTH {}\n#define PBR_DFG_TABLE_HEIGHT {}\n", table.Width(), table.Height());
        out += std::format("const vec4 PBR_DFG_TABLE[{}] = vec4[{}](\n", table.Texels().size(), table.Texels().size());
        for (std::size_t i = 0; i < table.Texels().size(); ++i)
        {
            const auto& t = table.Texels()[i];
            out += std::format("    vec4({:.6g}, {:.6g}, {:.6g}, {:.6g}){}\n", t.x, t.y, t.z, t.w,
                               i + 1 < table.Texels().size() ? "," : "");
        }
        out += ");\n";
        return out;
    }

    // Precomputed per (surface, view) terms, mirrors PbrLobes in pbr/brdf.glsl.
    struct Lobes
    {
        glm::dvec3 Diffuse{0.0};
        glm::dvec3 F0{0.04};
        glm::dvec3 F90{1.0};
        double Alpha = 0.25;
        double Roughness = 0.5;
        double NoV = 1.0;
        glm::dvec3 Fms{0.0};
        double EWhiteView = 0.0;
        double EWhiteAverage = 0.0;
        glm::dvec3 SpecularAlbedoView{0.0};
        glm::dvec3 SpecularAlbedoAverage{0.0};
        double SpecularProbability = 0.5;
    };

    [[nodiscard]] inline double Luminance(const glm::dvec3& c)
    {
        return glm::dot(c, glm::dvec3(0.2126, 0.7152, 0.0722));
    }

    [[nodiscard]] inline glm::dvec3 SpecularAlbedo(const Lobes& b, const glm::dvec4& dfg, const glm::dvec3& fms)
    {
        return b.F0 * dfg.x + b.F90 * dfg.y + fms * (1.0 - dfg.x - dfg.y);
    }

    [[nodiscard]] inline Lobes MakeLobes(const Surface& s, const glm::dvec3& n, const glm::dvec3& v, const DfgTable& dfg)
    {
        Lobes b;
        b.Roughness = std::clamp(s.Roughness, kMinRoughness, 1.0);
        b.Alpha = b.Roughness * b.Roughness;
        b.NoV = std::max(glm::dot(n, v), 1e-4);
        const double metallic = std::clamp(s.Metallic, 0.0, 1.0);
        const glm::dvec3 dielectricF0 = glm::min(glm::dvec3(F0FromIor(s.Ior)) * s.SpecularColor, glm::dvec3(1.0)) * s.SpecularFactor;
        b.F0 = glm::mix(dielectricF0, s.BaseColor, metallic);
        b.F90 = glm::mix(glm::dvec3(s.SpecularFactor), glm::dvec3(1.0), metallic);
        const glm::dvec3 diffuseColor = s.BaseColor * (1.0 - metallic);

        const auto t = dfg.Lookup(b.NoV, b.Roughness);
        b.EWhiteView = t.x + t.y;
        b.EWhiteAverage = t.z + t.w;
        const glm::dvec3 favg = b.F0 + (b.F90 - b.F0) / 21.0;
        b.Fms = favg * favg * b.EWhiteAverage / (glm::dvec3(1.0) - favg * (1.0 - b.EWhiteAverage));
        b.SpecularAlbedoView = SpecularAlbedo(b, t, b.Fms);
        b.SpecularAlbedoAverage = b.F0 * t.z + b.F90 * t.w + b.Fms * (1.0 - b.EWhiteAverage);
        b.Diffuse = diffuseColor;

        const double single = Luminance(b.F0 * t.x + b.F90 * t.y);
        const double rest = Luminance(b.Fms * (1.0 - b.EWhiteView) + diffuseColor * (glm::dvec3(1.0) - b.SpecularAlbedoView));
        b.SpecularProbability = single <= 0.0 ? 0.0 : rest <= 0.0 ? 1.0 : std::clamp(single / (single + rest), 0.02, 0.98);
        return b;
    }

    [[nodiscard]] inline glm::dvec3 Eval(const Lobes& b, const glm::dvec3& n, const glm::dvec3& v, const glm::dvec3& l,
                                         const DfgTable& dfg)
    {
        const double nol = glm::dot(n, l);
        if (nol <= 0.0) return glm::dvec3(0.0);
        const double nov = b.NoV;
        const glm::dvec3 h = glm::normalize(v + l);
        const double noh = std::clamp(glm::dot(n, h), 0.0, 1.0);
        const double voh = std::clamp(glm::dot(v, h), 0.0, 1.0);

        const glm::dvec3 f = b.F0 + (b.F90 - b.F0) * FresnelWeight(voh);
        const glm::dvec3 single = Ggx(noh, b.Alpha) * SmithVisibility(nov, nol, b.Alpha) * f;

        const auto ti = dfg.Lookup(std::min(nol, 1.0), b.Roughness);
        const double eo = b.EWhiteView;
        const double ei = ti.x + ti.y;
        const double guardWhite = std::max(1.0 - b.EWhiteAverage, 1e-4);
        const glm::dvec3 multi = b.Fms * ((1.0 - eo) * (1.0 - ei) / (kPi * guardWhite));

        const glm::dvec3 esI = SpecularAlbedo(b, ti, b.Fms);
        const glm::dvec3 guard = glm::max(glm::dvec3(1.0) - b.SpecularAlbedoAverage, glm::dvec3(1e-4));
        const glm::dvec3 diffuse = b.Diffuse * (glm::dvec3(1.0) - b.SpecularAlbedoView) * (glm::dvec3(1.0) - esI) / (kPi * guard);
        return single + multi + diffuse;
    }

    [[nodiscard]] inline double Pdf(const Lobes& b, const glm::dvec3& n, const glm::dvec3& v, const glm::dvec3& l)
    {
        const double nol = glm::dot(n, l);
        if (nol <= 0.0) return 0.0;
        const glm::dvec3 h = glm::normalize(v + l);
        const double noh = std::clamp(glm::dot(n, h), 0.0, 1.0);
        const double nov = b.NoV;
        const double sqrtTerm = std::sqrt(b.Alpha * b.Alpha + (1.0 - b.Alpha * b.Alpha) * nov * nov);
        const double pdfSpec = Ggx(noh, b.Alpha) / (2.0 * (nov + sqrtTerm));
        return b.SpecularProbability * pdfSpec + (1.0 - b.SpecularProbability) * nol / kPi;
    }

    inline void Basis(const glm::dvec3& n, glm::dvec3& t, glm::dvec3& bt)
    {
        const double s = n.z >= 0.0 ? 1.0 : -1.0;
        const double a = -1.0 / (s + n.z);
        const double b = n.x * n.y * a;
        t = {1.0 + s * n.x * n.x * a, s * b, -s * n.x};
        bt = {b, s + n.y * n.y * a, -n.y};
    }

    struct Sample
    {
        glm::dvec3 L{0.0, 0.0, 1.0};
        double Pdf = 0.0;
        glm::dvec3 Weight{0.0}; // f * cos / pdf
    };

    [[nodiscard]] inline Sample SampleBsdf(const Lobes& b, const glm::dvec3& n, const glm::dvec3& v, glm::dvec3 u,
                                           const DfgTable& dfg)
    {
        glm::dvec3 t, bt;
        Basis(n, t, bt);
        const glm::dvec3 vLocal(glm::dot(v, t), glm::dot(v, bt), glm::dot(v, n));
        glm::dvec3 lLocal;
        if (u.x < b.SpecularProbability)
        {
            u.x /= b.SpecularProbability;
            const glm::dvec3 h = SampleVisibleNormal(vLocal, b.Alpha, u.x, u.y);
            lLocal = 2.0 * glm::dot(vLocal, h) * h - vLocal;
        }
        else
        {
            u.x = (u.x - b.SpecularProbability) / (1.0 - b.SpecularProbability);
            const double r = std::sqrt(u.y);
            const double phi = 2.0 * kPi * u.z;
            lLocal = {r * std::cos(phi), r * std::sin(phi), std::sqrt(std::max(0.0, 1.0 - u.y))};
        }
        Sample out;
        if (lLocal.z <= 0.0) return out;
        out.L = t * lLocal.x + bt * lLocal.y + n * lLocal.z;
        out.Pdf = Pdf(b, n, v, out.L);
        if (out.Pdf <= 0.0) return out;
        out.Weight = Eval(b, n, v, out.L, dfg) * glm::dot(n, out.L) / out.Pdf;
        return out;
    }

    // Albedo of the full lobe set by sampling (white furnace integrand); the mean over many samples.
    [[nodiscard]] inline glm::dvec3 DirectionalAlbedo(const Surface& s, const double nov, const DfgTable& dfg,
                                                      const std::uint32_t samples)
    {
        const glm::dvec3 n(0.0, 0.0, 1.0);
        const glm::dvec3 v(std::sqrt(1.0 - nov * nov), 0.0, nov);
        const auto lobes = MakeLobes(s, n, v, dfg);
        glm::dvec3 sum(0.0);
        for (std::uint32_t i = 0; i < samples; ++i)
        {
            const auto a = Hammersley(i, samples);
            const double w = std::fmod(static_cast<double>(i) * 0.6180339887498949 + 0.1, 1.0);
            sum += SampleBsdf(lobes, n, v, {a.x, a.y, w}, dfg).Weight;
        }
        return sum / static_cast<double>(samples);
    }
}
