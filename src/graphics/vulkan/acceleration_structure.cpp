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
            out.Info.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
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

    void BuildBlasBatch(const std::shared_ptr<VulkanDevice>& device, const VulkanCommandPool& pool,
                        const vk::Queue queue, const std::span<Blas* const> blases)
    {
        if (blases.empty()) return;
        GPP_ZONE("BuildBlasBatch");
        vk::DeviceSize scratchSize = 0;
        for (const auto* blas : blases) scratchSize = std::max(scratchSize, blas->ScratchSize());
        VulkanBuffer scratch;
        const auto address = CreateScratch(device, scratch, scratchSize);

        // One scratch buffer is shared, so consecutive builds are serialised with a barrier
        vk::MemoryBarrier barrier{vk::AccessFlagBits::eAccelerationStructureWriteKHR,
                                  vk::AccessFlagBits::eAccelerationStructureReadKHR
                                      | vk::AccessFlagBits::eAccelerationStructureWriteKHR};
        ImmediateSubmit(pool, queue, [&](const vk::CommandBuffer cmd)
        {
            bool first = true;
            for (const auto* blas : blases)
            {
                if (!first)
                {
                    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
                                        vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR, {}, barrier, {}, {});
                }
                first = false;
                blas->RecordBuild(cmd, address);
            }
        });
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
        build.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
        build.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
        build.geometryCount = 1;
        build.pGeometries = &geometry;
        const auto sizes = m_Device->GetDevice().getAccelerationStructureBuildSizesKHR(
            vk::AccelerationStructureBuildTypeKHR::eDevice, build, capacity, m_Device->GetDispatcher());

        m_Structure.Create(m_Device, vk::AccelerationStructureTypeKHR::eTopLevel, sizes.accelerationStructureSize);
        m_ScratchAddress = CreateScratch(m_Device, m_Scratch, sizes.buildScratchSize);
        m_Capacity = capacity;
    }

    void Tlas::SetInstances(const std::span<const TlasInstance> instances)
    {
        Reserve(std::max(m_Capacity, std::bit_ceil(static_cast<std::uint32_t>(instances.size()))));
        std::vector<vk::AccelerationStructureInstanceKHR> packed;
        packed.reserve(instances.size());
        for (const auto& instance : instances) packed.push_back(PackTlasInstance(instance));
        if (!packed.empty())
        {
            m_Instances.Upload(packed.data(), packed.size() * sizeof(vk::AccelerationStructureInstanceKHR));
        }
        m_Count = static_cast<std::uint32_t>(packed.size());
    }

    void Tlas::RecordBuild(const vk::CommandBuffer commandBuffer) const
    {
        const auto geometry = Geometry();
        vk::AccelerationStructureBuildGeometryInfoKHR build{};
        build.type = vk::AccelerationStructureTypeKHR::eTopLevel;
        build.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
        build.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
        build.dstAccelerationStructure = m_Structure.GetHandle();
        build.geometryCount = 1;
        build.pGeometries = &geometry;
        build.scratchData.deviceAddress = m_ScratchAddress;
        const vk::AccelerationStructureBuildRangeInfoKHR range{m_Count, 0, 0, 0};
        const auto* ranges = &range;
        commandBuffer.buildAccelerationStructuresKHR(1, &build, &ranges, m_Device->GetDispatcher());
    }
}
