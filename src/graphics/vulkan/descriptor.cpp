module;
#include <vk_mem_alloc.h>
module GPP.Graphics;

import vulkan;
import GPP.Core;
import :Vulkan.Descriptor;

namespace GPP
{
    namespace
    {
        struct PoolSizeRatio
        {
            vk::DescriptorType type;
            float ratio;
        };

        constexpr std::array<PoolSizeRatio, 8> kPoolSizeRatios{{
            {vk::DescriptorType::eSampler, 0.5f},
            {vk::DescriptorType::eCombinedImageSampler, 4.0f},
            {vk::DescriptorType::eSampledImage, 4.0f},
            {vk::DescriptorType::eStorageImage, 1.0f},
            {vk::DescriptorType::eUniformTexelBuffer, 1.0f},
            {vk::DescriptorType::eStorageTexelBuffer, 1.0f},
            {vk::DescriptorType::eUniformBuffer, 2.0f},
            {vk::DescriptorType::eStorageBuffer, 2.0f},
        }};
    }

    VulkanDescriptorAllocator::VulkanDescriptorAllocator(std::shared_ptr<VulkanDevice> device,
                                                         const std::uint32_t maxSetsPerPool)
        : m_Device(std::move(device)), m_MaxSetsPerPool(maxSetsPerPool)
    {
        if (!m_Device)
        {
            throw std::invalid_argument("VulkanDescriptorAllocator requires a valid VulkanDevice.");
        }
    }

    VulkanDescriptorAllocator::~VulkanDescriptorAllocator()
    {
        DestroyAllPools();
    }

    VulkanDescriptorAllocator::VulkanDescriptorAllocator(VulkanDescriptorAllocator&& other) noexcept
        : m_Device(std::move(other.m_Device)), m_MaxSetsPerPool(other.m_MaxSetsPerPool),
          m_CurrentPool(other.m_CurrentPool), m_UsedPools(std::move(other.m_UsedPools)),
          m_FreePools(std::move(other.m_FreePools))
    {
        other.m_CurrentPool = nullptr;
    }

    VulkanDescriptorAllocator& VulkanDescriptorAllocator::operator=(VulkanDescriptorAllocator&& other) noexcept
    {
        if (this == &other) return *this;
        DestroyAllPools();
        m_Device = std::move(other.m_Device);
        m_MaxSetsPerPool = other.m_MaxSetsPerPool;
        m_CurrentPool = other.m_CurrentPool;
        m_UsedPools = std::move(other.m_UsedPools);
        m_FreePools = std::move(other.m_FreePools);
        other.m_CurrentPool = nullptr;
        return *this;
    }

    vk::DescriptorPool VulkanDescriptorAllocator::CreatePool() const
    {
        std::vector<vk::DescriptorPoolSize> sizes;
        sizes.reserve(kPoolSizeRatios.size());
        for (const auto& [type, ratio] : kPoolSizeRatios)
        {
            sizes.emplace_back(type,
                               static_cast<std::uint32_t>(ratio * static_cast<float>(m_MaxSetsPerPool)));
        }
        vk::DescriptorPoolCreateInfo info{};
        info.maxSets = m_MaxSetsPerPool;
        info.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
        info.pPoolSizes = sizes.data();
        info.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        return m_Device->GetDevice().createDescriptorPool(info);
    }

    vk::DescriptorPool VulkanDescriptorAllocator::GrabPool()
    {
        if (!m_FreePools.empty())
        {
            const auto pool = m_FreePools.back();
            m_FreePools.pop_back();
            return pool;
        }
        return CreatePool();
    }

    vk::DescriptorSet VulkanDescriptorAllocator::Allocate(const vk::DescriptorSetLayout layout)
    {
        if (!m_CurrentPool)
        {
            m_CurrentPool = GrabPool();
            m_UsedPools.push_back(m_CurrentPool);
        }

        vk::DescriptorSetAllocateInfo allocateInfo{};
        allocateInfo.descriptorPool = m_CurrentPool;
        allocateInfo.descriptorSetCount = 1;
        allocateInfo.pSetLayouts = &layout;

        vk::DescriptorSet set{};
        auto result = m_Device->GetDevice().allocateDescriptorSets(&allocateInfo, &set);
        if (result == vk::Result::eErrorOutOfPoolMemory || result == vk::Result::eErrorFragmentedPool)
        {
            m_CurrentPool = GrabPool();
            m_UsedPools.push_back(m_CurrentPool);
            allocateInfo.descriptorPool = m_CurrentPool;
            result = m_Device->GetDevice().allocateDescriptorSets(&allocateInfo, &set);
        }
        if (result != vk::Result::eSuccess)
        {
            throw std::runtime_error("Failed to allocate Vulkan descriptor set.");
        }
        return set;
    }

    void VulkanDescriptorAllocator::ResetPools()
    {
        for (const auto pool : m_UsedPools)
        {
            m_Device->GetDevice().resetDescriptorPool(pool);
            m_FreePools.push_back(pool);
        }
        m_UsedPools.clear();
        m_CurrentPool = nullptr;
    }

    void VulkanDescriptorAllocator::DestroyAllPools()
    {
        if (!m_Device) return;
        const auto device = m_Device->GetDevice();
        for (const auto pool : m_UsedPools) device.destroyDescriptorPool(pool);
        for (const auto pool : m_FreePools) device.destroyDescriptorPool(pool);
        m_UsedPools.clear();
        m_FreePools.clear();
        m_CurrentPool = nullptr;
    }

    DescriptorSetWriter& DescriptorSetWriter::WriteStorageImage(
        const vk::DescriptorSet set, const std::uint32_t binding,
        const vk::ImageView view, const vk::ImageLayout layout)
    {
        const auto& info = m_ImageInfos.emplace_back(vk::Sampler{}, view, layout);
        vk::WriteDescriptorSet write{};
        write.dstSet = set;
        write.dstBinding = binding;
        write.descriptorCount = 1;
        write.descriptorType = vk::DescriptorType::eStorageImage;
        write.pImageInfo = &info;
        m_Writes.push_back(write);
        return *this;
    }

    DescriptorSetWriter& DescriptorSetWriter::WriteCombinedImageSampler(
        const vk::DescriptorSet set, const std::uint32_t binding,
        const vk::ImageView view, const vk::Sampler sampler, const vk::ImageLayout layout)
    {
        const auto& info = m_ImageInfos.emplace_back(sampler, view, layout);
        vk::WriteDescriptorSet write{};
        write.dstSet = set;
        write.dstBinding = binding;
        write.descriptorCount = 1;
        write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        write.pImageInfo = &info;
        m_Writes.push_back(write);
        return *this;
    }

    DescriptorSetWriter& DescriptorSetWriter::WriteUniformBuffer(
        const vk::DescriptorSet set, const std::uint32_t binding,
        const vk::Buffer buffer, const vk::DeviceSize offset, const vk::DeviceSize range)
    {
        const auto& info = m_BufferInfos.emplace_back(buffer, offset, range);
        vk::WriteDescriptorSet write{};
        write.dstSet = set;
        write.dstBinding = binding;
        write.descriptorCount = 1;
        write.descriptorType = vk::DescriptorType::eUniformBuffer;
        write.pBufferInfo = &info;
        m_Writes.push_back(write);
        return *this;
    }

    DescriptorSetWriter& DescriptorSetWriter::WriteStorageBuffer(
        const vk::DescriptorSet set, const std::uint32_t binding,
        const vk::Buffer buffer, const vk::DeviceSize offset, const vk::DeviceSize range)
    {
        const auto& info = m_BufferInfos.emplace_back(buffer, offset, range);
        vk::WriteDescriptorSet write{};
        write.dstSet = set;
        write.dstBinding = binding;
        write.descriptorCount = 1;
        write.descriptorType = vk::DescriptorType::eStorageBuffer;
        write.pBufferInfo = &info;
        m_Writes.push_back(write);
        return *this;
    }

    void DescriptorSetWriter::Update()
    {
        if (m_Writes.empty()) return;
        m_Device.updateDescriptorSets(static_cast<std::uint32_t>(m_Writes.size()), m_Writes.data(), 0, nullptr);
        m_Writes.clear();
        m_ImageInfos.clear();
        m_BufferInfos.clear();
    }
}
