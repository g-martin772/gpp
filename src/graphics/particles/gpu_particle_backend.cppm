module;
#include <vk_mem_alloc.h>
export module GPP.Graphics:Particles.Gpu;

import std;
import glm;
import vulkan;
import GPP.Core;
import GPP.Simulation;
import :Vulkan;
import :Shader;

export namespace GPP
{
    struct GpuParticleOptions
    {
        std::shared_ptr<VulkanDevice> Device;
        std::shared_ptr<IFileSystem> FileSystem;
        std::shared_ptr<Logger> Log;
        // Directory with emit.comp, step.comp, ... (gpp/shaders/particles)
        std::filesystem::path ShaderDirectory;
        // Searched for particle_custom_force.glsl when Force is Custom
        std::vector<std::filesystem::path> IncludeDirectories;
        ParticleForceKind Force{ParticleForceKind::Newtonian};
        bool PostMacroStep{false};
        std::unordered_map<std::string, std::string> Defines;
        std::filesystem::path CacheDirectory;
        std::uint32_t FramesInFlight{3};
        bool PreferAtomicFloat{true};
    };

    // Buffers of the live particle set. Valid until the next MacroStep (the backend swaps its two sets each step).
    struct ParticleGpuBuffers
    {
        vk::Buffer PosMass;
        vk::Buffer VelTemp;
        vk::Buffer Meta;
        vk::Buffer State;      // word 0: particles stored, alive ones first, then dead ones from the last step
        std::uint32_t Capacity{0};
    };

    // Particles in storage buffers advanced by compute kernels. MacroStep records into the command buffer given to
    // BeginRecording, or runs synchronously when none is open. Statistics are read back asynchronously: call
    // CollectCompleted(slot) once the frame recorded for that slot has finished on the GPU.
    class GpuParticleBackend final : public IParticleBackend
    {
    public:
        static constexpr std::uint32_t kRingRecords = 256;

        explicit GpuParticleBackend(GpuParticleOptions options);
        ~GpuParticleBackend() override;
        GpuParticleBackend(const GpuParticleBackend&) = delete;
        GpuParticleBackend& operator=(const GpuParticleBackend&) = delete;

        void Configure(const ParticleSettings& settings) override;
        void Reset() override;
        void MacroStep(const ParticleMacroStep& step) override;
        [[nodiscard]] ParticleStats Stats() override;
        void Download(ParticleArrays& out) override;
        void Deposit(const ParticleGridSpec& spec, ParticleGrid& out) override;

        [[nodiscard]] std::string_view Name() const noexcept override { return "gpu"; }
        [[nodiscard]] const ParticleSettings& Settings() const noexcept override { return m_Settings; }

        void BeginRecording(vk::CommandBuffer commandBuffer, std::uint32_t frameSlot);
        void EndRecording();
        void CollectCompleted(std::uint32_t frameSlot);

        [[nodiscard]] ParticleGpuBuffers Buffers() const;
        [[nodiscard]] bool UsesAtomicFloat() const noexcept { return m_AtomicFloat; }

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
        ParticleSettings m_Settings;
        bool m_AtomicFloat{false};
    };
}
