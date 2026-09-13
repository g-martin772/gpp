module;
#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
module GPP.Graphics;

import std;
import GPP.Core;
import :Vulkan.Buffer;

namespace GPP
{
    static VulkanBufferSpecification MakeCommonBufferSpecification(
        const vk::DeviceSize size, const vk::BufferUsageFlags usage,
        const bool hostVisible, const char* debugName)
    {
        VulkanBufferSpecification specification{};
        specification.size = size;
        specification.usage = usage | vk::BufferUsageFlagBits::eTransferDst;
        specification.memoryUsage = hostVisible
                                        ? VMA_MEMORY_USAGE_AUTO_PREFER_HOST
                                        : VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        specification.persistentMapping = hostVisible;
        specification.debugName = debugName;
        if (hostVisible)
        {
            specification.allocationFlags |=
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        }
        return specification;
    }

    VulkanBufferSpecification MakeVertexBufferSpecification(const vk::DeviceSize size,
                                                            const bool hostVisible)
    {
        return MakeCommonBufferSpecification(size, vk::BufferUsageFlagBits::eVertexBuffer,
                                             hostVisible, "VertexBuffer");
    }

    VulkanBufferSpecification MakeIndexBufferSpecification(const vk::DeviceSize size,
                                                           const bool hostVisible)
    {
        return MakeCommonBufferSpecification(size, vk::BufferUsageFlagBits::eIndexBuffer,
                                             hostVisible, "IndexBuffer");
    }

    VulkanBufferSpecification MakeUniformBufferSpecification(const vk::DeviceSize size,
                                                             const bool hostVisible)
    {
        return MakeCommonBufferSpecification(size, vk::BufferUsageFlagBits::eUniformBuffer,
                                             hostVisible, "UniformBuffer");
    }

    VulkanBufferSpecification MakeStorageBufferSpecification(const vk::DeviceSize size,
                                                             const bool hostVisible)
    {
        return MakeCommonBufferSpecification(size, vk::BufferUsageFlagBits::eStorageBuffer,
                                             hostVisible, "StorageBuffer");
    }

    VulkanBufferSpecification MakeStagingBufferSpecification(const vk::DeviceSize size)
    {
        auto specification = MakeCommonBufferSpecification(
            size, vk::BufferUsageFlagBits::eTransferSrc, true, "StagingBuffer");
        specification.usage = vk::BufferUsageFlagBits::eTransferSrc;
        specification.memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        return specification;
    }

    void CopyBuffer(const vk::CommandBuffer commandBuffer, const vk::Buffer source,
                    const vk::Buffer destination, const vk::DeviceSize size,
                    const vk::DeviceSize sourceOffset, const vk::DeviceSize destinationOffset)
    {
        vk::BufferCopy region{sourceOffset, destinationOffset, size};
        commandBuffer.copyBuffer(source, destination, 1, &region);
    }

    void FillBuffer(const vk::CommandBuffer commandBuffer, const vk::Buffer buffer,
                    const uint32_t value, const vk::DeviceSize size, const vk::DeviceSize offset)
    {
        commandBuffer.fillBuffer(buffer, offset, size, value);
    }

    void UpdateBuffer(const vk::CommandBuffer commandBuffer, const vk::Buffer buffer,
                      const void* data, const vk::DeviceSize size, const vk::DeviceSize offset)
    {
        if (!data || size == 0 || size > 65536)
            throw std::invalid_argument("UpdateBuffer requires non-null data and a size in [1, 65536]");
        commandBuffer.updateBuffer(buffer, offset, size, data);
    }

    void BindVertexBuffer(const vk::CommandBuffer commandBuffer, const vk::Buffer buffer,
                          const vk::DeviceSize offset, const uint32_t binding)
    {
        commandBuffer.bindVertexBuffers(binding, 1, &buffer, &offset);
    }

    void BindIndexBuffer(const vk::CommandBuffer commandBuffer, const vk::Buffer buffer,
                         const vk::DeviceSize offset, const vk::IndexType indexType)
    {
        commandBuffer.bindIndexBuffer(buffer, offset, indexType);
    }

    VulkanBuffer::VulkanBuffer(const std::shared_ptr<VulkanDevice>& device,
                               const VulkanBufferSpecification& specification,
                               const std::shared_ptr<Logger>& logger)
        : m_Logger(logger)
    {
        Create(device, specification);
    }

    VulkanBuffer::~VulkanBuffer()
    {
        Destroy();
    }

    VulkanBuffer::VulkanBuffer(VulkanBuffer&& other) noexcept
        : m_Device(std::move(other.m_Device)), m_Logger(std::move(other.m_Logger)),
          m_Specification(std::move(other.m_Specification)), m_Buffer(other.m_Buffer),
          m_Allocation(other.m_Allocation), m_MappedData(other.m_MappedData)
    {
        other.m_Buffer = nullptr;
        other.m_Allocation = nullptr;
        other.m_MappedData = nullptr;
    }

    VulkanBuffer& VulkanBuffer::operator=(VulkanBuffer&& other) noexcept
    {
        if (this == &other)
            return *this;
        Destroy();
        m_Device = std::move(other.m_Device);
        m_Logger = std::move(other.m_Logger);
        m_Specification = std::move(other.m_Specification);
        m_Buffer = other.m_Buffer;
        m_Allocation = other.m_Allocation;
        m_MappedData = other.m_MappedData;
        other.m_Buffer = nullptr;
        other.m_Allocation = nullptr;
        other.m_MappedData = nullptr;
        return *this;
    }

    void VulkanBuffer::Create(const std::shared_ptr<VulkanDevice>& device,
                              const VulkanBufferSpecification& specification)
    {
        Destroy();
        if (!device || !device->GetAllocator())
            throw std::invalid_argument("VulkanBuffer requires a device with a VMA allocator");
        if (specification.size == 0)
            throw std::invalid_argument("VulkanBuffer size must be greater than zero");
        if (!specification.usage)
            throw std::invalid_argument("VulkanBuffer requires at least one usage flag");

        m_Device = device;
        m_Specification = specification;

        vk::BufferCreateInfo createInfo{};
        createInfo.flags = specification.flags;
        createInfo.size = specification.size;
        createInfo.usage = specification.usage;
        createInfo.sharingMode = specification.sharingMode;
        const VkBufferCreateInfo rawCreateInfo = createInfo;

        VmaAllocationCreateInfo allocationInfo{};
        allocationInfo.usage = specification.memoryUsage;
        allocationInfo.flags = specification.allocationFlags;
        if (specification.persistentMapping)
        {
            allocationInfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
            allocationInfo.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        }

        VkBuffer rawBuffer = VK_NULL_HANDLE;
        VmaAllocationInfo allocationResult{};
        if (vmaCreateBuffer(device->GetAllocator(), &rawCreateInfo, &allocationInfo, &rawBuffer,
                            &m_Allocation, &allocationResult) != VK_SUCCESS)
            throw std::runtime_error("Failed to create VMA-backed Vulkan buffer");

        m_Buffer = rawBuffer;
        if (specification.persistentMapping)
            m_MappedData = allocationResult.pMappedData;
        SetDebugName(specification.debugName);
    }

    void VulkanBuffer::Destroy()
    {
        if (!m_Device)
            return;
        if (m_MappedData && !(m_Specification.persistentMapping))
            Unmap();
        if (m_Allocation)
            vmaDestroyBuffer(m_Device->GetAllocator(), m_Buffer, m_Allocation);
        m_Buffer = nullptr;
        m_Allocation = nullptr;
        m_MappedData = nullptr;
        m_Device.reset();
    }

    void VulkanBuffer::Resize(const vk::DeviceSize size)
    {
        if (size == 0)
            throw std::invalid_argument("VulkanBuffer size must be greater than zero");
        auto specification = m_Specification;
        specification.size = size;
        Create(m_Device, specification);
    }

    void VulkanBuffer::SetDebugName(std::string name)
    {
        if (!name.empty() && m_Allocation)
            vmaSetAllocationName(m_Device->GetAllocator(), m_Allocation, name.c_str());
    }

    void* VulkanBuffer::Map()
    {
        if (!m_Device || !m_Allocation)
            throw std::runtime_error("Cannot map an uninitialized Vulkan buffer");
        if (!m_MappedData)
        {
            if (vmaMapMemory(m_Device->GetAllocator(), m_Allocation, &m_MappedData) != VK_SUCCESS)
                throw std::runtime_error("Failed to map Vulkan buffer memory");
        }
        return m_MappedData;
    }

    void VulkanBuffer::Unmap()
    {
        if (m_Device && m_Allocation && m_MappedData && !m_Specification.persistentMapping)
        {
            vmaUnmapMemory(m_Device->GetAllocator(), m_Allocation);
            m_MappedData = nullptr;
        }
    }

    void VulkanBuffer::Flush(const vk::DeviceSize offset, const vk::DeviceSize size)
    {
        if (!m_Device || !m_Allocation)
            throw std::runtime_error("Cannot flush an uninitialized Vulkan buffer");
        if (vmaFlushAllocation(m_Device->GetAllocator(), m_Allocation, offset, size) != VK_SUCCESS)
            throw std::runtime_error("Failed to flush Vulkan buffer memory");
    }

    void VulkanBuffer::Invalidate(const vk::DeviceSize offset, const vk::DeviceSize size)
    {
        if (!m_Device || !m_Allocation)
            throw std::runtime_error("Cannot invalidate an uninitialized Vulkan buffer");
        if (vmaInvalidateAllocation(m_Device->GetAllocator(), m_Allocation, offset, size) != VK_SUCCESS)
            throw std::runtime_error("Failed to invalidate Vulkan buffer memory");
    }

    void VulkanBuffer::Upload(const void* data, const vk::DeviceSize size,
                              const vk::DeviceSize offset, const bool flush)
    {
        if (!data || size == 0 || offset + size > m_Specification.size)
            throw std::invalid_argument("VulkanBuffer::Upload range is outside the buffer");
        std::memcpy(static_cast<std::byte*>(Map()) + offset, data, size);
        if (flush)
            Flush(offset, size);
        if (!m_Specification.persistentMapping)
            Unmap();
    }

    void VulkanBuffer::Read(void* destination, const vk::DeviceSize size,
                            const vk::DeviceSize offset, const bool invalidate)
    {
        if (!destination || size == 0 || offset + size > m_Specification.size)
            throw std::invalid_argument("VulkanBuffer::Read range is outside the buffer");
        if (invalidate)
            Invalidate(offset, size);
        std::memcpy(destination, static_cast<const std::byte*>(Map()) + offset, size);
        if (!m_Specification.persistentMapping)
            Unmap();
    }

    vk::DeviceAddress VulkanBuffer::GetDeviceAddress() const
    {
        if (!m_Device || !m_Buffer)
            return 0;
        vk::BufferDeviceAddressInfo info{m_Buffer};
        return m_Device->GetDevice().getBufferAddress(info);
    }

    VmaAllocationInfo VulkanBuffer::GetAllocationInfo() const
    {
        if (!m_Device || !m_Allocation)
            return {};
        VmaAllocationInfo info{};
        vmaGetAllocationInfo(m_Device->GetAllocator(), m_Allocation, &info);
        return info;
    }

    vk::DescriptorBufferInfo VulkanBuffer::GetDescriptorInfo(const vk::DeviceSize offset,
                                                             const vk::DeviceSize range) const
    {
        if (offset > m_Specification.size)
            throw std::invalid_argument("VulkanBuffer descriptor offset is outside the buffer");
        const vk::DeviceSize available = m_Specification.size - offset;
        return {m_Buffer, offset, range == VK_WHOLE_SIZE ? available : range};
    }
}
