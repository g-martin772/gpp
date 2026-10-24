module;
#include <vk_mem_alloc.h>
#include <gpp/profile.h>
module GPP.Graphics;

import std;
import glm;
import vulkan;
import GPP.Core;
import :Vulkan.AccelerationStructure;

namespace GPP
{
    namespace
    {
        VulkanBufferSpecification DeviceBuffer(const vk::DeviceSize size, const vk::BufferUsageFlags usage,
                                               const char* debugName)
        {
            VulkanBufferSpecification specification{};
            specification.size = size;
            specification.usage = usage | vk::BufferUsageFlagBits::eShaderDeviceAddress;
            specification.memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            specification.debugName = debugName;
            return specification;
        }

        vk::DeviceSize ScratchAlignment(const VulkanDevice& device)
        {
            const auto properties = device.GetPhysicalDevice().getProperties2<
                vk::PhysicalDeviceProperties2, vk::PhysicalDeviceAccelerationStructurePropertiesKHR>();
            return std::max<vk::DeviceSize>(
                properties.get<vk::PhysicalDeviceAccelerationStructurePropertiesKHR>()
                          .minAccelerationStructureScratchOffsetAlignment, 1);
        }

        vk::DeviceAddress AlignUp(const vk::DeviceAddress address, const vk::DeviceSize alignment)
        {
            return (address + alignment - 1) / alignment * alignment;
        }

        vk::DeviceAddress CreateScratch(const std::shared_ptr<VulkanDevice>& device, VulkanBuffer& scratch,
                                        const vk::DeviceSize size)
        {
            const auto alignment = ScratchAlignment(*device);
            scratch.Create(device, DeviceBuffer(size + alignment, vk::BufferUsageFlagBits::eStorageBuffer,
                                                "AccelerationStructureScratch"));
            return AlignUp(scratch.GetDeviceAddress(), alignment);
        }
    }

    vk::TransformMatrixKHR ToTransformMatrix(const glm::mat4& transform)
    {
        vk::TransformMatrixKHR out{};
        for (int row = 0; row < 3; ++row)
        {
            for (int column = 0; column < 4; ++column)
            {
                out.matrix[row][column] = transform[column][row];
            }
        }
        return out;
    }

    vk::AccelerationStructureInstanceKHR PackTlasInstance(const TlasInstance& instance)
    {
        vk::AccelerationStructureInstanceKHR out{};
        out.transform = ToTransformMatrix(instance.Transform);
        out.instanceCustomIndex = instance.CustomIndex & 0xFFFFFFu;
        out.mask = instance.Mask;
        out.instanceShaderBindingTableRecordOffset = 0;
        out.flags = static_cast<VkGeometryInstanceFlagsKHR>(instance.Flags);
        out.accelerationStructureReference = instance.Blas;
        return out;
    }

    AccelerationStructure::~AccelerationStructure()
    {
        Destroy();
    }

    AccelerationStructure::AccelerationStructure(AccelerationStructure&& other) noexcept
        : m_Device(std::move(other.m_Device)), m_Buffer(std::move(other.m_Buffer)), m_Handle(other.m_Handle),
          m_Address(other.m_Address)
    {
        other.m_Handle = nullptr;
        other.m_Address = 0;
    }

    AccelerationStructure& AccelerationStructure::operator=(AccelerationStructure&& other) noexcept
    {
        if (this == &other) return *this;
        Destroy();
        m_Device = std::move(other.m_Device);
        m_Buffer = std::move(other.m_Buffer);
        m_Handle = other.m_Handle;
        m_Address = other.m_Address;
        other.m_Handle = nullptr;
        other.m_Address = 0;
        return *this;
    }

    void AccelerationStructure::Create(const std::shared_ptr<VulkanDevice>& device,
                                       const vk::AccelerationStructureTypeKHR type, const vk::DeviceSize size)
    {
        Destroy();
        if (!device || !device->GetCapabilities().AccelerationStructure)
            throw std::runtime_error("AccelerationStructure needs a device with VK_KHR_acceleration_structure");
        m_Device = device;
        m_Buffer.Create(device, DeviceBuffer(size, vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR,
                                             "AccelerationStructure"));
        vk::AccelerationStructureCreateInfoKHR info{};
        info.buffer = m_Buffer.GetBuffer();
        info.size = size;
        info.type = type;
        const auto& dispatcher = device->GetDispatcher();
        m_Handle = device->GetDevice().createAccelerationStructureKHR(info, nullptr, dispatcher);
        m_Address = device->GetDevice().getAccelerationStructureAddressKHR(
            vk::AccelerationStructureDeviceAddressInfoKHR{m_Handle}, dispatcher);
    }

    void AccelerationStructure::Destroy()
    {
        if (m_Device && m_Handle)
        {
            m_Device->GetDevice().destroyAccelerationStructureKHR(m_Handle, nullptr, m_Device->GetDispatcher());
        }
        m_Handle = nullptr;
        m_Address = 0;
        m_Buffer.Destroy();
    }

    namespace
    {
        constexpr auto kTlasFlags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace
                                    | vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate;

        struct BlasBuild
        {
            vk::AccelerationStructureGeometryKHR Geometry;
            vk::AccelerationStructureBuildGeometryInfoKHR Info;
            std::uint32_t PrimitiveCount = 0;
        };

        BlasBuild MakeBlasBuild(const BlasTriangles& triangles)
        {
            vk::AccelerationStructureGeometryTrianglesDataKHR data{};
            data.vertexFormat = triangles.VertexFormat;
            data.vertexData.deviceAddress = triangles.VertexAddress;
            data.vertexStride = triangles.VertexStride;
            data.maxVertex = triangles.VertexCount > 0 ? triangles.VertexCount - 1 : 0;
            data.indexType = vk::IndexType::eUint32;
            data.indexData.deviceAddress = triangles.IndexAddress;

            BlasBuild out;
            out.Geometry.geometryType = vk::GeometryTypeKHR::eTriangles;
            out.Geometry.geometry.triangles = data;
            out.Geometry.flags = vk::GeometryFlagBitsKHR::eOpaque;
            out.Info.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
            out.Info.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace
                             | vk::BuildAccelerationStructureFlagBitsKHR::eAllowCompaction;
            out.Info.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
            out.Info.geometryCount = 1;
            out.PrimitiveCount = triangles.IndexCount / 3;
            return out;
        }
    }

    Blas::Blas(const std::shared_ptr<VulkanDevice>& device, const BlasTriangles& triangles)
        : m_Triangles(triangles)
    {
        auto build = MakeBlasBuild(triangles);
        build.Info.pGeometries = &build.Geometry;
        const auto sizes = device->GetDevice().getAccelerationStructureBuildSizesKHR(
            vk::AccelerationStructureBuildTypeKHR::eDevice, build.Info, build.PrimitiveCount, device->GetDispatcher());
        m_ScratchSize = sizes.buildScratchSize;
        m_Structure.Create(device, vk::AccelerationStructureTypeKHR::eBottomLevel, sizes.accelerationStructureSize);
    }

    Blas::Blas(const std::shared_ptr<VulkanDevice>& device, const VulkanCommandPool& pool, const vk::Queue queue,
               const BlasTriangles& triangles)
        : Blas(device, triangles)
    {
        Blas* self = this;
        BuildBlasBatch(device, pool, queue, std::span<Blas* const>(&self, 1));
    }

    void Blas::RecordBuild(const vk::CommandBuffer commandBuffer, const vk::DeviceAddress scratch) const
    {
        auto build = MakeBlasBuild(m_Triangles);
        build.Info.pGeometries = &build.Geometry;
        build.Info.dstAccelerationStructure = m_Structure.GetHandle();
        build.Info.scratchData.deviceAddress = scratch;
        const vk::AccelerationStructureBuildRangeInfoKHR range{build.PrimitiveCount, 0, 0, 0};
        const auto* ranges = &range;
        commandBuffer.buildAccelerationStructuresKHR(1, &build.Info, &ranges, m_Structure.GetDevice()->GetDispatcher());
    }

    void Blas::RecordCompact(const vk::CommandBuffer commandBuffer, const vk::DeviceSize compactedSize)
    {
        m_Compact.Create(m_Structure.GetDevice(), vk::AccelerationStructureTypeKHR::eBottomLevel, compactedSize);
        vk::CopyAccelerationStructureInfoKHR info{};
        info.src = m_Structure.GetHandle();
        info.dst = m_Compact.GetHandle();
        info.mode = vk::CopyAccelerationStructureModeKHR::eCompact;
        commandBuffer.copyAccelerationStructureKHR(info, m_Structure.GetDevice()->GetDispatcher());
    }

    void Blas::FinishCompact()
    {
        if (m_Compact.GetHandle()) m_Structure = std::move(m_Compact);
    }

    void BuildBlasBatch(const std::shared_ptr<VulkanDevice>& device, const VulkanCommandPool& pool,
                        const vk::Queue queue, const std::span<Blas* const> blases)
    {
        if (blases.empty()) return;
        GPP_ZONE("BuildBlasBatch");
        const auto vkDevice = device->GetDevice();
        const auto& dispatcher = device->GetDispatcher();
        const auto count = static_cast<std::uint32_t>(blases.size());
        vk::DeviceSize scratchSize = 0;
        for (const auto* blas : blases) scratchSize = std::max(scratchSize, blas->ScratchSize());
        VulkanBuffer scratch;
        const auto address = CreateScratch(device, scratch, scratchSize);

        vk::QueryPoolCreateInfo poolInfo{};
        poolInfo.queryType = vk::QueryType::eAccelerationStructureCompactedSizeKHR;
        poolInfo.queryCount = count;
        const auto queries = vkDevice.createQueryPool(poolInfo);
        struct QueryGuard
        {
            vk::Device Device;
            vk::QueryPool Pool;
            ~QueryGuard() { Device.destroyQueryPool(Pool); }
        } guard{vkDevice, queries};

        const vk::MemoryBarrier barrier{vk::AccessFlagBits::eAccelerationStructureWriteKHR,
                                        vk::AccessFlagBits::eAccelerationStructureReadKHR
                                            | vk::AccessFlagBits::eAccelerationStructureWriteKHR};
        constexpr auto stage = vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR;
        std::vector<vk::AccelerationStructureKHR> handles;
        handles.reserve(count);
        for (const auto* blas : blases) handles.push_back(blas->GetHandle());
        ImmediateSubmit(pool, queue, [&](const vk::CommandBuffer cmd)
        {
            cmd.resetQueryPool(queries, 0, count);
            bool first = true;
            for (const auto* blas : blases)
            {
                if (!first) cmd.pipelineBarrier(stage, stage, {}, barrier, {}, {});
                first = false;
                blas->RecordBuild(cmd, address);
            }
            cmd.pipelineBarrier(stage, stage, {}, barrier, {}, {});
            cmd.writeAccelerationStructuresPropertiesKHR(handles, vk::QueryType::eAccelerationStructureCompactedSizeKHR,
                                                         queries, 0, dispatcher);
        });

        std::vector<std::uint64_t> compacted(count);
        const auto result = vkDevice.getQueryPoolResults(queries, 0, count, compacted.size() * sizeof(std::uint64_t),
                                                        compacted.data(), sizeof(std::uint64_t),
                                                        vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait);
        if (result != vk::Result::eSuccess) return;

        std::vector<Blas*> shrinking;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            if (compacted[i] > 0 && compacted[i] < blases[i]->GetSize()) shrinking.push_back(blases[i]);
        }
        if (shrinking.empty()) return;
        ImmediateSubmit(pool, queue, [&](const vk::CommandBuffer cmd)
        {
            for (std::uint32_t i = 0, next = 0; i < count; ++i)
            {
                if (next < shrinking.size() && shrinking[next] == blases[i])
                {
                    shrinking[next]->RecordCompact(cmd, compacted[i]);
                    ++next;
                }
            }
        });
        for (auto* blas : shrinking) blas->FinishCompact();
    }

    Tlas::Tlas(std::shared_ptr<VulkanDevice> device)
        : m_Device(std::move(device))
    {
        Reserve(16);
    }

    vk::AccelerationStructureGeometryKHR Tlas::Geometry() const
    {
        vk::AccelerationStructureGeometryInstancesDataKHR data{};
        data.arrayOfPointers = false;
        data.data.deviceAddress = m_Instances.GetDeviceAddress();
        vk::AccelerationStructureGeometryKHR geometry{};
        geometry.geometryType = vk::GeometryTypeKHR::eInstances;
        geometry.geometry.instances = data;
        return geometry;
    }

    void Tlas::Reserve(const std::uint32_t capacity)
    {
        if (capacity <= m_Capacity) return;
        auto instanceSpec = MakeStorageBufferSpecification(
            capacity * sizeof(vk::AccelerationStructureInstanceKHR), true);
        instanceSpec.usage |= vk::BufferUsageFlagBits::eShaderDeviceAddress
                              | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR;
        instanceSpec.debugName = "TlasInstances";
        m_Instances.Create(m_Device, instanceSpec);

        const auto geometry = Geometry();
        vk::AccelerationStructureBuildGeometryInfoKHR build{};
        build.type = vk::AccelerationStructureTypeKHR::eTopLevel;
        build.flags = kTlasFlags;
        build.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
        build.geometryCount = 1;
        build.pGeometries = &geometry;
        const auto sizes = m_Device->GetDevice().getAccelerationStructureBuildSizesKHR(
            vk::AccelerationStructureBuildTypeKHR::eDevice, build, capacity, m_Device->GetDispatcher());

        m_Structure.Create(m_Device, vk::AccelerationStructureTypeKHR::eTopLevel, sizes.accelerationStructureSize);
        m_ScratchAddress = CreateScratch(m_Device, m_Scratch,
                                         std::max(sizes.buildScratchSize, sizes.updateScratchSize));
        m_Capacity = capacity;
        m_Built = false;
    }

    void Tlas::SetInstances(const std::span<const TlasInstance> instances)
    {
        Reserve(std::max(m_Capacity, std::bit_ceil(static_cast<std::uint32_t>(instances.size()))));
        m_Packed.clear();
        m_Keys.clear();
        for (const auto& instance : instances)
        {
            const auto packed = PackTlasInstance(instance);
            m_Packed.push_back(packed);
            m_Keys.push_back({packed.instanceCustomIndex, packed.mask | (packed.flags << 8),
                              packed.accelerationStructureReference});
        }
        if (!m_Packed.empty())
        {
            m_Instances.Upload(m_Packed.data(), m_Packed.size() * sizeof(vk::AccelerationStructureInstanceKHR));
        }
        m_Count = static_cast<std::uint32_t>(m_Packed.size());
        m_Refit = m_Built && m_Refits < kMaxRefits && m_Keys == m_BuiltKeys;
    }

    void Tlas::RecordBuild(const vk::CommandBuffer commandBuffer)
    {
        GPP_ZONE("Tlas.RecordBuild");
        const auto geometry = Geometry();
        vk::AccelerationStructureBuildGeometryInfoKHR build{};
        build.type = vk::AccelerationStructureTypeKHR::eTopLevel;
        build.flags = kTlasFlags;
        build.mode = m_Refit ? vk::BuildAccelerationStructureModeKHR::eUpdate : vk::BuildAccelerationStructureModeKHR::eBuild;
        if (m_Refit) build.srcAccelerationStructure = m_Structure.GetHandle();
        build.dstAccelerationStructure = m_Structure.GetHandle();
        build.geometryCount = 1;
        build.pGeometries = &geometry;
        build.scratchData.deviceAddress = m_ScratchAddress;
        const vk::AccelerationStructureBuildRangeInfoKHR range{m_Count, 0, 0, 0};
        const auto* ranges = &range;
        commandBuffer.buildAccelerationStructuresKHR(1, &build, &ranges, m_Device->GetDispatcher());
        m_Refits = m_Refit ? m_Refits + 1 : 0;
        m_Built = true;
        m_BuiltKeys = m_Keys;
    }
}
