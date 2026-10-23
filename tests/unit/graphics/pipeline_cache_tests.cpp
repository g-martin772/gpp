#include <catch2/catch_test_macros.hpp>

import GPP;
import std;
import vulkan;

using namespace GPP;

namespace
{
    vk::PhysicalDeviceProperties Properties()
    {
        vk::PhysicalDeviceProperties properties{};
        properties.vendorID = 0x10de;
        properties.deviceID = 0x2504;
        for (std::size_t i = 0; i < properties.pipelineCacheUUID.size(); ++i)
        {
            properties.pipelineCacheUUID[i] = static_cast<std::uint8_t>(i + 1);
        }
        return properties;
    }

    std::vector<std::byte> MakeCache(const vk::PhysicalDeviceProperties& properties, std::size_t payload = 16)
    {
        std::vector<std::byte> data(32 + payload);
        const auto put = [&](std::size_t offset, std::uint32_t value) { std::memcpy(data.data() + offset, &value, 4); };
        put(0, 32);
        put(4, 1);
        put(8, properties.vendorID);
        put(12, properties.deviceID);
        std::memcpy(data.data() + 16, properties.pipelineCacheUUID.data(), 16);
        return data;
    }
}

TEST_CASE("Pipeline cache data from the same device is accepted", "[graphics][pipeline-cache]")
{
    const auto properties = Properties();
    CHECK(IsPipelineCacheDataCompatible(MakeCache(properties), properties));
}

TEST_CASE("Pipeline cache data from another device or driver is rejected", "[graphics][pipeline-cache]")
{
    const auto properties = Properties();
    const auto data = MakeCache(properties);

    auto other = properties;
    other.vendorID = 0x1002;
    CHECK_FALSE(IsPipelineCacheDataCompatible(data, other));

    other = properties;
    other.deviceID += 1;
    CHECK_FALSE(IsPipelineCacheDataCompatible(data, other));

    other = properties;
    other.pipelineCacheUUID[5] ^= 0xff;
    CHECK_FALSE(IsPipelineCacheDataCompatible(data, other));
}

TEST_CASE("Truncated or malformed pipeline cache data is rejected", "[graphics][pipeline-cache]")
{
    const auto properties = Properties();
    auto data = MakeCache(properties);
    CHECK_FALSE(IsPipelineCacheDataCompatible({}, properties));
    CHECK_FALSE(IsPipelineCacheDataCompatible(std::span(data).first(20), properties));

    auto badVersion = data;
    badVersion[4] = std::byte{2};
    CHECK_FALSE(IsPipelineCacheDataCompatible(badVersion, properties));

    auto hugeHeader = data;
    hugeHeader[0] = std::byte{0xff};
    CHECK_FALSE(IsPipelineCacheDataCompatible(hugeHeader, properties));
}
