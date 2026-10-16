module;
#include <version>
export module GPP.Core:Units;

import std;

// Sources: SI 2019, CODATA 2022, IAU 2012 B2 and 2015 B3 nominal values, GRAVITY 2023 (Sgr A*), EHT 2019 (M87*).

#ifdef __STDCPP_FLOAT128_T__
#define GPP_WIDE_LITERAL(x) x##f128
#else
#define GPP_WIDE_LITERAL(x) x##L
#endif

export namespace GPP::Units
{
#ifdef __STDCPP_FLOAT128_T__
    using Wide = std::float128_t;
    inline constexpr bool kHasFloat128 = true;
#else
    using Wide = long double;
    inline constexpr bool kHasFloat128 = false;
#endif

    enum class Precision : std::uint8_t { F32, F64, F128 };

    inline constexpr std::array<Precision, 3> kPrecisions{Precision::F32, Precision::F64, Precision::F128};

    [[nodiscard]] constexpr std::string_view PrecisionName(const Precision precision)
    {
        switch (precision)
        {
        case Precision::F32: return "f32";
        case Precision::F64: return "f64";
        case Precision::F128: return "f128";
        }
        return "f64";
    }

    [[nodiscard]] constexpr bool CpuSupports(const Precision precision)
    {
        return precision != Precision::F128 || kHasFloat128;
    }

    [[nodiscard]] constexpr std::optional<Precision> PrecisionFromName(const std::string_view name)
    {
        for (const auto precision : kPrecisions)
        {
            if (PrecisionName(precision) == name) return precision;
        }
        return std::nullopt;
    }

    [[nodiscard]] constexpr Wide Pi() { return GPP_WIDE_LITERAL(3.14159265358979323846264338327950288); }

#define GPP_UNITS_DEFINE(Name, Expr) \
    inline constexpr Wide k##Name##W = Expr; \
    inline constexpr double k##Name = static_cast<double>(k##Name##W);

    // ---- SI 2019 defining constants ----
    GPP_UNITS_DEFINE(SpeedOfLight, GPP_WIDE_LITERAL(299792458.0))             // m/s
    GPP_UNITS_DEFINE(PlanckConstant, GPP_WIDE_LITERAL(6.62607015e-34))        // J s
    GPP_UNITS_DEFINE(BoltzmannConstant, GPP_WIDE_LITERAL(1.380649e-23))       // J/K
    GPP_UNITS_DEFINE(ElementaryCharge, GPP_WIDE_LITERAL(1.602176634e-19))     // C
    GPP_UNITS_DEFINE(AvogadroConstant, GPP_WIDE_LITERAL(6.02214076e23))       // 1/mol

    // ---- CODATA 2022 ----
    GPP_UNITS_DEFINE(GravitationalConstant, GPP_WIDE_LITERAL(6.67430e-11))    // m^3 kg^-1 s^-2
    GPP_UNITS_DEFINE(VacuumPermittivity, GPP_WIDE_LITERAL(8.8541878188e-12))  // F/m
    GPP_UNITS_DEFINE(VacuumPermeability, GPP_WIDE_LITERAL(1.25663706127e-6))  // N/A^2
    GPP_UNITS_DEFINE(ProtonMass, GPP_WIDE_LITERAL(1.67262192595e-27))         // kg
    GPP_UNITS_DEFINE(ElectronMass, GPP_WIDE_LITERAL(9.1093837139e-31))        // kg
    GPP_UNITS_DEFINE(ThomsonCrossSection, GPP_WIDE_LITERAL(6.6524587051e-29)) // m^2

    // ---- Derived ----
    // sigma_SB = 2 pi^5 k^4 / (15 h^3 c^2)
    GPP_UNITS_DEFINE(StefanBoltzmann,
                     2 * Pi() * Pi() * Pi() * Pi() * Pi() * kBoltzmannConstantW * kBoltzmannConstantW
                         * kBoltzmannConstantW * kBoltzmannConstantW
                         / (15 * kPlanckConstantW * kPlanckConstantW * kPlanckConstantW * kSpeedOfLightW
                            * kSpeedOfLightW))                                 // W m^-2 K^-4
    GPP_UNITS_DEFINE(RadiationConstant, 4 * kStefanBoltzmannW / kSpeedOfLightW) // J m^-3 K^-4

    // ---- Time and length definitions ----
    GPP_UNITS_DEFINE(Day, GPP_WIDE_LITERAL(86400.0))                          // s
    GPP_UNITS_DEFINE(JulianYear, GPP_WIDE_LITERAL(31557600.0))                // s (365.25 d)
    GPP_UNITS_DEFINE(AstronomicalUnit, GPP_WIDE_LITERAL(149597870700.0))      // m, IAU 2012 B2
    GPP_UNITS_DEFINE(Parsec, 648000 / Pi() * kAstronomicalUnitW)              // m, IAU 2015 B2
    GPP_UNITS_DEFINE(LightYear, kSpeedOfLightW * kJulianYearW)                // m

    // ---- IAU 2015 B3 nominal values ----
    GPP_UNITS_DEFINE(SolarMassParameter, GPP_WIDE_LITERAL(1.3271244e20))      // GM_sun, m^3/s^2
    GPP_UNITS_DEFINE(EarthMassParameter, GPP_WIDE_LITERAL(3.986004e14))       // GM_earth
    GPP_UNITS_DEFINE(JupiterMassParameter, GPP_WIDE_LITERAL(1.2668653e17))    // GM_jupiter
    GPP_UNITS_DEFINE(SolarRadius, GPP_WIDE_LITERAL(6.957e8))                  // m
    GPP_UNITS_DEFINE(EarthEquatorialRadius, GPP_WIDE_LITERAL(6.3781e6))       // m
    GPP_UNITS_DEFINE(JupiterEquatorialRadius, GPP_WIDE_LITERAL(7.1492e7))     // m
    GPP_UNITS_DEFINE(SolarLuminosity, GPP_WIDE_LITERAL(3.828e26))             // W
    GPP_UNITS_DEFINE(SolarEffectiveTemperature, GPP_WIDE_LITERAL(5772.0))     // K

    // ---- Masses ----
    GPP_UNITS_DEFINE(SolarMass, kSolarMassParameterW / kGravitationalConstantW)
    GPP_UNITS_DEFINE(EarthMass, kEarthMassParameterW / kGravitationalConstantW)
    GPP_UNITS_DEFINE(JupiterMass, kJupiterMassParameterW / kGravitationalConstantW)

    // ---- Reference bhs ----
    GPP_UNITS_DEFINE(SagittariusAStarMass, GPP_WIDE_LITERAL(4.297e6) * kSolarMassW)  // GRAVITY 2023
    GPP_UNITS_DEFINE(M87StarMass, GPP_WIDE_LITERAL(6.5e9) * kSolarMassW)             // EHT 2019

    // ---- Geometrized-unit scales ----
    GPP_UNITS_DEFINE(SolarGravitationalRadius,
                     kSolarMassParameterW / (kSpeedOfLightW * kSpeedOfLightW))              // GM/c^2 [m]
    GPP_UNITS_DEFINE(SolarGeometricTime,
                     kSolarMassParameterW / (kSpeedOfLightW * kSpeedOfLightW * kSpeedOfLightW)) // GM/c^3 [s]

#undef GPP_UNITS_DEFINE

    // old: 1 length unit = GM_sun/c^2, bh in solar mass, etc
    namespace Legacy
    {
        inline constexpr double kLengthM = kSolarGravitationalRadius;
        inline constexpr double kBlackHoleMassKg = kSolarMass;
        inline constexpr double kGravityStrength = 99.45;
        // Physical seconds per legacy sim second: tau = (GM_sun/c^3) sqrt(99.45).
        inline const double kTimeS = kSolarGeometricTime * std::sqrt(kGravityStrength);
    }

    template <typename Real>
    [[nodiscard]] constexpr Real Narrow(const Wide value)
    {
        return static_cast<Real>(value);
    }

    // ---- Geometrized quantities, SI mass in kg ----

    // r_g = GM/c^2
    template <typename Real = double>
    [[nodiscard]] constexpr Real GravitationalRadius(const Real massKg)
    {
        return Narrow<Real>(kGravitationalConstantW / (kSpeedOfLightW * kSpeedOfLightW)) * massKg;
    }

    // r_s = 2 GM/c^2
    template <typename Real = double>
    [[nodiscard]] constexpr Real SchwarzschildRadius(const Real massKg)
    {
        return Real(2) * GravitationalRadius(massKg);
    }

    // GM/c^3
    template <typename Real = double>
    [[nodiscard]] constexpr Real GeometricTime(const Real massKg)
    {
        return Narrow<Real>(kGravitationalConstantW / (kSpeedOfLightW * kSpeedOfLightW * kSpeedOfLightW)) * massKg;
    }

    // Outer/inner horizon radii in units of r_g: 1 +- sqrt(1 - a*^2 - q*^2); NaN for no horizon
    template <typename Real = double>
    [[nodiscard]] Real HorizonRadiusInGravitationalRadii(const Real spinStar, const Real chargeStar,
                                                         const bool outer = true)
    {
        const Real discriminant = Real(1) - spinStar * spinStar - chargeStar * chargeStar;
        if (discriminant < Real(0)) return std::numeric_limits<Real>::quiet_NaN();
        const Real root = std::sqrt(discriminant);
        return outer ? Real(1) + root : Real(1) - root;
    }

    // Prograde (or retrograde) ISCO of Kerr in units of r_g (Bardeen, Press & Teukolsky 1972, eq. 2.21).
    template <typename Real = double>
    [[nodiscard]] Real IscoInGravitationalRadii(const Real spinStar, const bool prograde = true)
    {
        const Real a = spinStar;
        const Real a2 = a * a;
        const Real z1 = Real(1) + std::cbrt(Real(1) - a2) * (std::cbrt(Real(1) + a) + std::cbrt(Real(1) - a));
        const Real z2 = std::sqrt(Real(3) * a2 + z1 * z1);
        const Real root = std::sqrt((Real(3) - z1) * (Real(3) + z1 + Real(2) * z2));
        return prograde ? Real(3) + z2 - root : Real(3) + z2 + root;
    }

    // L_Edd = 4 pi G M m_p c / sigma_T  [W]
    template <typename Real = double>
    [[nodiscard]] constexpr Real EddingtonLuminosity(const Real massKg)
    {
        const Wide factor = 4 * Pi() * kGravitationalConstantW * kProtonMassW * kSpeedOfLightW / kThomsonCrossSectionW;
        return Narrow<Real>(factor) * massKg;
    }

    // Keplerian orbital period (Newtonian, circular): 2 pi sqrt(r^3 / GM)
    template <typename Real = double>
    [[nodiscard]] Real KeplerPeriod(const Real radiusM, const Real massKg)
    {
        const Real mu = Narrow<Real>(kGravitationalConstantW) * massKg;
        return Real(2) * Narrow<Real>(Pi()) * std::sqrt(radiusM * radiusM * radiusM / mu);
    }

    // ---- Display units ----

    enum class Dimension : std::uint8_t
    {
        None, Length, Mass, Time, Velocity, AngularRate, Temperature
    };

    [[nodiscard]] constexpr std::string_view DimensionName(const Dimension dimension)
    {
        switch (dimension)
        {
        case Dimension::None: return "dimensionless";
        case Dimension::Length: return "length";
        case Dimension::Mass: return "mass";
        case Dimension::Time: return "time";
        case Dimension::Velocity: return "velocity";
        case Dimension::AngularRate: return "angular rate";
        case Dimension::Temperature: return "temperature";
        }
        return "dimensionless";
    }

    inline constexpr double kNoAuto = std::numeric_limits<double>::infinity();

    struct UnitDef
    {
        std::string_view Symbol;
        std::string_view Name;
        Dimension Dim;
        double Scale;
        double AutoFrom;
        std::array<std::string_view, 4> Aliases{};
        bool SiPrefixable = false;
    };

    [[nodiscard]] inline const std::vector<UnitDef>& AllUnits()
    {
        static const std::vector<UnitDef> units = []
        {
            const double au = kAstronomicalUnit;
            const double pc = kParsec;
            const double ly = kLightYear;
            const double year = kJulianYear;
            const double sun = kSolarMass;
            const double c = kSpeedOfLight;
            const double kpi = static_cast<double>(Pi());
            const Dimension L = Dimension::Length;
            const Dimension M = Dimension::Mass;
            const Dimension T = Dimension::Time;
            const Dimension V = Dimension::Velocity;
            const Dimension W = Dimension::AngularRate;
            return std::vector<UnitDef>{
                {"nm", "nanometre", L, 1e-9, 0.0, {}, false},
                {"\xC2\xB5m", "micrometre", L, 1e-6, 1e-6, {"um", "micron"}, false},
                {"mm", "millimetre", L, 1e-3, 1e-3, {}, false},
                {"cm", "centimetre", L, 1e-2, kNoAuto, {}, false},
                {"m", "metre", L, 1.0, 1e-2, {"meter", "metres", "meters"}, true},
                {"km", "kilometre", L, 1e3, 1e3, {"kilometer"}, false},
                {"R\xE2\x98\x89", "solar radius", L, static_cast<double>(kSolarRadius), kNoAuto,
                 {"Rsun", "Rsol", "R_sun"}, false},
                {"AU", "astronomical unit", L, au, 0.01 * au, {"au", "ua"}, false},
                {"ly", "light year", L, ly, 0.1 * ly, {"Lj", "lj", "lightyear", "lightyears"}, false},
                {"pc", "parsec", L, pc, pc, {"parsec"}, false},
                {"kpc", "kiloparsec", L, 1e3 * pc, 100.0 * pc, {}, false},
                {"Mpc", "megaparsec", L, 1e6 * pc, 1e5 * pc, {"mpc"}, false},
                {"Gpc", "gigaparsec", L, 1e9 * pc, 1e8 * pc, {"gpc"}, false},

                {"g", "gram", M, 1e-3, kNoAuto, {}, false},
                {"kg", "kilogram", M, 1.0, 0.0, {"kilogram"}, false},
                {"t", "tonne", M, 1e3, kNoAuto, {"tonne"}, false},
                {"M\xE2\x8A\x95", "earth mass", M, static_cast<double>(kEarthMass), kNoAuto,
                 {"Mearth", "Me", "M_earth"}, false},
                {"MJ", "jupiter mass", M, static_cast<double>(kJupiterMass), kNoAuto, {"Mjup", "M_jup"}, false},
                {"M\xE2\x98\x89", "solar mass", M, sun, 0.1 * sun, {"Msun", "Msol", "M_sun", "Ms"}, false},
                {"Sgr A*", "Sagittarius A* mass", M, static_cast<double>(kSagittariusAStarMass),
                 0.5 * static_cast<double>(kSagittariusAStarMass), {"SgrA*", "SgrA", "Sgr A"}, false},
                {"M87*", "M87* mass", M, static_cast<double>(kM87StarMass),
                 0.5 * static_cast<double>(kM87StarMass), {"M87"}, false},

                {"ns", "nanosecond", T, 1e-9, 0.0, {}, false},
                {"\xC2\xB5s", "microsecond", T, 1e-6, 1e-6, {"us"}, false},
                {"ms", "millisecond", T, 1e-3, 1e-3, {}, false},
                {"s", "second", T, 1.0, 1.0, {"sec", "second", "seconds"}, true},
                {"min", "minute", T, 60.0, 120.0, {"minute", "minutes"}, false},
                {"h", "hour", T, 3600.0, 7200.0, {"hr", "hour", "hours"}, false},
                {"d", "day", T, static_cast<double>(kDay), 2.0 * kDay, {"day", "days"}, false},
                {"yr", "julian year", T, year, year, {"a", "year", "years", "y"}, false},
                {"kyr", "kiloyear", T, 1e3 * year, 1e3 * year, {"ka"}, false},
                {"Myr", "megayear", T, 1e6 * year, 1e6 * year, {"Ma"}, false},
                {"Gyr", "gigayear", T, 1e9 * year, 1e9 * year, {"Ga"}, false},

                {"m/s", "metre per second", V, 1.0, 0.0, {}, false},
                {"km/s", "kilometre per second", V, 1e3, 1e3, {"kms"}, false},
                {"c", "speed of light", V, c, 0.01 * c, {}, false},

                {"rad/s", "radian per second", W, 1.0, 0.0, {"rads"}, false},
                {"\xC2\xB0/s", "degree per second", W, kpi / 180.0, kNoAuto, {"deg/s", "dps"}, false},
                {"rpm", "revolution per minute", W, 2.0 * kpi / 60.0, kNoAuto, {}, false},

                {"K", "kelvin", Dimension::Temperature, 1.0, 0.0, {"kelvin"}, false},
            };
        }();
        return units;
    }

    [[nodiscard]] inline std::vector<const UnitDef*> UnitsOf(const Dimension dimension)
    {
        std::vector<const UnitDef*> result;
        for (const auto& unit : AllUnits())
        {
            if (unit.Dim == dimension) result.push_back(&unit);
        }
        return result;
    }

    [[nodiscard]] inline const UnitDef* BaseUnit(const Dimension dimension)
    {
        for (const auto& unit : AllUnits())
        {
            if (unit.Dim == dimension && unit.Scale == 1.0 && unit.AutoFrom != kNoAuto) return &unit;
        }
        return nullptr;
    }

    [[nodiscard]] inline const UnitDef* PickDisplayUnit(const Dimension dimension, const double valueSi)
    {
        const UnitDef* base = BaseUnit(dimension);
        if (!std::isfinite(valueSi) || valueSi == 0.0) return base;
        const double magnitude = std::fabs(valueSi);
        const UnitDef* chosen = nullptr;
        for (const auto& unit : AllUnits())
        {
            if (unit.Dim != dimension || unit.AutoFrom == kNoAuto) continue;
            if (unit.AutoFrom <= magnitude) chosen = &unit;
        }
        if (!chosen)
        {
            for (const auto& unit : AllUnits())
            {
                if (unit.Dim == dimension && unit.AutoFrom != kNoAuto) return &unit;
            }
        }
        return chosen ? chosen : base;
    }

    [[nodiscard]] constexpr double ToSi(const double displayValue, const UnitDef& unit) { return displayValue * unit.Scale; }
    [[nodiscard]] constexpr double FromSi(const double siValue, const UnitDef& unit) { return siValue / unit.Scale; }

    namespace Detail
    {
        [[nodiscard]] inline bool EqualsInsensitive(const std::string_view a, const std::string_view b)
        {
            return std::ranges::equal(a, b, [](const char x, const char y)
            {
                return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
            });
        }

        [[nodiscard]] inline bool Matches(const UnitDef& unit, const std::string_view text, const bool exact)
        {
            const auto same = [&](const std::string_view candidate)
            {
                return exact ? candidate == text : EqualsInsensitive(candidate, text);
            };
            if (same(unit.Symbol)) return true;
            return std::ranges::any_of(unit.Aliases, [&](const std::string_view alias) { return !alias.empty() && same(alias); });
        }

        struct Prefix
        {
            std::string_view Text;
            double Factor;
        };

        inline constexpr std::array<Prefix, 8> kPrefixes{{
            {"n", 1e-9}, {"\xC2\xB5", 1e-6}, {"u", 1e-6}, {"m", 1e-3}, {"k", 1e3}, {"M", 1e6}, {"G", 1e9}, {"T", 1e12}}};
    }

    struct ResolvedUnit
    {
        const UnitDef* Unit = nullptr;
        double ExtraFactor = 1.0;
    };

    // case-sensitive first, then case-insensitive, then SI prefix + prefixable base unit ("mm", "Gm").
    [[nodiscard]] inline std::optional<ResolvedUnit> FindUnit(const Dimension dimension, const std::string_view text)
    {
        for (const bool exact : {true, false})
        {
            for (const auto& unit : AllUnits())
            {
                if (unit.Dim == dimension && Detail::Matches(unit, text, exact)) return ResolvedUnit{&unit, 1.0};
            }
        }
        for (const auto& prefix : Detail::kPrefixes)
        {
            if (!text.starts_with(prefix.Text) || text.size() <= prefix.Text.size()) continue;
            const auto rest = text.substr(prefix.Text.size());
            for (const auto& unit : AllUnits())
            {
                if (unit.Dim == dimension && unit.SiPrefixable && Detail::Matches(unit, rest, true))
                {
                    return ResolvedUnit{&unit, prefix.Factor};
                }
            }
        }
        return std::nullopt;
    }

    struct Quantity
    {
        double ValueSi = 0.0;
        const UnitDef* Unit = nullptr;
    };

    [[nodiscard]] inline std::optional<Quantity> ParseQuantity(std::string_view text, const Dimension dimension,
                                                               const UnitDef* defaultUnit = nullptr)
    {
        const auto trim = [](std::string_view s)
        {
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
            return s;
        };
        text = trim(text);
        if (text.starts_with('+')) text.remove_prefix(1);
        if (text.empty()) return std::nullopt;

        double number = 0.0;
        const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), number);
        if (ec != std::errc{} || !std::isfinite(number)) return std::nullopt;
        const auto unitText = trim(text.substr(static_cast<std::size_t>(end - text.data())));

        if (unitText.empty())
        {
            const UnitDef* unit = defaultUnit ? defaultUnit : BaseUnit(dimension);
            return Quantity{defaultUnit ? ToSi(number, *defaultUnit) : number, unit};
        }
        const auto resolved = FindUnit(dimension, unitText);
        if (!resolved) return std::nullopt;
        return Quantity{ToSi(number, *resolved->Unit) * resolved->ExtraFactor, resolved->Unit};
    }

    [[nodiscard]] inline std::string FormatNumber(const double value, const int significantDigits = 5)
    {
        if (!std::isfinite(value)) return std::isnan(value) ? "nan" : (value > 0 ? "inf" : "-inf");
        return std::format("{:.{}g}", value, std::clamp(significantDigits, 1, 17));
    }

    [[nodiscard]] inline std::string FormatIn(const double valueSi, const UnitDef& unit, const int significantDigits = 5)
    {
        return FormatNumber(FromSi(valueSi, unit), significantDigits) + " " + std::string(unit.Symbol);
    }

    [[nodiscard]] inline std::string FormatQuantity(const double valueSi, const Dimension dimension,
                                                    const int significantDigits = 5)
    {
        const UnitDef* unit = PickDisplayUnit(dimension, valueSi);
        return unit ? FormatIn(valueSi, *unit, significantDigits) : FormatNumber(valueSi, significantDigits);
    }

    [[nodiscard]] inline std::unordered_map<std::string, std::string> ShaderDefines()
    {
        const auto literal = [](const double value) { return std::format("{:.17g}", value); };
        return {
            {"GPP_STEFAN_BOLTZMANN", literal(kStefanBoltzmann)},
            {"GPP_SOLAR_MASS_KG", literal(kSolarMass)},
            {"GPP_SOLAR_GRAVITATIONAL_RADIUS_M", literal(kSolarGravitationalRadius)},
            {"GPP_SPEED_OF_LIGHT", literal(kSpeedOfLight)},
        };
    }
}

#undef GPP_WIDE_LITERAL
