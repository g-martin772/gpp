module;
#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
export module GPP.Graphics:Vulkan.Buffer;

import std;
import GPP.Core;
import :Vulkan.Device;

namespace GPP
{
    export struct VulkanBufferSpecification
    {
        vk::DeviceSize size = 0;
        vk::BufferUsageFlags usage{};
        vk::BufferCreateFlags flags{};
        vk::SharingMode sharingMode = vk::SharingMode::eExclusive;
        VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_AUTO;
        VmaAllocationCreateFlags allocationFlags{};
        bool persistentMapping = false;
        std::string debugName;
    };

    export VulkanBufferSpecification MakeVertexBufferSpecification(
        vk::DeviceSize size, bool hostVisible = false);
    export VulkanBufferSpecification MakeIndexBufferSpecification(
        vk::DeviceSize size, bool hostVisible = false);
    export VulkanBufferSpecification MakeUniformBufferSpecification(
        vk::DeviceSize size, bool hostVisible = true);
    export VulkanBufferSpecification MakeStorageBufferSpecification(
        vk::DeviceSize size, bool hostVisible = false);
    export VulkanBufferSpecification MakeStagingBufferSpecification(vk::DeviceSize size);

    export void CopyBuffer(vk::CommandBuffer commandBuffer, vk::Buffer source,
                           vk::Buffer destination, vk::DeviceSize size,
                           vk::DeviceSize sourceOffset = 0,
                           vk::DeviceSize destinationOffset = 0);
    export void FillBuffer(vk::CommandBuffer commandBuffer, vk::Buffer buffer,
                           uint32_t value, vk::DeviceSize size = VK_WHOLE_SIZE,
                           vk::DeviceSize offset = 0);
    export void UpdateBuffer(vk::CommandBuffer commandBuffer, vk::Buffer buffer,
                             const void* data, vk::DeviceSize size,
                             vk::DeviceSize offset = 0);
    export void BindVertexBuffer(vk::CommandBuffer commandBuffer, vk::Buffer buffer,
                                 vk::DeviceSize offset = 0, uint32_t binding = 0);
    export void BindIndexBuffer(vk::CommandBuffer commandBuffer, vk::Buffer buffer,
                                vk::DeviceSize offset = 0,
                                vk::IndexType indexType = vk::IndexType::eUint32);

    export class VulkanBuffer
    {
    public:
        VulkanBuffer() = default;
        VulkanBuffer(const std::shared_ptr<VulkanDevice>& device,
                     const VulkanBufferSpecification& specification,
                     const std::shared_ptr<Logger>& logger = nullptr);
        ~VulkanBuffer();
        VulkanBuffer(const VulkanBuffer&) = delete;
        VulkanBuffer& operator=(const VulkanBuffer&) = delete;
        VulkanBuffer(VulkanBuffer&& other) noexcept;
        VulkanBuffer& operator=(VulkanBuffer&& other) noexcept;

        void Create(const std::shared_ptr<VulkanDevice>& device,
                    const VulkanBufferSpecification& specification);
        void Destroy();
        void Resize(vk::DeviceSize size);
        void SetDebugName(std::string name);

        void* Map();
        void Unmap();
        void Flush(vk::DeviceSize offset = 0, vk::DeviceSize size = VK_WHOLE_SIZE);
        void Invalidate(vk::DeviceSize offset = 0, vk::DeviceSize size = VK_WHOLE_SIZE);
        void Upload(const void* data, vk::DeviceSize size,
                    vk::DeviceSize offset = 0, bool flush = true);
        void Read(void* destination, vk::DeviceSize size,
                  vk::DeviceSize offset = 0, bool invalidate = true);

        vk::Buffer GetBuffer() const noexcept { return m_Buffer; }
        VmaAllocation GetAllocation() const noexcept { return m_Allocation; }
        vk::DeviceSize GetSize() const noexcept { return m_Specification.size; }
        vk::DeviceAddress GetDeviceAddress() const;
        VmaAllocationInfo GetAllocationInfo() const;
        vk::DescriptorBufferInfo GetDescriptorInfo(vk::DeviceSize offset = 0,
                                                   vk::DeviceSize range = VK_WHOLE_SIZE) const;
        const VulkanBufferSpecification& GetSpecification() const noexcept { return m_Specification; }
        bool IsMapped() const noexcept { return m_MappedData != nullptr; }
        void* GetMappedData() const noexcept { return m_MappedData; }

    private:
        std::shared_ptr<VulkanDevice> m_Device;
        std::shared_ptr<Logger> m_Logger;
        VulkanBufferSpecification m_Specification;
        vk::Buffer m_Buffer{};
        VmaAllocation m_Allocation = nullptr;
        void* m_MappedData = nullptr;
    };
}
