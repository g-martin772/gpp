#include <catch2/catch_test_macros.hpp>

import GPP;
import std;

using namespace GPP;

namespace
{
    struct CountingTheme final : public Theme
    {
        using Dependencies = std::tuple<Logger>;

        explicit CountingTheme(const std::shared_ptr<Logger>& logger)
            : Theme(logger)
        {
        }

        void Apply(ImGuiStyle&, ImGuiIO&) override { ++ApplyCount; }

        int ApplyCount = 0;
    };
}

TEST_CASE("ThemeProxy has no active theme until one is set", "[theme][proxy]")
{
    auto logger = std::make_shared<Logger>();
    ThemeProxy proxy(logger);

    CHECK(proxy.GetActive() == nullptr);
}

TEST_CASE("ThemeProxy swaps the active theme and returns the previous one", "[theme][proxy]")
{
    auto logger = std::make_shared<Logger>();
    ThemeProxy proxy(logger);
    CountingTheme themeA(logger);
    CountingTheme themeB(logger);

    auto* previous = proxy.SwapActive(&themeA);
    CHECK(previous == nullptr);
    CHECK(proxy.GetActive() == &themeA);

    previous = proxy.SwapActive(&themeB);
    CHECK(previous == &themeA);
    CHECK(proxy.GetActive() == &themeB);
}

TEST_CASE("Theme::Apply is invoked with the owning proxy's active instance", "[theme][proxy]")
{
    auto logger = std::make_shared<Logger>();
    ThemeProxy proxy(logger);
    CountingTheme theme(logger);
    proxy.SwapActive(&theme);

    ImGuiStyle style{};
    ImGuiIO io{};
    proxy.GetActive()->Apply(style, io);

    CHECK(theme.ApplyCount == 1);
}
