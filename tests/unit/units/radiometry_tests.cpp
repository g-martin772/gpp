#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

import GPP;
import std;

using namespace GPP;
using namespace GPP::Radiometry;
using Catch::Approx;

TEST_CASE("Radiometric dimensions resolve units and convert", "[units][radiometry]")
{
    using namespace GPP::Units;
    CHECK(Convert(1361.0, "W/m^2", "kW/m^2") == Approx(1.361));
    CHECK(Convert(683.0, "lx", "W/m^2") == Approx(1.0));
    CHECK(Convert(2.0, "kW/sr", "W/sr") == Approx(2000.0));
    CHECK(Convert(1.0, "W/m^2/sr", "mW/m^2/sr") == Approx(1000.0));
    CHECK(Convert(1.0, "Lsun", "W") == Approx(kSolarLuminosity));
    CHECK(PickDisplayUnit(Dimension::Irradiance, 1361.0)->Symbol == "kW/m\xC2\xB2");
    CHECK(PickDisplayUnit(Dimension::Irradiance, 0.5)->Symbol == "mW/m\xC2\xB2");
    CHECK(PickDisplayUnit(Dimension::Power, 40.0)->Symbol == "W");
    CHECK(PickDisplayUnit(Dimension::Radiance, 2.0e7)->Symbol == "kW/m\xC2\xB2/sr");

    const auto parsed = ParseQuantity("1.361 kW/m^2", Dimension::Irradiance);
    REQUIRE(parsed.has_value());
    CHECK(parsed->ValueSi == Approx(1361.0));
    CHECK(DimensionName(Dimension::RadiantIntensity) == "radiant intensity");
    CHECK(BaseUnit(Dimension::Radiance)->Symbol == "W/m\xC2\xB2/sr");
}

TEST_CASE("Planck radiance obeys Wien and Stefan-Boltzmann", "[radiometry]")
{
    constexpr double wienB = 2.897771955e-3;
    const double peak = wienB / 5778.0;
    CHECK(PlanckSpectralRadiance(peak, 5778.0) > PlanckSpectralRadiance(peak * 0.9, 5778.0));
    CHECK(PlanckSpectralRadiance(peak, 5778.0) > PlanckSpectralRadiance(peak * 1.1, 5778.0));

    double total = 0.0;
    const double step = 2e-9;
    for (double lambda = 20e-9; lambda < 200e-6; lambda += step) total += PlanckSpectralRadiance(lambda, 5778.0) * step;
    const double expected = Units::kStefanBoltzmann * std::pow(5778.0, 4.0) / static_cast<double>(Units::Pi());
    CHECK(total == Approx(expected).epsilon(2e-3));
}

TEST_CASE("Blackbody colour has unit luminance and the right hue", "[radiometry]")
{
    for (const double kelvin : {1500.0, 2700.0, 4000.0, 5778.0, 6500.0, 10000.0, 25000.0})
    {
        const auto color = BlackbodyColor(kelvin);
        CHECK(RelativeLuminance(color) == Approx(1.0f).epsilon(1e-5));
        CHECK(color.x >= 0.0f);
        CHECK(color.y >= 0.0f);
        CHECK(color.z >= 0.0f);
    }

    const auto warm = BlackbodyColor(2700.0);
    CHECK(warm.x > warm.y);
    CHECK(warm.y > warm.z);
    CHECK(warm.x / warm.z > 4.0f);

    const auto sun = BlackbodyColor(5778.0);
    CHECK(sun.x == Approx(sun.y).epsilon(0.16));
    CHECK(sun.z == Approx(sun.y).epsilon(0.16));
    CHECK(sun.x > sun.y);
    CHECK(sun.y > sun.z);

    const auto d65ish = BlackbodyColor(6500.0);
    CHECK(d65ish.x == Approx(1.0f).epsilon(0.08));
    CHECK(d65ish.y == Approx(1.0f).epsilon(0.08));
    CHECK(d65ish.z == Approx(1.0f).epsilon(0.08));

    const auto blue = BlackbodyColor(10000.0);
    CHECK(blue.z > blue.y);
    CHECK(blue.y > blue.x);
    CHECK(blue.z / blue.x > 1.4f);

    CHECK(BlackbodyColor(10.0) == BlackbodyColor(Radiometry::kBlackbodyMinKelvin));
}

TEST_CASE("Solar luminous efficacy and lux conversion", "[radiometry]")
{
    const double efficacy = BlackbodyLuminousEfficacy(5778.0);
    CHECK(efficacy == Approx(93.0).margin(4.0));
    const double lux = 1361.0 * efficacy;
    CHECK(lux == Approx(1.27e5).epsilon(0.05));
    CHECK(LuminanceFromRadiance(1.0) == Approx(683.0));
    CHECK(RadianceFromLuminance(683.0) == Approx(1.0));
}

TEST_CASE("Physical camera exposure follows EV100", "[radiometry][camera]")
{
    CHECK(Ev100(1.0, 1.0, 100.0) == Approx(0.0).margin(1e-12));
    CHECK(Ev100(16.0, 1.0 / 100.0, 100.0) == Approx(14.6439).epsilon(1e-4));
    CHECK(Ev100(2.8, 1.0 / 60.0, 400.0) == Approx(std::log2(2.8 * 2.8 * 60.0 / 4.0)));

    PhysicalCamera camera{.Aperture = 4.0, .Shutter = 1.0 / 250.0, .Iso = 200.0};
    CHECK(Ev100(camera) == Approx(std::log2(16.0 * 250.0 / 2.0)));

    CHECK(ExposureScaleFromEv100(0.0) == Approx(1.0 / 1.2));
    CHECK(ExposureScaleFromEv100(1.0) == Approx(ExposureScaleFromEv100(0.0) / 2.0));
    CHECK(ExposureScale(camera) == Approx(1.0 / (1.2 * std::exp2(Ev100(camera)))));

    for (const double ev : {-4.0, 0.0, 5.3, 14.64})
    {
        CHECK(Ev100FromExposureScale(ExposureScaleFromEv100(ev)) == Approx(ev).margin(1e-9));
        CHECK(Ev100FromLuminance(LuminanceFromEv100(ev)) == Approx(ev).margin(1e-9));
        const double aperture = 5.6;
        const double iso = 200.0;
        const double shutter = ShutterForEv100(ev, aperture, iso);
        CHECK(Ev100(aperture, shutter, iso) == Approx(ev).margin(1e-9));
        CHECK(Ev100(ApertureForEv100(ev, shutter, iso), shutter, iso) == Approx(ev).margin(1e-9));
        CHECK(Ev100(aperture, shutter, IsoForEv100(ev, aperture, shutter)) == Approx(ev).margin(1e-9));
    }

    CHECK(LuminanceFromEv100(Ev100FromLuminance(18.0)) == Approx(18.0));
    CHECK(ExposureScaleFromEv100(Ev100FromLuminance(1000.0)) * 1000.0 == Approx(12.5 / 120.0));
}
