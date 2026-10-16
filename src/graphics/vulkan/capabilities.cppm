export module GPP.Graphics:Vulkan.Capabilities;

import std;

export namespace GPP
{
    enum class GpuType { Other, Integrated, Discrete, Virtual, Cpu };

    // Keys accepted by GpuCapabilities::Has() and by a render setting's `Requires`
    namespace GpuCapability
    {
        inline constexpr std::string_view kRayQuery = "rayQuery";
        inline constexpr std::string_view kAccelerationStructure = "accelerationStructure";
        inline constexpr std::string_view kRayTracingPipeline = "rayTracingPipeline";
        inline constexpr std::string_view kShaderFloat64 = "shaderFloat64";
        inline constexpr std::string_view kAtomicFloat = "atomicFloat";
        inline constexpr std::string_view kDescriptorIndexing = "descriptorIndexing";
        inline constexpr std::string_view kHdrMetadata = "hdrMetadata";
        // Never available: lets a setting list an option that needs hardware that does not exist
        inline constexpr std::string_view kNever = "unavailable";

        inline constexpr std::array<std::string_view, 7> kAll{
            kRayQuery, kAccelerationStructure, kRayTracingPipeline, kShaderFloat64,
            kAtomicFloat, kDescriptorIndexing, kHdrMetadata};
    }

    struct GpuCapabilities
    {
        std::string DeviceName;
        std::string DriverVersion;
        std::uint32_t VendorId{0};
        std::uint32_t ApiVersion{0};
        GpuType Type{GpuType::Other};
        std::uint64_t DeviceLocalBytes{0};

        bool RayQuery{false};
        bool AccelerationStructure{false};
        bool RayTracingPipeline{false};
        bool ShaderFloat64{false};
        bool AtomicFloat{false};
        bool DescriptorIndexing{false};
        bool HdrMetadata{false};

        // Why this device was chosen over the others
        std::string Reason;

        [[nodiscard]] bool Has(std::string_view key) const noexcept
        {
            if (key.empty()) return true;
            if (key == GpuCapability::kRayQuery) return RayQuery;
            if (key == GpuCapability::kAccelerationStructure) return AccelerationStructure;
            if (key == GpuCapability::kRayTracingPipeline) return RayTracingPipeline;
            if (key == GpuCapability::kShaderFloat64) return ShaderFloat64;
            if (key == GpuCapability::kAtomicFloat) return AtomicFloat;
            if (key == GpuCapability::kDescriptorIndexing) return DescriptorIndexing;
            if (key == GpuCapability::kHdrMetadata) return HdrMetadata;
            return false;
        }

        // Switches a capability off (also the test and GPP_GPU_DISABLE hook). Unknown keys are ignored.
        void Disable(std::string_view key) noexcept
        {
            if (key == GpuCapability::kRayQuery) RayQuery = false;
            else if (key == GpuCapability::kAccelerationStructure) { AccelerationStructure = false; RayQuery = false; }
            else if (key == GpuCapability::kRayTracingPipeline) RayTracingPipeline = false;
            else if (key == GpuCapability::kShaderFloat64) ShaderFloat64 = false;
            else if (key == GpuCapability::kAtomicFloat) AtomicFloat = false;
            else if (key == GpuCapability::kDescriptorIndexing) DescriptorIndexing = false;
            else if (key == GpuCapability::kHdrMetadata) HdrMetadata = false;
        }
    };

    [[nodiscard]] inline std::string_view CapabilityLabel(std::string_view key) noexcept
    {
        if (key == GpuCapability::kRayQuery) return "hardware ray queries (VK_KHR_ray_query)";
        if (key == GpuCapability::kAccelerationStructure) return "acceleration structures (VK_KHR_acceleration_structure)";
        if (key == GpuCapability::kRayTracingPipeline) return "ray tracing pipelines (VK_KHR_ray_tracing_pipeline)";
        if (key == GpuCapability::kShaderFloat64) return "64-bit floats in shaders (shaderFloat64)";
        if (key == GpuCapability::kAtomicFloat) return "float atomics (VK_EXT_shader_atomic_float)";
        if (key == GpuCapability::kDescriptorIndexing) return "descriptor indexing (bindless arrays)";
        if (key == GpuCapability::kHdrMetadata) return "HDR metadata (VK_EXT_hdr_metadata)";
        if (key == GpuCapability::kNever) return "hardware that does not exist yet";
        return key;
    }

    [[nodiscard]] inline std::string_view GpuTypeName(GpuType type) noexcept
    {
        switch (type)
        {
        case GpuType::Integrated: return "integrated";
        case GpuType::Discrete: return "discrete";
        case GpuType::Virtual: return "virtual";
        case GpuType::Cpu: return "cpu";
        case GpuType::Other: break;
        }
        return "other";
    }

    // Higher is better. Discrete beats integrated whatever the optional features; features and memory break ties.
    [[nodiscard]] inline int ScoreGpu(const GpuCapabilities& caps) noexcept
    {
        int score = 0;
        switch (caps.Type)
        {
        case GpuType::Discrete: score = 1000; break;
        case GpuType::Integrated: score = 300; break;
        case GpuType::Virtual: score = 100; break;
        case GpuType::Cpu: score = 10; break;
        case GpuType::Other: score = 0; break;
        }
        if (caps.RayQuery) score += 100;
        if (caps.AccelerationStructure) score += 20;
        if (caps.ShaderFloat64) score += 20;
        if (caps.DescriptorIndexing) score += 10;
        constexpr std::uint64_t kGiB = 1ull << 30;
        score += static_cast<int>(std::min<std::uint64_t>(caps.DeviceLocalBytes / kGiB, 24)) * 5;
        return score;
    }

    struct GpuCandidate
    {
        GpuCapabilities Caps;
        bool Suitable{true};
    };

    // Index of the best suitable candidate (first wins ties), nullopt when none is suitable. `preferred` is a
    // case-insensitive substring of a device name that overrides the scoring when it matches a suitable device.
    [[nodiscard]] inline std::optional<std::size_t> ChooseGpu(const std::vector<GpuCandidate>& candidates,
                                                              std::string_view preferred = {})
    {
        const auto lower = [](std::string_view text)
        {
            std::string out(text);
            std::ranges::transform(out, out.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return out;
        };
        if (!preferred.empty())
        {
            const auto needle = lower(preferred);
            for (std::size_t i = 0; i < candidates.size(); ++i)
            {
                if (candidates[i].Suitable && lower(candidates[i].Caps.DeviceName).contains(needle)) return i;
            }
        }
        std::optional<std::size_t> best;
        int bestScore = std::numeric_limits<int>::min();
        for (std::size_t i = 0; i < candidates.size(); ++i)
        {
            if (!candidates[i].Suitable) continue;
            if (const int score = ScoreGpu(candidates[i].Caps); score > bestScore)
            {
                bestScore = score;
                best = i;
            }
        }
        return best;
    }

    [[nodiscard]] inline std::string DescribeChoice(const std::vector<GpuCandidate>& candidates, std::size_t chosen,
                                                    bool forced)
    {
        const auto& caps = candidates[chosen].Caps;
        std::size_t suitable = 0;
        for (const auto& candidate : candidates) suitable += candidate.Suitable ? 1 : 0;
        std::string reason = forced ? "selected by GPP_GPU" : suitable <= 1 ? "only suitable device" : "highest score";
        reason += std::format(" ({} GPU, {} GiB device memory, score {})", GpuTypeName(caps.Type),
                              caps.DeviceLocalBytes >> 30, ScoreGpu(caps));
        return reason;
    }
}
