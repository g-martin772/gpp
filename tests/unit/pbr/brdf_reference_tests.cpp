#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

import GPP;
import glm;
import std;

using namespace GPP::Pbr;
using Catch::Approx;

namespace
{
    const DfgTable& Table()
    {
        static const DfgTable table = BuildDfgTable();
        return table;
    }

    Surface Dielectric(const double roughness, const glm::dvec3& base = glm::dvec3(1.0))
    {
        return {.BaseColor = base, .Metallic = 0.0, .Roughness = roughness};
    }

    Surface Metal(const double roughness, const glm::dvec3& base = glm::dvec3(1.0))
    {
        return {.BaseColor = base, .Metallic = 1.0, .Roughness = roughness};
    }

    glm::dvec3 Direction(const double theta, const double phi)
    {
        return {std::sin(theta) * std::cos(phi), std::sin(theta) * std::sin(phi), std::cos(theta)};
    }

    glm::dvec3 QuadratureAlbedo(const Surface& s, const double nov, const int steps)
    {
        const glm::dvec3 n(0.0, 0.0, 1.0);
        const glm::dvec3 v(std::sqrt(1.0 - nov * nov), 0.0, nov);
        const auto lobes = MakeLobes(s, n, v, Table());
        glm::dvec3 sum(0.0);
        const double dTheta = (kPi / 2.0) / steps;
        const double dPhi = kPi / steps;
        for (int i = 0; i < steps; ++i)
        {
            const double theta = (i + 0.5) * dTheta;
            for (int j = 0; j < steps; ++j)
            {
                const auto l = Direction(theta, (j + 0.5) * dPhi);
                sum += Eval(lobes, n, v, l, Table()) * l.z * std::sin(theta) * dTheta * dPhi;
            }
        }
        return 2.0 * sum;
    }
}

TEST_CASE("DFG table matches direct integration off the grid", "[pbr][brdf]")
{
    for (const double roughness : {0.13, 0.37, 0.61, 0.93})
    {
        for (const double nov : {0.07, 0.33, 0.58, 0.91})
        {
            const auto exact = IntegrateDfg(nov, roughness, 8192);
            const auto table = Table().Lookup(nov, roughness);
            INFO("r=" << roughness << " nov=" << nov);
            CHECK(table.x == Approx(exact.x).margin(4e-3));
            CHECK(table.y == Approx(exact.y).margin(4e-3));
        }
    }
}

TEST_CASE("DFG averages equal the cosine-weighted mean of the table", "[pbr][brdf]")
{
    for (const double roughness : {0.2, 0.5, 1.0})
    {
        double a = 0.0, b = 0.0;
        constexpr int steps = 256;
        for (int i = 0; i < steps; ++i)
        {
            const double mu = (i + 0.5) / steps;
            const auto t = Table().Lookup(mu, roughness);
            a += 2.0 * mu * t.x / steps;
            b += 2.0 * mu * t.y / steps;
        }
        const auto t = Table().Lookup(0.5, roughness);
        CHECK(t.z == Approx(a).margin(4e-3));
        CHECK(t.w == Approx(b).margin(4e-3));
    }
}

TEST_CASE("White furnace: albedo 1 under a white environment", "[pbr][brdf][furnace]")
{
    for (const double roughness : {0.1, 0.2, 0.3, 0.5, 0.7, 0.85, 1.0})
    {
        for (const double nov : {0.1, 0.3, 0.6, 1.0})
        {
            for (const bool metal : {false, true})
            {
                const auto surface = metal ? Metal(roughness) : Dielectric(roughness);
                const auto albedo = DirectionalAlbedo(surface, nov, Table(), 1u << 15);
                INFO("roughness=" << roughness << " nov=" << nov << (metal ? " metal" : " dielectric"));
                CHECK(albedo.x == Approx(1.0).epsilon(0.01));
                CHECK(albedo.y == Approx(1.0).epsilon(0.01));
                CHECK(albedo.z == Approx(1.0).epsilon(0.01));
            }
        }
    }
}

TEST_CASE("Energy is conserved for coloured and tinted materials", "[pbr][brdf]")
{
    const std::array<Surface, 5> surfaces{{
        Dielectric(0.4, {0.8, 0.5, 0.2}),
        Metal(0.4, {1.0, 0.766, 0.336}),
        Metal(0.9, {0.95, 0.64, 0.54}),
        {.BaseColor = {0.9, 0.9, 0.9}, .Metallic = 0.5, .Roughness = 0.6, .Ior = 2.4},
        {.BaseColor = {0.3, 0.6, 0.9}, .Metallic = 0.0, .Roughness = 0.25, .SpecularColor = {1.0, 0.5, 0.2},
         .SpecularFactor = 0.8},
    }};
    for (const auto& surface : surfaces)
    {
        for (const double nov : {0.05, 0.25, 0.5, 0.8, 1.0})
        {
            const auto albedo = DirectionalAlbedo(surface, nov, Table(), 1u << 14);
            CHECK(albedo.x <= 1.01);
            CHECK(albedo.y <= 1.01);
            CHECK(albedo.z <= 1.01);
            CHECK(albedo.x >= 0.0);
        }
    }
}

TEST_CASE("BSDF is reciprocal", "[pbr][brdf]")
{
    const std::array<Surface, 4> surfaces{{
        Dielectric(0.3, {0.8, 0.4, 0.2}),
        Metal(0.6, {0.9, 0.7, 0.3}),
        Dielectric(0.9),
        {.BaseColor = {0.5, 0.5, 0.9}, .Metallic = 0.4, .Roughness = 0.15, .Ior = 1.8},
    }};
    const glm::dvec3 n = glm::normalize(glm::dvec3(0.2, -0.3, 0.9));
    std::uint32_t state = 1234567u;
    const auto next = [&]
    {
        state = state * 1664525u + 1013904223u;
        return static_cast<double>(state >> 8) / static_cast<double>(1u << 24);
    };
    for (const auto& surface : surfaces)
    {
        for (int i = 0; i < 200; ++i)
        {
            const glm::dvec3 v = Direction(std::acos(next() * 0.98 + 0.02), next() * 2.0 * kPi);
            const glm::dvec3 l = Direction(std::acos(next() * 0.98 + 0.02), next() * 2.0 * kPi);
            glm::dvec3 t, bt;
            Basis(n, t, bt);
            const glm::dvec3 vw = t * v.x + bt * v.y + n * v.z;
            const glm::dvec3 lw = t * l.x + bt * l.y + n * l.z;
            const auto forward = Eval(MakeLobes(surface, n, vw, Table()), n, vw, lw, Table());
            const auto backward = Eval(MakeLobes(surface, n, lw, Table()), n, lw, vw, Table());
            for (int c = 0; c < 3; ++c)
            {
                CHECK(forward[c] == Approx(backward[c]).epsilon(1e-9).margin(1e-12));
            }
        }
    }
}

TEST_CASE("Sampling routine agrees with quadrature of the BSDF", "[pbr][brdf][montecarlo]")
{
    for (const double roughness : {0.35, 0.6, 1.0})
    {
        for (const double nov : {0.25, 0.7})
        {
            for (const bool metal : {false, true})
            {
                const auto surface = metal ? Metal(roughness, {0.9, 0.6, 0.3}) : Dielectric(roughness, {0.7, 0.5, 0.3});
                const auto reference = QuadratureAlbedo(surface, nov, 360);
                const auto sampled = DirectionalAlbedo(surface, nov, Table(), 1u << 16);
                INFO("roughness=" << roughness << " nov=" << nov << (metal ? " metal" : " dielectric"));
                for (int c = 0; c < 3; ++c) CHECK(sampled[c] == Approx(reference[c]).epsilon(0.01));
            }
        }
    }
}

TEST_CASE("Sampling pdf integrates to the valid-sample fraction and matches histogram", "[pbr][brdf][montecarlo]")
{
    const glm::dvec3 n(0.0, 0.0, 1.0);
    for (const double roughness : {0.4, 0.8})
    {
        for (const double nov : {0.3, 0.9})
        {
            const glm::dvec3 v(std::sqrt(1.0 - nov * nov), 0.0, nov);
            const auto lobes = MakeLobes(Dielectric(roughness, {0.6, 0.6, 0.6}), n, v, Table());

            constexpr int steps = 500;
            double integral = 0.0;
            const double dTheta = (kPi / 2.0) / steps;
            const double dPhi = 2.0 * kPi / steps;
            for (int i = 0; i < steps; ++i)
            {
                const double theta = (i + 0.5) * dTheta;
                for (int j = 0; j < steps; ++j)
                {
                    integral += Pdf(lobes, n, v, Direction(theta, (j + 0.5) * dPhi)) * std::sin(theta) * dTheta * dPhi;
                }
            }

            constexpr std::uint32_t samples = 1u << 16;
            std::uint32_t valid = 0;
            for (std::uint32_t i = 0; i < samples; ++i)
            {
                const auto a = Hammersley(i, samples);
                const auto s = SampleBsdf(lobes, n, v, {a.x, a.y, std::fmod(i * 0.6180339887498949 + 0.1, 1.0)}, Table());
                if (s.Pdf > 0.0) ++valid;
            }
            INFO("roughness=" << roughness << " nov=" << nov);
            CHECK(integral == Approx(static_cast<double>(valid) / samples).epsilon(0.01));
            CHECK(integral <= 1.0 + 1e-3);
            CHECK(integral > 0.9);
        }
    }
}

TEST_CASE("Specular colour, ior and factor drive F0 and F90", "[pbr][brdf]")
{
    CHECK(F0FromIor(1.5) == Approx(0.04));
    CHECK(F0FromIor(1.0) == Approx(0.0));
    CHECK(F0FromIor(2.0) == Approx(1.0 / 9.0));

    const glm::dvec3 n(0.0, 0.0, 1.0);
    const glm::dvec3 v(0.0, 0.0, 1.0);
    const auto base = MakeLobes(Dielectric(0.5), n, v, Table());
    CHECK(base.F0.x == Approx(0.04));
    CHECK(base.F90.x == Approx(1.0));

    const auto tinted = MakeLobes({.Roughness = 0.5, .Ior = 1.5, .SpecularColor = {1.0, 0.5, 0.0}, .SpecularFactor = 0.5},
                                  n, v, Table());
    CHECK(tinted.F0.x == Approx(0.02));
    CHECK(tinted.F0.y == Approx(0.01));
    CHECK(tinted.F0.z == Approx(0.0));
    CHECK(tinted.F90.x == Approx(0.5));

    const auto gold = MakeLobes(Metal(0.3, {1.0, 0.77, 0.34}), n, v, Table());
    CHECK(gold.F0.y == Approx(0.77));
    CHECK(gold.F90.y == Approx(1.0));
    CHECK(glm::length(gold.Diffuse) == Approx(0.0));
}

TEST_CASE("Hammersley sequence is deterministic and well spread", "[pbr]")
{
    CHECK(Hammersley(0, 16).x == Approx(1.0 / 32.0));
    CHECK(Hammersley(1, 16).y == Approx(0.5));
    CHECK(Hammersley(2, 16).y == Approx(0.25));
    double mean = 0.0;
    for (std::uint32_t i = 0; i < 1024; ++i) mean += Hammersley(i, 1024).y;
    CHECK(mean / 1024.0 == Approx(0.5).epsilon(1e-3));
}
