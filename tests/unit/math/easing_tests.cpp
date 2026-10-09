#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

import GPP;
import std;

using namespace GPP;
using Catch::Approx;

TEST_CASE("Every easing curve maps 0 to 0 and 1 to 1 and stays monotonic", "[math][easing]")
{
    for (const auto kind : kEaseKinds)
    {
        INFO(EaseName(kind));
        CHECK(Ease(kind, 0.0f) == Approx(0.0f));
        CHECK(Ease(kind, 1.0f) == Approx(1.0f));
        float previous = 0.0f;
        for (int i = 1; i <= 100; ++i)
        {
            const float value = Ease(kind, static_cast<float>(i) / 100.0f);
            CHECK(value >= previous - 1.0e-6f);
            previous = value;
        }
    }
}

TEST_CASE("Easing curves have their characteristic midpoints", "[math][easing]")
{
    CHECK(Ease(EaseKind::Linear, 0.25f) == Approx(0.25f));
    CHECK(Ease(EaseKind::EaseIn, 0.5f) == Approx(0.25f));
    CHECK(Ease(EaseKind::EaseOut, 0.5f) == Approx(0.75f));
    CHECK(Ease(EaseKind::EaseInOut, 0.5f) == Approx(0.5f));
    CHECK(Ease(EaseKind::CubicIn, 0.5f) == Approx(0.125f));
    CHECK(Ease(EaseKind::CubicOut, 0.5f) == Approx(0.875f));
    CHECK(Ease(EaseKind::CubicInOut, 0.5f) == Approx(0.5f));
    CHECK(Ease(EaseKind::Smoothstep, 0.5f) == Approx(0.5f));
    CHECK(Ease(EaseKind::Smoothstep, 0.25f) == Approx(0.15625f));
}

TEST_CASE("Easing clamps its input and names round-trip", "[math][easing]")
{
    CHECK(Ease(EaseKind::EaseIn, -3.0f) == 0.0f);
    CHECK(Ease(EaseKind::EaseOut, 7.0f) == 1.0f);
    for (const auto kind : kEaseKinds) CHECK(EaseFromName(EaseName(kind)) == kind);
    CHECK_FALSE(EaseFromName("bounce").has_value());
}

TEST_CASE("Smoothing is frame-rate independent", "[math][easing]")
{
    float coarse = 0.0f;
    for (int i = 0; i < 10; ++i) coarse += (1.0f - coarse) * SmoothingFactor(0.5f, 0.1f);
    float fine = 0.0f;
    for (int i = 0; i < 100; ++i) fine += (1.0f - fine) * SmoothingFactor(0.5f, 0.01f);
    CHECK(coarse == Approx(fine).margin(1.0e-4));
    CHECK(coarse == Approx(0.75f).margin(1.0e-4));
    CHECK(SmoothingFactor(0.0f, 0.016f) == 1.0f);
}

TEST_CASE("Look-at rotation faces the target", "[math][orientation]")
{
    const glm::vec3 eye(0.0f, 0.0f, 10.0f);
    const glm::vec3 target(0.0f, 0.0f, 0.0f);
    const glm::vec3 forward = LookAtRotation(eye, target) * glm::vec3(0.0f, 0.0f, -1.0f);
    CHECK(forward.z == Approx(-1.0f));
    const glm::vec3 up = LookAtRotation(glm::vec3(0.0f, 5.0f, 0.0f), target) * glm::vec3(0.0f, 0.0f, -1.0f);
    CHECK(up.y == Approx(-1.0f));
}
