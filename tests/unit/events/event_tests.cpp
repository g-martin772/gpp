#include <catch2/catch_test_macros.hpp>

import GPP;
import std;

using namespace GPP;
using namespace std::chrono_literals;

TEST_CASE("Window input enums expose SDL-compatible values", "[input][window]")
{
    CHECK(static_cast<std::uint32_t>(KeyCode::A) == 0x61u);
    CHECK(static_cast<std::uint32_t>(ScanCode::A) == 4u);
    CHECK(static_cast<std::uint32_t>(ScanCode::F12) == 69u);
    CHECK(static_cast<std::uint8_t>(MouseButton::Left) == 1u);
    CHECK(static_cast<std::uint8_t>(MouseButton::X2) == 5u);
}

TEST_CASE("Event dispatcher preserves immediate and deferred delivery", "[events]")
{
    EventDispatcher dispatcher;
    std::vector<int> values;

    auto immediate = dispatcher.Subscribe<KeyEvent>(
        [&](const KeyEvent&) { values.push_back(1); });
    auto deferred = dispatcher.Subscribe<KeyEvent>(
        [&](const KeyEvent&) { values.push_back(2); },
        EventDelivery::Deferred, EventTarget::Main);

    dispatcher.Publish(KeyEvent{});
    REQUIRE(values == std::vector<int>{1});

    dispatcher.Pump(EventTarget::Main);
    REQUIRE(values == std::vector<int>{1, 2});
}

TEST_CASE("Event dispatcher routes async work to the thread pool", "[events][threadpool]")
{
    EventDispatcher dispatcher;
    std::promise<std::thread::id> promise;
    auto future = promise.get_future();
    const auto caller = std::this_thread::get_id();
    auto subscription = dispatcher.Subscribe<KeyEvent>(
        [&](const KeyEvent&) { promise.set_value(std::this_thread::get_id()); },
        EventDelivery::Async, EventTarget::ThreadPool);

    dispatcher.Publish(KeyEvent{});
    REQUIRE(future.wait_for(2s) == std::future_status::ready);
    REQUIRE(future.get() != caller);
}

TEST_CASE("Input state tracks key transitions", "[input]")
{
    auto d = std::make_shared<EventDispatcher>();
    InputState input(d);
    auto& dispatcher = *d;

    dispatcher.Publish(KeyEvent{.Key = static_cast<KeyCode>(42), .Down = true});
    REQUIRE(input.IsKeyDown(static_cast<KeyCode>(42)));
    REQUIRE(input.WasKeyPressed(static_cast<KeyCode>(42)));

    input.BeginFrame();
    REQUIRE(input.IsKeyDown(static_cast<KeyCode>(42)));
    REQUIRE_FALSE(input.WasKeyPressed(static_cast<KeyCode>(42)));

    dispatcher.Publish(KeyEvent{.Key = static_cast<KeyCode>(42), .Down = false});
    REQUIRE_FALSE(input.IsKeyDown(static_cast<KeyCode>(42)));
    REQUIRE(input.WasKeyReleased(static_cast<KeyCode>(42)));

    dispatcher.Publish(MouseButtonEvent{.Button = static_cast<MouseButton>(1), .Down = true});
    REQUIRE(input.IsMouseButtonDown(static_cast<MouseButton>(1)));
    REQUIRE(input.WasMouseButtonPressed(static_cast<MouseButton>(1)));
    input.BeginFrame();
    dispatcher.Publish(MouseButtonEvent{.Button = static_cast<MouseButton>(1), .Down = false});
    REQUIRE(input.WasMouseButtonReleased(static_cast<MouseButton>(1)));
}
