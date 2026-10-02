#include <catch2/catch_test_macros.hpp>

import GPP;
import std;

using namespace GPP;

namespace
{
    struct CountingHotReloadLayer final : public HotReloadableLayer
    {
        using Dependencies = std::tuple<Logger>;

        explicit CountingHotReloadLayer(const std::shared_ptr<Logger>& logger)
            : HotReloadableLayer(logger)
        {
        }

        void OnAttach() override { ++AttachCount; }
        void OnDetach() override { ++DetachCount; }
        void OnUpdate(float) override { ++UpdateCount; }
        void OnRender() override { ++RenderCount; }
        void OnUiRender() override { ++UiRenderCount; }
        void OnEvent() override { ++EventCount; }

        int AttachCount = 0;
        int DetachCount = 0;
        int UpdateCount = 0;
        int RenderCount = 0;
        int UiRenderCount = 0;
        int EventCount = 0;
    };
}

TEST_CASE("HotReloadLayerProxy defers OnAttach until the proxy itself is attached",
          "[hotreload][layer][proxy]")
{
    auto logger = std::make_shared<Logger>();
    HotReloadLayerProxy proxy(logger);
    CountingHotReloadLayer layer(logger);

    CHECK(proxy.GetActive() == nullptr);

    const auto* previous = proxy.SwapActive(&layer);
    CHECK(previous == nullptr);
    CHECK(proxy.GetActive() == &layer);
    CHECK(layer.AttachCount == 0); // the proxy isn't attached to a render target yet

    proxy.OnAttach();
    CHECK(layer.AttachCount == 1);
}

TEST_CASE("HotReloadLayerProxy forwards calls to the active instance", "[hotreload][layer][proxy]")
{
    auto logger = std::make_shared<Logger>();
    HotReloadLayerProxy proxy(logger);
    CountingHotReloadLayer layer(logger);
    proxy.SwapActive(&layer);
    proxy.OnAttach();

    proxy.OnUpdate(0.016f);
    proxy.OnRender();
    proxy.OnUiRender();
    proxy.OnEvent();

    CHECK(layer.UpdateCount == 1);
    CHECK(layer.RenderCount == 1);
    CHECK(layer.UiRenderCount == 1);
    CHECK(layer.EventCount == 1);
}

TEST_CASE("HotReloadLayerProxy detaches the outgoing and attaches the incoming instance on a live swap",
          "[hotreload][layer][proxy]")
{
    auto logger = std::make_shared<Logger>();
    HotReloadLayerProxy proxy(logger);
    CountingHotReloadLayer layerA(logger);
    CountingHotReloadLayer layerB(logger);

    proxy.SwapActive(&layerA);
    proxy.OnAttach();
    CHECK(layerA.AttachCount == 1);

    auto* previous = proxy.SwapActive(&layerB);
    CHECK(previous == &layerA);
    CHECK(layerA.DetachCount == 1);
    CHECK(layerB.AttachCount == 1);
    CHECK(layerB.DetachCount == 0);
    CHECK(proxy.GetActive() == &layerB);

    proxy.OnDetach();
    CHECK(layerB.DetachCount == 1);
    CHECK(layerA.DetachCount == 1); // unaffected by the proxy's later, unrelated detach
}

TEST_CASE("HotReloadLayerProxy does not call OnAttach/OnDetach on a swap while detached",
          "[hotreload][layer][proxy]")
{
    auto logger = std::make_shared<Logger>();
    HotReloadLayerProxy proxy(logger);
    CountingHotReloadLayer layerA(logger);
    CountingHotReloadLayer layerB(logger);

    // Never attached: e.g. the manager staged a replacement before the owning GuiLayerStack was ever
    // attached to a render target (the common case for the very first load at application start-up).
    proxy.SwapActive(&layerA);
    proxy.SwapActive(&layerB);

    CHECK(layerA.AttachCount == 0);
    CHECK(layerA.DetachCount == 0);
    CHECK(layerB.AttachCount == 0);
    CHECK(proxy.GetActive() == &layerB);
}
