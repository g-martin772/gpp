#include <catch2/catch_test_macros.hpp>

import GPP;
import std;

using namespace GPP;

namespace
{
    GpuCandidate Make(std::string name, GpuType type, std::uint64_t gib, bool rayQuery = false, bool suitable = true)
    {
        GpuCandidate candidate;
        candidate.Caps.DeviceName = std::move(name);
        candidate.Caps.Type = type;
        candidate.Caps.DeviceLocalBytes = gib << 30;
        candidate.Caps.RayQuery = rayQuery;
        candidate.Caps.AccelerationStructure = rayQuery;
        candidate.Suitable = suitable;
        return candidate;
    }
}

TEST_CASE("A discrete GPU beats an integrated one that has more optional features", "[graphics][capabilities]")
{
    auto integrated = Make("Iris", GpuType::Integrated, 16, true);
    integrated.Caps.ShaderFloat64 = true;
    const std::vector candidates{integrated, Make("GTX 1060", GpuType::Discrete, 6)};
    CHECK(ChooseGpu(candidates) == 1);
}

TEST_CASE("Features and memory break ties between equal types", "[graphics][capabilities]")
{
    const std::vector candidates{Make("A", GpuType::Discrete, 8), Make("B", GpuType::Discrete, 8, true),
                                 Make("C", GpuType::Discrete, 12)};
    CHECK(ChooseGpu(candidates) == 1);
}

TEST_CASE("The first device wins an exact tie", "[graphics][capabilities]")
{
    const std::vector candidates{Make("A", GpuType::Discrete, 8), Make("B", GpuType::Discrete, 8)};
    CHECK(ChooseGpu(candidates) == 0);
}

TEST_CASE("Unsuitable devices are never chosen", "[graphics][capabilities]")
{
    const std::vector candidates{Make("Big", GpuType::Discrete, 24, true, false), Make("Small", GpuType::Integrated, 2)};
    CHECK(ChooseGpu(candidates) == 1);
    CHECK_FALSE(ChooseGpu({Make("Big", GpuType::Discrete, 24, true, false)}));
    CHECK_FALSE(ChooseGpu({}));
}

TEST_CASE("A preferred name overrides the score but only among suitable devices", "[graphics][capabilities]")
{
    const std::vector candidates{Make("NVIDIA GeForce RTX 3060", GpuType::Discrete, 12, true),
                                 Make("AMD Radeon Graphics", GpuType::Integrated, 2),
                                 Make("Broken Card", GpuType::Discrete, 24, true, false)};
    CHECK(ChooseGpu(candidates, "radeon") == 1);
    CHECK(ChooseGpu(candidates, "broken") == 0);
    CHECK(ChooseGpu(candidates, "no such gpu") == 0);
}

TEST_CASE("Capabilities answer by key and can be switched off", "[graphics][capabilities]")
{
    GpuCapabilities caps;
    caps.RayQuery = true;
    caps.AccelerationStructure = true;
    caps.ShaderFloat64 = true;
    CHECK(caps.Has(GpuCapability::kRayQuery));
    CHECK(caps.Has(""));
    CHECK_FALSE(caps.Has(GpuCapability::kAtomicFloat));
    CHECK_FALSE(caps.Has(GpuCapability::kNever));
    CHECK_FALSE(caps.Has("nonsense"));

    caps.Disable(GpuCapability::kAccelerationStructure);
    CHECK_FALSE(caps.Has(GpuCapability::kRayQuery));
    caps.Disable(GpuCapability::kShaderFloat64);
    CHECK_FALSE(caps.Has(GpuCapability::kShaderFloat64));
}

TEST_CASE("The choice is explained", "[graphics][capabilities]")
{
    const std::vector candidates{Make("A", GpuType::Discrete, 12, true), Make("B", GpuType::Integrated, 2)};
    const auto reason = DescribeChoice(candidates, 0, false);
    CHECK(reason.find("highest score") != std::string::npos);
    CHECK(reason.find("discrete") != std::string::npos);
    CHECK(DescribeChoice(candidates, 0, true).find("GPP_GPU") != std::string::npos);
    CHECK(DescribeChoice({candidates[0]}, 0, false).find("only suitable") != std::string::npos);
}
