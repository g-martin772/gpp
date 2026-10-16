#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

import GPP;
import std;

using namespace GPP::Units;
using Catch::Approx;

TEST_CASE("Defining constants are exact and derived ones agree with CODATA", "[units]")
{
    CHECK(kSpeedOfLight == 299792458.0);
    CHECK(kAstronomicalUnit == 149597870700.0);
    CHECK(kJulianYear == 31557600.0);
    CHECK(kStefanBoltzmann == Approx(5.670374419e-8).epsilon(1e-9));
    CHECK(kParsec == Approx(3.0856775814913673e16).epsilon(1e-15));
    CHECK(kLightYear == Approx(9.4607304725808e15).epsilon(1e-14));
    CHECK(kSolarMass == Approx(1.98841e30).epsilon(1e-5));
    CHECK(kSolarGravitationalRadius == Approx(1476.625).epsilon(1e-6));
    CHECK(kSolarGeometricTime == Approx(4.925490947e-6).epsilon(1e-9));
}

TEST_CASE("Wide constants carry more digits than double when float128 exists", "[units]")
{
    if constexpr (kHasFloat128)
    {
        CHECK(kStefanBoltzmannW != static_cast<Wide>(kStefanBoltzmann));
    }
    CHECK(CpuSupports(Precision::F64));
    CHECK(CpuSupports(Precision::F128) == kHasFloat128);
}

TEST_CASE("Geometrized quantities match textbook values", "[units]")
{
    CHECK(SchwarzschildRadius(kSolarMass) == Approx(2953.25).epsilon(1e-7));
    CHECK(IscoInGravitationalRadii(0.0) == Approx(6.0));
    CHECK(IscoInGravitationalRadii(1.0) == Approx(1.0).margin(1e-6));
    CHECK(IscoInGravitationalRadii(1.0, false) == Approx(9.0));
    CHECK(HorizonRadiusInGravitationalRadii(0.0, 0.0) == Approx(2.0));
    CHECK(HorizonRadiusInGravitationalRadii(0.6, 0.0, false) == Approx(0.2));
    CHECK(std::isnan(HorizonRadiusInGravitationalRadii(0.8, 0.8)));
    CHECK(EddingtonLuminosity(kSolarMass) == Approx(1.257e31).epsilon(2e-3));

    const double mass = 10.0 * kSolarMass;
    const double isco = 6.0 * GravitationalRadius(mass);
    CHECK(KeplerPeriod(isco, mass) == Approx(4.55e-3).epsilon(5e-3));
}

TEST_CASE("Geometrized functions work in every precision", "[units]")
{
    const float f = SchwarzschildRadius(static_cast<float>(kSolarMass));
    CHECK(f == Approx(2953.25f).epsilon(1e-5));
    if constexpr (kHasFloat128)
    {
        const Wide w = SchwarzschildRadius<Wide>(kSolarMassW);
        CHECK(static_cast<double>(w) == Approx(2953.25).epsilon(1e-7));
    }
}

TEST_CASE("Display unit selection follows the magnitude", "[units][display]")
{
    CHECK(PickDisplayUnit(Dimension::Length, 0.0)->Symbol == "m");
    CHECK(PickDisplayUnit(Dimension::Length, 2.5)->Symbol == "m");
    CHECK(PickDisplayUnit(Dimension::Length, 5000.0)->Symbol == "km");
    CHECK(PickDisplayUnit(Dimension::Length, -5000.0)->Symbol == "km");
    CHECK(PickDisplayUnit(Dimension::Length, 1.5 * kAstronomicalUnit)->Symbol == "AU");
    CHECK(PickDisplayUnit(Dimension::Length, 2.0 * kLightYear)->Symbol == "ly");
    CHECK(PickDisplayUnit(Dimension::Length, 8.2e3 * kParsec)->Symbol == "kpc");
    CHECK(PickDisplayUnit(Dimension::Length, 3.0e6 * kParsec)->Symbol == "Mpc");
    CHECK(PickDisplayUnit(Dimension::Length, 1e-4)->Symbol == "\xC2\xB5m");

    CHECK(PickDisplayUnit(Dimension::Mass, 1e20)->Symbol == "kg");
    CHECK(PickDisplayUnit(Dimension::Mass, 0.09 * kSolarMass)->Symbol == "kg");
    CHECK(PickDisplayUnit(Dimension::Mass, 0.1 * kSolarMass)->Symbol == "M\xE2\x98\x89");
    CHECK(PickDisplayUnit(Dimension::Mass, 10.0 * kSolarMass)->Symbol == "M\xE2\x98\x89");
    CHECK(PickDisplayUnit(Dimension::Mass, kSagittariusAStarMass)->Symbol == "Sgr A*");

    CHECK(PickDisplayUnit(Dimension::Time, 0.5)->Symbol == "ms");
    CHECK(PickDisplayUnit(Dimension::Time, 30.0)->Symbol == "s");
    CHECK(PickDisplayUnit(Dimension::Time, 3 * 86400.0)->Symbol == "d");
    CHECK(PickDisplayUnit(Dimension::Time, 5.0 * kJulianYear)->Symbol == "yr");
    CHECK(PickDisplayUnit(Dimension::Time, 3.0e6 * kJulianYear)->Symbol == "Myr");

    CHECK(PickDisplayUnit(Dimension::Velocity, 0.2 * kSpeedOfLight)->Symbol == "c");
}

TEST_CASE("Quantities round trip through text", "[units][display]")
{
    for (const double value : {1.0, 0.0, -3.7e3, 4.2e11, 9.46e15, 2.2e22, 1e-5})
    {
        const auto text = FormatQuantity(value, Dimension::Length, 17);
        const auto parsed = ParseQuantity(text, Dimension::Length);
        INFO(text);
        REQUIRE(parsed);
        CHECK(parsed->ValueSi == Approx(value).epsilon(1e-12));
    }
    for (const double value : {1e20, 2e30, 3e36, 1.5e41})
    {
        const auto text = FormatQuantity(value, Dimension::Mass, 17);
        const auto parsed = ParseQuantity(text, Dimension::Mass);
        INFO(text);
        REQUIRE(parsed);
        CHECK(parsed->ValueSi == Approx(value).epsilon(1e-12));
    }
}

TEST_CASE("Parsing accepts aliases, prefixes and a default unit", "[units][display]")
{
    CHECK(ParseQuantity("1.5 AU", Dimension::Length)->ValueSi == Approx(1.5 * kAstronomicalUnit));
    CHECK(ParseQuantity("1.5au", Dimension::Length)->ValueSi == Approx(1.5 * kAstronomicalUnit));
    CHECK(ParseQuantity("3 Lj", Dimension::Length)->ValueSi == Approx(3 * kLightYear));
    CHECK(ParseQuantity("2 mpc", Dimension::Length)->ValueSi == Approx(2e6 * kParsec));
    CHECK(ParseQuantity("7 mm", Dimension::Length)->ValueSi == Approx(7e-3));
    CHECK(ParseQuantity("4e3km", Dimension::Length)->ValueSi == Approx(4e6));
    CHECK(ParseQuantity("12 Gm", Dimension::Length)->ValueSi == Approx(12e9));
    CHECK(ParseQuantity("10 Msun", Dimension::Mass)->ValueSi == Approx(10 * kSolarMass));
    CHECK(ParseQuantity("1 Mearth", Dimension::Mass)->ValueSi == Approx(kEarthMass));
    CHECK(ParseQuantity("1 SgrA*", Dimension::Mass)->ValueSi == Approx(kSagittariusAStarMass));
    CHECK(ParseQuantity("2 days", Dimension::Time)->ValueSi == Approx(172800.0));
    CHECK(ParseQuantity("3 Myr", Dimension::Time)->ValueSi == Approx(3e6 * kJulianYear));
    CHECK(ParseQuantity("0.1 c", Dimension::Velocity)->ValueSi == Approx(0.1 * kSpeedOfLight));

    const UnitDef* km = FindUnit(Dimension::Length, "km")->Unit;
    CHECK(ParseQuantity("2.5", Dimension::Length, km)->ValueSi == Approx(2500.0));
    CHECK(ParseQuantity("2.5", Dimension::Length)->ValueSi == Approx(2.5));

    CHECK_FALSE(ParseQuantity("", Dimension::Length));
    CHECK_FALSE(ParseQuantity("abc", Dimension::Length));
    CHECK_FALSE(ParseQuantity("5 kg", Dimension::Length));
    CHECK_FALSE(ParseQuantity("5 parsnips", Dimension::Length));
}

TEST_CASE("Every dimension has a base unit and ascending auto thresholds", "[units]")
{
    for (const auto dimension : {Dimension::Length, Dimension::Mass, Dimension::Time, Dimension::Velocity,
                                 Dimension::AngularRate, Dimension::Temperature})
    {
        INFO(DimensionName(dimension));
        REQUIRE(BaseUnit(dimension) != nullptr);
        double previous = -1.0;
        for (const auto* unit : UnitsOf(dimension))
        {
            if (unit->AutoFrom == kNoAuto) continue;
            CHECK(unit->AutoFrom >= previous);
            previous = unit->AutoFrom;
        }
    }
}

TEST_CASE("Shader defines carry the constants as full precision literals", "[units]")
{
    const auto defines = ShaderDefines();
    REQUIRE(defines.contains("GPP_STEFAN_BOLTZMANN"));
    CHECK(std::stod(defines.at("GPP_STEFAN_BOLTZMANN")) == kStefanBoltzmann);
}
