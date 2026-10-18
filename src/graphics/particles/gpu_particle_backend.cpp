module;
#include <vk_mem_alloc.h>
module GPP.Graphics;

import std;
import glm;
import vulkan;
import GPP.Core;
import GPP.Simulation;
import :Vulkan;
import :Shader;
import :Particles.Gpu;

namespace GPP
{
    namespace
    {
        // std430 mirror of MacroParams in shaders/particles/particle_common.glsl
        struct GpuMacroParams
        {
            float BaseStep{0.0f};
            float Eta{0.0f};
            float InverseScale{0.0f};
            std::uint32_t EmitterCount{0};
            std::uint32_t SinkCount{0};
            std::uint32_t ColliderCount{0};
            std::uint32_t RegionCount{0};
            std::uint32_t NewTotal{0};
            std::uint32_t Capacity{0};
            std::uint32_t Flags{0};
            std::uint32_t Pad0{0};
            std::uint32_t Pad1{0};
            ParticleForceParams Force;
            std::array<ParticleEmitterParams, kMaxParticleEmitters> Emitters{};
            std::array<ParticleSinkParams, kMaxParticleSinks> Sinks{};
            std::array<ParticleColliderParams, kMaxParticleColliders> Colliders{};
            std::array<ParticleRegionParams, kMaxParticleRegions> Regions{};
        };
        static_assert(sizeof(GpuMacroParams) == 10992);

        struct Push
        {
            std::uint32_t ParamsIndex{0};
            std::uint32_t Arg0{0};
            std::uint32_t Arg1{0};
            std::uint32_t Arg2{0};
        };

        constexpr std::uint32_t kAccumWords = 1024;
        constexpr std::uint32_t kStateWords = 128;
        constexpr std::uint32_t kArgsSlots = 64;
        constexpr std::uint32_t kAccSink = 0;
        constexpr std::uint32_t kAccCollider = 384;
        constexpr std::uint32_t kAccDrag = 768;
        constexpr std::uint32_t kAccStats = 800;
        constexpr std::uint32_t kAccOutside = 920;
        constexpr std::uint32_t kSlotStride = 24;
        constexpr std::uint32_t kStateUsed = 0;
        constexpr std::uint32_t kStateDropped = 2;
        constexpr vk::DeviceSize kReadbackBytes = (kAccumWords + kStateWords) * sizeof(std::uint32_t);

        FixedPoint ReadPair(const std::uint32_t* words, const std::uint32_t base)
        {
            FixedPoint value;
            value.Value = static_cast<std::int64_t>((static_cast<std::uint64_t>(words[base + 1]) << 32) | words[base]);
            return value;
        }

        const char* ForceName(const ParticleForceKind kind)
        {
            switch (kind)
            {
            case ParticleForceKind::Newtonian: return "1";
            case ParticleForceKind::PaczynskiWiita: return "2";
            default: return "3";
            }
        }
    }

    struct GpuParticleBackend::Impl
    {
        struct Kernel
        {
            CompiledShader Compiled;
            std::unique_ptr<VulkanPipeline> Pipeline;
            std::array<vk::DescriptorSet, 2> Sets{};
            std::string Name;
        };

        struct ParticleSet
        {
            VulkanBuffer Pos;
            VulkanBuffer Vel;
            VulkanBuffer Meta;
        };

        struct Pending
        {
            bool Valid{false};
            std::uint64_t Serial{0};
            double SimTime{0.0};
        };

        explicit Impl(GpuParticleOptions opts)
            : Options(std::move(opts)),
              Device(Options.Device),
              Log(Options.Log),
              Compiler(Options.FileSystem, Options.Log),
              Pool(Device, Options.Log, Device->GetQueueIndices().Graphics),
              Descriptors(Device, 256)
        {
        }

        GpuParticleOptions Options;
        std::shared_ptr<VulkanDevice> Device;
        std::shared_ptr<Logger> Log;
        ShaderCompiler Compiler;
        VulkanCommandPool Pool;
        VulkanDescriptorAllocator Descriptors;
        VulkanDescriptorAllocator TransientDescriptors{Device, 16};

        Kernel Emit, FinalizeEmit, Bin, Scan, Scatter, DragMass, Step, PostMacro, Stats, Deposit;
        std::array<ParticleSet, 2> Sets;
        VulkanBuffer State, Ring, Accum, Args, Hist;
        std::vector<VulkanBuffer> Readback;
        std::vector<Pending> Pendings;
        ParticleStats Latest;
        ParticleSettings Settings;

        vk::CommandBuffer Cmd{};
        std::uint32_t FrameSlot{0};
        std::uint32_t StepIndex{0};
        std::uint32_t Parity{0};
        std::uint32_t RegionCount{0};
        bool Configured{false};
        bool WarnedForce{false};
        bool AtomicFloat{false};

        [[nodiscard]] vk::Queue Queue() const { return Device->GetGraphicsQueue(); }
        [[nodiscard]] float InverseScale() const
        {
            return 1.0f / (Settings.ReferenceWeight * Settings.AccumulatorResolution);
        }

        VulkanBuffer MakeBuffer(const vk::DeviceSize size, const vk::BufferUsageFlags usage, const bool host, std::string name) const
        {
            auto specification = MakeStorageBufferSpecification(size, host);
            specification.usage |= usage | vk::BufferUsageFlagBits::eStorageBuffer;
            specification.debugName = std::move(name);
            return VulkanBuffer(Device, specification, Log);
        }

        std::unordered_map<std::string, std::string> Defines() const
        {
            auto defines = Options.Defines;
            defines["PF_MAX_BIN"] = std::to_string(Settings.MaxBin);
            defines["PF_FORCE_KIND"] = ForceName(Options.Force);
            return defines;
        }

        void Build(Kernel& kernel, const std::string& file, std::unordered_map<std::string, std::string> defines)
        {
            ShaderSource source{.path = Options.ShaderDirectory / file, .stage = ShaderStage::Compute,
                                .entryPoint = "main", .defines = std::move(defines)};
            ShaderCompileOptions compile;
            compile.cacheDirectory = Options.CacheDirectory;
            compile.includeDirectories.push_back(Options.ShaderDirectory);
            for (const auto& directory : Options.IncludeDirectories) compile.includeDirectories.push_back(directory);
            kernel.Name = file;
            kernel.Compiled = Compiler.Compile(source, compile);
            kernel.Pipeline = std::make_unique<VulkanPipeline>(Device, kernel.Compiled);
            kernel.Sets = {};
        }

        const VulkanBuffer& BufferFor(const std::uint32_t binding, const std::uint32_t parity) const
        {
            const auto& current = Sets[parity];
            const auto& other = Sets[1 - parity];
            switch (binding)
            {
            case 0: return current.Pos;
            case 1: return current.Vel;
            case 2: return current.Meta;
            case 3: return other.Pos;
            case 4: return other.Vel;
            case 5: return other.Meta;
            case 6: return State;
            case 7: return Ring;
            case 8: return Accum;
            case 9: return Args;
            case 10: return Hist;
            default: throw std::runtime_error("particle kernel binding out of range");
            }
        }

        void WriteSets(Kernel& kernel)
        {
            if (kernel.Pipeline->GetDescriptorSetLayouts().empty()) return;
            for (std::uint32_t parity = 0; parity < 2; ++parity)
            {
                kernel.Sets[parity] = Descriptors.Allocate(kernel.Pipeline->GetDescriptorSetLayouts()[0]);
                DescriptorSetWriter writer(Device->GetDevice());
                for (const auto& binding : kernel.Compiled.reflection.descriptorBindings)
                {
                    if (binding.set != 0 || binding.binding > 10) continue;
                    writer.WriteStorageBuffer(kernel.Sets[parity], binding.binding,
                                              BufferFor(binding.binding, parity).GetBuffer());
                }
                writer.Update();
            }
        }

        void CompileAll()
        {
            const auto defines = Defines();
            Build(Emit, "emit.comp", defines);
            Build(FinalizeEmit, "finalize_emit.comp", defines);
            Build(Bin, "bin.comp", defines);
            Build(Scan, "scan.comp", defines);
            Build(Scatter, "scatter.comp", defines);
            Build(DragMass, "drag_mass.comp", defines);
            Build(Step, "step.comp", defines);
            Build(Stats, "stats.comp", defines);
            if (Options.PostMacroStep) Build(PostMacro, "post_macro.comp", defines);
        }

        void AllocateAndBind()
        {
            const vk::DeviceSize particleBytes = static_cast<vk::DeviceSize>(Settings.Capacity) * sizeof(glm::vec4);
            const auto usage = vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst;
            for (std::uint32_t i = 0; i < 2; ++i)
            {
                Sets[i].Pos = MakeBuffer(particleBytes, usage, false, "ParticlePosMass");
                Sets[i].Vel = MakeBuffer(particleBytes, usage, false, "ParticleVelTemp");
                Sets[i].Meta = MakeBuffer(particleBytes, usage, false, "ParticleMeta");
            }
            State = MakeBuffer(kStateWords * sizeof(std::uint32_t), usage, false, "ParticleState");
            Accum = MakeBuffer(kAccumWords * sizeof(std::uint32_t), usage, false, "ParticleAccum");
            Args = MakeBuffer(kArgsSlots * sizeof(glm::uvec4),
                              usage | vk::BufferUsageFlagBits::eIndirectBuffer, false, "ParticleArgs");
            const std::size_t blocks = Settings.Capacity / 256 + 2;
            Hist = MakeBuffer(blocks * (Settings.MaxBin + 2) * sizeof(std::uint32_t), usage, false, "ParticleHistogram");
            Ring = MakeBuffer(static_cast<vk::DeviceSize>(Options.FramesInFlight) * GpuParticleBackend::kRingRecords
                                  * sizeof(GpuMacroParams),
                              {}, true, "ParticleRing");
            Readback.clear();
            for (std::uint32_t i = 0; i < Options.FramesInFlight; ++i)
            {
                VulkanBuffer buffer(Device, MakeReadbackBufferSpecification(kReadbackBytes), Log);
                Readback.push_back(std::move(buffer));
            }
            Pendings.assign(Options.FramesInFlight, {});
        }

        void BindAll()
        {
            Descriptors = VulkanDescriptorAllocator(Device, 256);
            for (Kernel* kernel : {&Emit, &FinalizeEmit, &Bin, &Scan, &Scatter, &DragMass, &Step, &Stats})
            {
                WriteSets(*kernel);
            }
            if (Options.PostMacroStep) WriteSets(PostMacro);
        }

        void Barrier(const vk::CommandBuffer cmd, const bool transferIn = false, const bool transferOut = false) const
        {
            vk::PipelineStageFlags src = vk::PipelineStageFlagBits::eComputeShader;
            vk::PipelineStageFlags dst = vk::PipelineStageFlagBits::eComputeShader | vk::PipelineStageFlagBits::eDrawIndirect;
            vk::AccessFlags srcAccess = vk::AccessFlagBits::eShaderWrite;
            vk::AccessFlags dstAccess = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite
                                        | vk::AccessFlagBits::eIndirectCommandRead;
            if (transferIn)
            {
                src |= vk::PipelineStageFlagBits::eTransfer;
                srcAccess |= vk::AccessFlagBits::eTransferWrite;
            }
            if (transferOut)
            {
                dst |= vk::PipelineStageFlagBits::eTransfer;
                dstAccess |= vk::AccessFlagBits::eTransferRead;
            }
            const vk::MemoryBarrier barrier{srcAccess, dstAccess};
            cmd.pipelineBarrier(src, dst, {}, 1, &barrier, 0, nullptr, 0, nullptr);
        }

        void Bind(const vk::CommandBuffer cmd, Kernel& kernel, const Push& push) const
        {
            cmd.bindPipeline(vk::PipelineBindPoint::eCompute, kernel.Pipeline->GetPipeline());
            if (kernel.Sets[Parity])
            {
                cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, kernel.Pipeline->GetLayout(), 0,
                                       kernel.Sets[Parity], {});
            }
            if (!kernel.Compiled.reflection.pushConstants.empty())
            {
                cmd.pushConstants(kernel.Pipeline->GetLayout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(Push), &push);
            }
        }

        void Dispatch(const vk::CommandBuffer cmd, Kernel& kernel, const std::uint32_t groups, const Push& push) const
        {
            Bind(cmd, kernel, push);
            cmd.dispatch(groups, 1, 1);
        }

        void DispatchIndirect(const vk::CommandBuffer cmd, Kernel& kernel, const std::uint32_t slot, const Push& push) const
        {
            Bind(cmd, kernel, push);
            cmd.dispatchIndirect(Args.GetBuffer(), slot * sizeof(glm::uvec4));
        }

        void Zero(const vk::CommandBuffer cmd, const VulkanBuffer& buffer, const std::uint32_t firstWord, const std::uint32_t words) const
        {
            cmd.fillBuffer(buffer.GetBuffer(), firstWord * sizeof(std::uint32_t), words * sizeof(std::uint32_t), 0u);
        }

        void RecordReset(const vk::CommandBuffer cmd)
        {
            Zero(cmd, State, 0, kStateWords);
            Zero(cmd, Accum, 0, kAccumWords);
            Zero(cmd, Args, 0, kArgsSlots * 4);
            Barrier(cmd, true);
            Parity = 0;
            StepIndex = 0;
            for (auto& pending : Pendings) pending.Valid = false;
        }

        void RunNow(const std::function<void(vk::CommandBuffer)>& record)
        {
            if (Cmd)
            {
                record(Cmd);
                return;
            }
            ImmediateSubmit(Pool, Queue(), record);
        }

        void Record(const vk::CommandBuffer cmd, const ParticleMacroStep& step, const std::uint32_t slot, const std::uint32_t index)
        {
            if (index >= GpuParticleBackend::kRingRecords)
            {
                throw std::runtime_error("more particle macro steps in one frame than the parameter ring holds");
            }
            if (!WarnedForce && step.Force.Kind != static_cast<std::uint32_t>(Options.Force) && Log)
            {
                WarnedForce = true;
                Log->Warn("Particle force kind {} does not match the compiled kernels ({}).", step.Force.Kind,
                          static_cast<std::uint32_t>(Options.Force));
            }

            auto params = std::make_unique<GpuMacroParams>();
            params->BaseStep = step.BaseStep;
            params->Eta = Settings.Eta;
            params->InverseScale = InverseScale();
            params->EmitterCount = static_cast<std::uint32_t>(std::min<std::size_t>(step.Emitters.size(), kMaxParticleEmitters));
            params->SinkCount = static_cast<std::uint32_t>(std::min<std::size_t>(step.Sinks.size(), kMaxParticleSinks));
            params->ColliderCount = static_cast<std::uint32_t>(std::min<std::size_t>(step.Colliders.size(), kMaxParticleColliders));
            params->RegionCount = static_cast<std::uint32_t>(std::min<std::size_t>(step.Regions.size(), kMaxParticleRegions));
            params->Capacity = Settings.Capacity;
            params->Force = step.Force;
            std::uint32_t newTotal = 0;
            for (std::uint32_t i = 0; i < params->EmitterCount; ++i)
            {
                params->Emitters[i] = step.Emitters[i];
                params->Emitters[i].Batch.z = newTotal;
                newTotal += step.Emitters[i].Batch.y;
            }
            params->NewTotal = newTotal;
            std::ranges::copy_n(step.Sinks.begin(), params->SinkCount, params->Sinks.begin());
            std::ranges::copy_n(step.Colliders.begin(), params->ColliderCount, params->Colliders.begin());
            std::ranges::copy_n(step.Regions.begin(), params->RegionCount, params->Regions.begin());
            bool hasDrag = false;
            for (std::uint32_t i = 0; i < params->ColliderCount; ++i)
            {
                hasDrag = hasDrag || params->Colliders[i].Info.x == static_cast<std::uint32_t>(ParticleColliderResponse::Drag);
            }
            const std::uint32_t recordIndex = slot * GpuParticleBackend::kRingRecords + index;
            Ring.Upload(params.get(), sizeof(GpuMacroParams), static_cast<vk::DeviceSize>(recordIndex) * sizeof(GpuMacroParams));

            const Push push{recordIndex, 0, 0, 0};
            if (hasDrag)
            {
                Zero(cmd, Accum, kAccDrag, 2 * kMaxParticleColliders);
                Barrier(cmd, true);
            }
            if (newTotal > 0)
            {
                Dispatch(cmd, Emit, (newTotal + 63) / 64, push);
                Barrier(cmd);
            }
            Dispatch(cmd, FinalizeEmit, 1, push);
            Barrier(cmd);
            DispatchIndirect(cmd, Bin, 0, push);
            Barrier(cmd);
            Dispatch(cmd, Scan, 1, push);
            Barrier(cmd);
            DispatchIndirect(cmd, Scatter, 0, push);
            Barrier(cmd);
            Parity ^= 1u;

            if (hasDrag)
            {
                DispatchIndirect(cmd, DragMass, 1, push);
                Barrier(cmd);
            }
            for (std::uint32_t bin = 0; bin <= Settings.MaxBin; ++bin)
            {
                DispatchIndirect(cmd, Step, 2 + bin, {recordIndex, bin, 0, 0});
            }
            Barrier(cmd);
            if (Options.PostMacroStep)
            {
                DispatchIndirect(cmd, PostMacro, 1, push);
                Barrier(cmd);
            }
            if (step.CollectStats)
            {
                Zero(cmd, Accum, kAccStats, 108);
                Barrier(cmd, true);
                DispatchIndirect(cmd, Stats, 0, push);
                Barrier(cmd, false, true);
                CopyBuffer(cmd, Accum.GetBuffer(), Readback[slot].GetBuffer(), kAccumWords * sizeof(std::uint32_t));
                CopyBuffer(cmd, State.GetBuffer(), Readback[slot].GetBuffer(), kStateWords * sizeof(std::uint32_t), 0,
                           kAccumWords * sizeof(std::uint32_t));
                const vk::MemoryBarrier hostBarrier{vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eHostRead};
                cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eHost, {}, 1,
                                    &hostBarrier, 0, nullptr, 0, nullptr);
                Pendings[slot] = {true, 0, step.SimTime + step.BaseStep};
                RegionCount = params->RegionCount;
            }
        }

        void Decode(const std::uint32_t slot)
        {
            if (!Pendings[slot].Valid) return;
            Pendings[slot].Valid = false;
            std::array<std::uint32_t, kAccumWords + kStateWords> words{};
            Readback[slot].Read(words.data(), kReadbackBytes);
            const std::uint32_t* acc = words.data();
            const std::uint32_t* state = words.data() + kAccumWords;
            const double scale = 1.0 / InverseScale();

            ParticleStats stats;
            stats.SimTime = Pendings[slot].SimTime;
            stats.Serial = state[40];
            stats.Dropped = state[kStateDropped];
            stats.Alive = static_cast<std::uint64_t>(ReadPair(acc, kAccStats).Value);
            stats.AliveMass = ReadPair(acc, kAccStats + 2).ToDouble(scale);
            const double massT = ReadPair(acc, kAccStats + 4).ToDouble(scale);
            const glm::dvec3 momentum(ReadPair(acc, kAccStats + 6).ToDouble(scale), ReadPair(acc, kAccStats + 8).ToDouble(scale),
                                      ReadPair(acc, kAccStats + 10).ToDouble(scale));
            if (stats.AliveMass > 0.0)
            {
                stats.MeanTemperature = massT / stats.AliveMass;
                stats.MeanVelocity = momentum / stats.AliveMass;
            }
            for (std::uint32_t r = 0; r < RegionCount; ++r)
            {
                const std::uint32_t base = kAccStats + 2 * (6 + 6 * r);
                ParticleRegionTotals region;
                region.Count = static_cast<std::uint64_t>(ReadPair(acc, base).Value);
                region.Mass = ReadPair(acc, base + 2).ToDouble(scale);
                if (region.Mass > 0.0)
                {
                    region.MeanTemperature = ReadPair(acc, base + 4).ToDouble(scale) / region.Mass;
                    region.MeanVelocity = glm::dvec3(ReadPair(acc, base + 6).ToDouble(scale), ReadPair(acc, base + 8).ToDouble(scale),
                                                     ReadPair(acc, base + 10).ToDouble(scale)) / region.Mass;
                }
                stats.Regions.push_back(region);
            }
            for (std::uint32_t s = 0; s < kMaxParticleSinks; ++s)
            {
                const std::uint32_t base = kAccSink + s * kSlotStride;
                ParticleAccumulatorTotals totals;
                totals.Count = acc[base + 16];
                totals.Killed = acc[base + 17];
                for (std::uint32_t q = 0; q < kParticleAccumulators; ++q) totals.Raw[q] = ReadPair(acc, base + 2 * q).ToDouble(scale);
                stats.Sinks.push_back(totals);
            }
            for (std::uint32_t c = 0; c < kMaxParticleColliders; ++c)
            {
                const std::uint32_t base = kAccCollider + c * kSlotStride;
                ParticleAccumulatorTotals totals;
                totals.Count = acc[base + 16];
                for (std::uint32_t q = 0; q < kParticleAccumulators; ++q) totals.Raw[q] = ReadPair(acc, base + 2 * q).ToDouble(scale);
                stats.Colliders.push_back(totals);
            }
            Latest = std::move(stats);
        }

        void ReadBuffer(const VulkanBuffer& source, const vk::DeviceSize bytes, void* destination)
        {
            if (bytes == 0) return;
            VulkanBuffer staging(Device, MakeReadbackBufferSpecification(bytes), Log);
            ImmediateSubmit(Pool, Queue(), [&](const vk::CommandBuffer cmd)
            {
                CopyBuffer(cmd, source.GetBuffer(), staging.GetBuffer(), bytes);
                const vk::MemoryBarrier hostBarrier{vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eHostRead};
                cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eHost, {}, 1,
                                    &hostBarrier, 0, nullptr, 0, nullptr);
            });
            staging.Read(destination, bytes);
        }
    };

    GpuParticleBackend::GpuParticleBackend(GpuParticleOptions options)
        : m_Impl(std::make_unique<Impl>(std::move(options)))
    {
        m_AtomicFloat = m_Impl->Options.PreferAtomicFloat && m_Impl->Device->GetCapabilities().Has(GpuCapability::kAtomicFloat);
        m_Impl->AtomicFloat = m_AtomicFloat;
    }

    GpuParticleBackend::~GpuParticleBackend()
    {
        if (m_Impl && m_Impl->Device) m_Impl->Device->WaitIdle();
    }

    void GpuParticleBackend::Configure(const ParticleSettings& settings)
    {
        auto& impl = *m_Impl;
        impl.Device->WaitIdle();
        m_Settings = settings;
        m_Settings.MaxBin = std::min(m_Settings.MaxBin, kMaxParticleBins);
        impl.Settings = m_Settings;
        impl.AllocateAndBind();
        impl.CompileAll();
        impl.BindAll();
        impl.Deposit = {};
        impl.Configured = true;
        Reset();
    }

    void GpuParticleBackend::Reset()
    {
        auto& impl = *m_Impl;
        impl.RunNow([&](const vk::CommandBuffer cmd) { impl.RecordReset(cmd); });
        impl.Latest = {};
    }

    void GpuParticleBackend::BeginRecording(const vk::CommandBuffer commandBuffer, const std::uint32_t frameSlot)
    {
        m_Impl->Cmd = commandBuffer;
        m_Impl->FrameSlot = frameSlot % m_Impl->Options.FramesInFlight;
        m_Impl->StepIndex = 0;
    }

    void GpuParticleBackend::EndRecording()
    {
        m_Impl->Cmd = vk::CommandBuffer{};
    }

    void GpuParticleBackend::CollectCompleted(const std::uint32_t frameSlot)
    {
        m_Impl->Decode(frameSlot % m_Impl->Options.FramesInFlight);
    }

    void GpuParticleBackend::MacroStep(const ParticleMacroStep& step)
    {
        auto& impl = *m_Impl;
        if (!impl.Configured) throw std::runtime_error("GpuParticleBackend::MacroStep before Configure");
        if (impl.Cmd)
        {
            impl.Record(impl.Cmd, step, impl.FrameSlot, impl.StepIndex++);
            return;
        }
        ImmediateSubmit(impl.Pool, impl.Queue(), [&](const vk::CommandBuffer cmd) { impl.Record(cmd, step, 0, 0); });
        impl.Decode(0);
    }

    ParticleStats GpuParticleBackend::Stats()
    {
        return m_Impl->Latest;
    }

    ParticleGpuBuffers GpuParticleBackend::Buffers() const
    {
        const auto& impl = *m_Impl;
        const auto& current = impl.Sets[impl.Parity];
        return {current.Pos.GetBuffer(), current.Vel.GetBuffer(), current.Meta.GetBuffer(), impl.State.GetBuffer(),
                m_Settings.Capacity};
    }

    void GpuParticleBackend::Download(ParticleArrays& out)
    {
        auto& impl = *m_Impl;
        out.Clear();
        std::array<std::uint32_t, kStateWords> state{};
        impl.ReadBuffer(impl.State, sizeof(state), state.data());
        const std::uint32_t used = std::min(state[kStateUsed], m_Settings.Capacity);
        ParticleArrays all;
        all.Resize(used);
        const auto& current = impl.Sets[impl.Parity];
        impl.ReadBuffer(current.Pos, used * sizeof(glm::vec4), all.PosMass.data());
        impl.ReadBuffer(current.Vel, used * sizeof(glm::vec4), all.VelTemp.data());
        impl.ReadBuffer(current.Meta, used * sizeof(glm::uvec4), all.Meta.data());
        for (std::size_t i = 0; i < used; ++i)
        {
            if (!ParticleArrays::Alive(all.Meta[i])) continue;
            out.PosMass.push_back(all.PosMass[i]);
            out.VelTemp.push_back(all.VelTemp[i]);
            out.Meta.push_back(all.Meta[i]);
        }
    }

    void GpuParticleBackend::Deposit(const ParticleGridSpec& spec, ParticleGrid& out)
    {
        auto& impl = *m_Impl;
        if (impl.Cmd) throw std::runtime_error("GpuParticleBackend::Deposit cannot run inside a recording");
        const std::size_t nodes = spec.NodeCount();
        out.Spec = spec;
        out.Mass.assign(nodes, 0.0f);
        out.Momentum.assign(nodes, glm::vec3(0.0f));
        out.MassTemperature.assign(nodes, 0.0f);
        out.OutsideMass = 0.0;
        if (nodes == 0) return;

        auto defines = impl.Defines();
        if (m_AtomicFloat) defines["PF_ATOMIC_FLOAT"] = "1";
        impl.Build(impl.Deposit, "deposit.comp", defines);
        impl.Deposit.Sets = {};

        const vk::DeviceSize gridBytes = nodes * 5 * sizeof(std::uint32_t);
        auto grid = impl.MakeBuffer(gridBytes, vk::BufferUsageFlagBits::eTransferSrc, false, "ParticleGrid");
        struct SpecBlock
        {
            glm::vec4 Origin;
            glm::uvec4 Dimensions;
            glm::vec4 Inverse;
        };
        const float unit = impl.Settings.ReferenceWeight * 0x1p-12f;
        const SpecBlock block{glm::vec4(spec.Origin, spec.CellSize), glm::uvec4(spec.Dimensions, 0u),
                              glm::vec4(1.0f / unit, 1.0f / (unit * spec.VelocityReference),
                                        1.0f / (unit * spec.TemperatureReference), 0.0f)};
        auto specBuffer = impl.MakeBuffer(sizeof(SpecBlock), {}, true, "ParticleGridSpec");
        specBuffer.Upload(&block, sizeof(block));

        impl.TransientDescriptors.ResetPools();
        const vk::DescriptorSet set = impl.TransientDescriptors.Allocate(impl.Deposit.Pipeline->GetDescriptorSetLayouts()[0]);
        DescriptorSetWriter writer(impl.Device->GetDevice());
        for (const auto& binding : impl.Deposit.Compiled.reflection.descriptorBindings)
        {
            if (binding.set != 0) continue;
            if (binding.binding == 11) writer.WriteStorageBuffer(set, 11, grid.GetBuffer());
            else if (binding.binding == 12) writer.WriteStorageBuffer(set, 12, specBuffer.GetBuffer());
            else writer.WriteStorageBuffer(set, binding.binding, impl.BufferFor(binding.binding, impl.Parity).GetBuffer());
        }
        writer.Update();

        ImmediateSubmit(impl.Pool, impl.Queue(), [&](const vk::CommandBuffer cmd)
        {
            impl.Zero(cmd, impl.Accum, kAccOutside, 2);
            cmd.fillBuffer(grid.GetBuffer(), 0, gridBytes, 0u);
            impl.Barrier(cmd, true);
            const Push push{0, 0, 0, 0};
            cmd.bindPipeline(vk::PipelineBindPoint::eCompute, impl.Deposit.Pipeline->GetPipeline());
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, impl.Deposit.Pipeline->GetLayout(), 0, set, {});
            cmd.pushConstants(impl.Deposit.Pipeline->GetLayout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(Push), &push);
            cmd.dispatch((impl.Settings.Capacity + 63) / 64, 1, 1);
            impl.Barrier(cmd, false, true);
        });

        std::vector<std::uint32_t> words(nodes * 5);
        impl.ReadBuffer(grid, gridBytes, words.data());
        std::array<std::uint32_t, kAccumWords> acc{};
        impl.ReadBuffer(impl.Accum, sizeof(acc), acc.data());
        const double massScale = 1.0 / impl.InverseScale();
        out.OutsideMass = ReadPair(acc.data(), kAccOutside).ToDouble(massScale);
        for (std::size_t n = 0; n < nodes; ++n)
        {
            const auto read = [&](const std::size_t channel, const float channelUnit)
            {
                if (m_AtomicFloat) return std::bit_cast<float>(words[channel * nodes + n]);
                return static_cast<float>(static_cast<std::int32_t>(words[channel * nodes + n])) * channelUnit;
            };
            out.Mass[n] = read(0, unit);
            out.Momentum[n] = {read(1, unit * spec.VelocityReference), read(2, unit * spec.VelocityReference),
                               read(3, unit * spec.VelocityReference)};
            out.MassTemperature[n] = read(4, unit * spec.TemperatureReference);
        }
    }
}
