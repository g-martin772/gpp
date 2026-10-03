module;
#include <vk_mem_alloc.h>
export module GPP.Graphics:Vulkan.Descriptor;

import std;
import vulkan;
import GPP.Core;
import :Vulkan.Device;

namespace GPP
{
    export struct VulkanDescriptorLayoutBinding
    {
        std::uint32_t binding = 0;
        vk::DescriptorType type = vk::DescriptorType::eUniformBuffer;
        std::uint32_t count = 1;
    };

    export class VulkanDescriptorAllocator
    {
    public:
        VulkanDescriptorAllocator() = default;
        explicit VulkanDescriptorAllocator(std::shared_ptr<VulkanDevice> device,
                                           std::uint32_t maxSetsPerPool = 1000);
        ~VulkanDescriptorAllocator();

        VulkanDescriptorAllocator(const VulkanDescriptorAllocator&) = delete;
        VulkanDescriptorAllocator& operator=(const VulkanDescriptorAllocator&) = delete;
        VulkanDescriptorAllocator(VulkanDescriptorAllocator&& other) noexcept;
        VulkanDescriptorAllocator& operator=(VulkanDescriptorAllocator&& other) noexcept;

        [[nodiscard]] vk::DescriptorSet Allocate(vk::DescriptorSetLayout layout);

        void ResetPools();

    private:
        [[nodiscard]] vk::DescriptorPool GrabPool();
        [[nodiscard]] vk::DescriptorPool CreatePool() const;
        void DestroyAllPools();

        std::shared_ptr<VulkanDevice> m_Device;
        std::uint32_t m_MaxSetsPerPool = 1000;
        vk::DescriptorPool m_CurrentPool{};
        std::vector<vk::DescriptorPool> m_UsedPools;
        std::vector<vk::DescriptorPool> m_FreePools;
    };

    export class DescriptorSetWriter
    {
    public:
        explicit DescriptorSetWriter(vk::Device device) noexcept : m_Device(device) {}

        DescriptorSetWriter& WriteStorageImage(vk::DescriptorSet set, std::uint32_t binding,
                                               vk::ImageView view,
                                               vk::ImageLayout layout = vk::ImageLayout::eGeneral);
        DescriptorSetWriter& WriteCombinedImageSampler(vk::DescriptorSet set, std::uint32_t binding,
                                                       vk::ImageView view, vk::Sampler sampler,
                                                       vk::ImageLayout layout =
                                                           vk::ImageLayout::eShaderReadOnlyOptimal);
        DescriptorSetWriter& WriteUniformBuffer(vk::DescriptorSet set, std::uint32_t binding,
                                                vk::Buffer buffer, vk::DeviceSize offset = 0,
                                                vk::DeviceSize range = VK_WHOLE_SIZE);
        DescriptorSetWriter& WriteStorageBuffer(vk::DescriptorSet set, std::uint32_t binding,
                                                vk::Buffer buffer, vk::DeviceSize offset = 0,
                                                vk::DeviceSize range = VK_WHOLE_SIZE);
        void Update();

    private:
        vk::Device m_Device;
        std::deque<vk::DescriptorImageInfo> m_ImageInfos;
        std::deque<vk::DescriptorBufferInfo> m_BufferInfos;
        std::vector<vk::WriteDescriptorSet> m_Writes;
    };
}
