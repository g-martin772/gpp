export module GPP.Core:Radiometry;

import std;
import glm;
import :Units;

export namespace GPP::Radiometry
{
    inline constexpr double kLuminousEfficacyMax = Units::kLuminousEfficacy540THz;
    inline constexpr double kHdrReferenceWhite = 203.0; // cd/m^2, ITU-R BT.2408

    [[nodiscard]] constexpr double LuminanceFromRadiance(const double radiance, const double efficacy = kLuminousEfficacyMax)
    {
        return radiance * efficacy;
    }

    [[nodiscard]] constexpr double RadianceFromLuminance(const double luminance, const double efficacy = kLuminousEfficacyMax)
    {
        return luminance / efficacy;
    }

    [[nodiscard]] inline double PlanckSpectralRadiance(const double wavelengthM, const double kelvin)
    {
        const double h = Units::kPlanckConstant;
        const double c = Units::kSpeedOfLight;
        const double k = Units::kBoltzmannConstant;
        const double x = h * c / (wavelengthM * k * kelvin);
        if (x > 700.0) return 0.0;
        return 2.0 * h * c * c / (std::pow(wavelengthM, 5.0) * std::expm1(x));
    }

    struct Xyz
    {
        double X = 0.0;
        double Y = 0.0;
        double Z = 0.0;
    };

    namespace Detail
    {
        [[nodiscard]] inline double Lobe(const double lambda, const double mean, const double sigmaLow, const double sigmaHigh)
        {
            const double t = (lambda - mean) / (lambda < mean ? sigmaLow : sigmaHigh);
            return std::exp(-0.5 * t * t);
        }
    }

    // Wyman, Sloan & Shirley 2013, multi-lobe fit of the CIE 1931 2-degree observer.
    [[nodiscard]] inline Xyz CieMatchingFunctions(const double wavelengthNm)
    {
        using Detail::Lobe;
        const double l = wavelengthNm;
        return {
            1.056 * Lobe(l, 599.8, 37.9, 31.0) + 0.362 * Lobe(l, 442.0, 16.0, 26.7) - 0.065 * Lobe(l, 501.1, 20.4, 26.2),
            0.821 * Lobe(l, 568.8, 46.9, 40.5) + 0.286 * Lobe(l, 530.9, 16.3, 31.1),
            1.217 * Lobe(l, 437.0, 11.8, 36.0) + 0.681 * Lobe(l, 459.0, 26.0, 13.8)};
    }

    [[nodiscard]] inline Xyz BlackbodyXyz(const double kelvin)
    {
        Xyz sum;
        for (int nm = 360; nm <= 830; ++nm)
        {
            const double radiance = PlanckSpectralRadiance(nm * 1e-9, kelvin);
            const auto match = CieMatchingFunctions(nm);
            sum.X += radiance * match.X;
            sum.Y += radiance * match.Y;
            sum.Z += radiance * match.Z;
        }
        return sum;
    }

    [[nodiscard]] inline glm::vec3 XyzToLinearSrgb(const Xyz& xyz)
    {
        return {static_cast<float>(3.2404542 * xyz.X - 1.5371385 * xyz.Y - 0.4985314 * xyz.Z),
                static_cast<float>(-0.9692660 * xyz.X + 1.8760108 * xyz.Y + 0.0415560 * xyz.Z),
                static_cast<float>(0.0556434 * xyz.X - 0.2040259 * xyz.Y + 1.0572252 * xyz.Z)};
    }

    [[nodiscard]] inline float RelativeLuminance(const glm::vec3& linearSrgb)
    {
        return glm::dot(linearSrgb, glm::vec3(0.2126f, 0.7152f, 0.0722f));
    }

    inline constexpr double kBlackbodyMinKelvin = 1000.0;
    inline constexpr double kBlackbodyMaxKelvin = 40000.0;

    // Chromaticity of a Planckian emitter as linear sRGB with relative luminance 1 (out-of-gamut channels clamped).
    [[nodiscard]] inline glm::vec3 BlackbodyColor(double kelvin)
    {
        kelvin = std::clamp(kelvin, kBlackbodyMinKelvin, kBlackbodyMaxKelvin);
        auto rgb = glm::max(XyzToLinearSrgb(BlackbodyXyz(kelvin)), glm::vec3(0.0f));
        const float luminance = RelativeLuminance(rgb);
        return luminance > 0.0f ? rgb / luminance : glm::vec3(1.0f);
    }

    // Luminous efficacy of radiation [lm/W] of a blackbody: K_cd * int B y dlambda / (sigma T^4 / pi)
    [[nodiscard]] inline double BlackbodyLuminousEfficacy(double kelvin)
    {
        kelvin = std::clamp(kelvin, kBlackbodyMinKelvin, kBlackbodyMaxKelvin);
        double y = 0.0;
        for (int nm = 360; nm <= 830; ++nm)
        {
            y += PlanckSpectralRadiance(nm * 1e-9, kelvin) * CieMatchingFunctions(nm).Y * 1e-9;
        }
        const double total = Units::kStefanBoltzmann * kelvin * kelvin * kelvin * kelvin / static_cast<double>(Units::Pi());
        return kLuminousEfficacyMax * y / total;
    }

    struct PhysicalCamera
    {
        double Aperture = 2.8;
        double Shutter = 1.0 / 125.0;
        double Iso = 100.0;
    };

    inline constexpr double kExposureCalibration = 1.2;
    inline constexpr double kMeterCalibration = 12.5;

    [[nodiscard]] inline double Ev100(const double aperture, const double shutter, const double iso)
    {
        return std::log2(aperture * aperture / shutter * 100.0 / iso);
    }

    [[nodiscard]] inline double Ev100(const PhysicalCamera& camera)
    {
        return Ev100(camera.Aperture, camera.Shutter, camera.Iso);
    }

    [[nodiscard]] inline double ExposureScaleFromEv100(const double ev100)
    {
        return 1.0 / (kExposureCalibration * std::exp2(ev100));
    }

    [[nodiscard]] inline double Ev100FromExposureScale(const double scale)
    {
        return std::log2(1.0 / (kExposureCalibration * scale));
    }

    [[nodiscard]] inline double ExposureScale(const PhysicalCamera& camera)
    {
        return ExposureScaleFromEv100(Ev100(camera));
    }

    // EV100 that maps the given average scene luminance [cd/m^2] to middle grey (reflected-light meter, K = 12.5).
    [[nodiscard]] inline double Ev100FromLuminance(const double luminance)
    {
        return std::log2(luminance * 100.0 / kMeterCalibration);
    }

    [[nodiscard]] inline double LuminanceFromEv100(const double ev100)
    {
        return std::exp2(ev100) * kMeterCalibration / 100.0;
    }

    [[nodiscard]] inline double ShutterForEv100(const double ev100, const double aperture, const double iso)
    {
        return aperture * aperture * 100.0 / (iso * std::exp2(ev100));
    }

    [[nodiscard]] inline double ApertureForEv100(const double ev100, const double shutter, const double iso)
    {
        return std::sqrt(shutter * iso * std::exp2(ev100) / 100.0);
    }

    [[nodiscard]] inline double IsoForEv100(const double ev100, const double aperture, const double shutter)
    {
        return aperture * aperture * 100.0 / (shutter * std::exp2(ev100));
    }
}
