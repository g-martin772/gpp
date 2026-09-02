module;
#include <vulkan/vulkan.hpp>
module GPP.Graphics;

import std;
import GPP.Core;
import :Vulkan.Command;

namespace GPP
{
    VulkanCommandBuffer::VulkanCommandBuffer(vk::CommandPool pool,
                                             const std::shared_ptr<VulkanDevice>& device,
                                             const std::shared_ptr<Logger>& logger,
                                             bool singleUse,
                                             bool renderPassContinue,
                                             bool simultaneousUse)
        : m_CommandPool(pool),
          m_Device(device),
          m_Logger(logger),
          m_IsSingleUse(singleUse),
          m_RenderPassContinue(renderPassContinue),
          m_SimultaneousUse(simultaneousUse)
    {
        if (!m_Device || m_CommandPool == VK_NULL_HANDLE)
            throw std::invalid_argument("VulkanCommandBuffer::VulkanCommandBuffer: invalid device or pool");

        vk::CommandBufferAllocateInfo allocateInfo{};
        allocateInfo.level = vk::CommandBufferLevel::ePrimary;
        allocateInfo.commandBufferCount = 1;
        allocateInfo.commandPool = m_CommandPool;

        try
        {
            m_CommandBuffer = m_Device->GetDevice().allocateCommandBuffers(allocateInfo)[0];
        }
        catch (const vk::SystemError& err)
        {
            if (m_Logger)
                m_Logger->Error("Failed to allocate vulkan command buffer: {}", err.what());
            m_CommandBuffer = VK_NULL_HANDLE;
        }
    }

    VulkanCommandBuffer::VulkanCommandBuffer(VulkanCommandBuffer&& other) noexcept
        : m_CommandBuffer(other.m_CommandBuffer),
          m_IsSingleUse(other.m_IsSingleUse),
          m_RenderPassContinue(other.m_RenderPassContinue),
          m_SimultaneousUse(other.m_SimultaneousUse),
          m_CommandPool(other.m_CommandPool),
          m_Device(std::move(other.m_Device)),
          m_Logger(std::move(other.m_Logger))
    {
        other.m_CommandBuffer = VK_NULL_HANDLE;
        other.m_IsSingleUse = false;
        other.m_RenderPassContinue = false;
        other.m_SimultaneousUse = false;
        other.m_CommandPool = VK_NULL_HANDLE;
        other.m_Device = nullptr;
        other.m_Logger = nullptr;
    }

    VulkanCommandBuffer& VulkanCommandBuffer::operator=(VulkanCommandBuffer&& other) noexcept
    {
        if (this == &other)
            return *this;

        if (m_CommandBuffer != VK_NULL_HANDLE && m_CommandPool != VK_NULL_HANDLE && m_Device)
            Free();

        m_CommandBuffer = other.m_CommandBuffer;
        m_IsSingleUse = other.m_IsSingleUse;
        m_RenderPassContinue = other.m_RenderPassContinue;
        m_SimultaneousUse = other.m_SimultaneousUse;
        m_CommandPool = other.m_CommandPool;
        m_Device = std::move(other.m_Device);
        m_Logger = std::move(other.m_Logger);

        other.m_CommandBuffer = VK_NULL_HANDLE;
        other.m_IsSingleUse = false;
        other.m_RenderPassContinue = false;
        other.m_SimultaneousUse = false;
        other.m_CommandPool = VK_NULL_HANDLE;
        other.m_Device = nullptr;
        other.m_Logger = nullptr;

        return *this;
    }

    VulkanCommandPool::VulkanCommandPool(const std::shared_ptr<VulkanDevice>& device,
                                         const std::shared_ptr<Logger>& logger,
                                         uint32_t queueFamilyIndex,
                                         vk::CommandPoolCreateFlags flags)
        : m_Device(device),
          m_Logger(logger)
    {
        vk::CommandPoolCreateInfo createInfo{};
        createInfo.flags = flags;
        createInfo.queueFamilyIndex = queueFamilyIndex;

        try
        {
            m_CommandPool = m_Device->GetDevice().createCommandPool(createInfo);
        }
        catch (const vk::SystemError& err)
        {
            if (m_Logger)
                m_Logger->Error("Failed to create vulkan command pool: {}", err.what());
            m_CommandPool = VK_NULL_HANDLE;
            return;
        }

        if (m_Logger)
            m_Logger->Trace("Created vulkan command pool: successful");
    }

    VulkanCommandPool::VulkanCommandPool(VulkanCommandPool&& other) noexcept
        : m_Device(std::move(other.m_Device)),
          m_Logger(std::move(other.m_Logger)),
          m_CommandPool(other.m_CommandPool)
    {
        other.m_CommandPool = VK_NULL_HANDLE;
        other.m_Device = nullptr;
        other.m_Logger = nullptr;
    }

    VulkanCommandPool& VulkanCommandPool::operator=(VulkanCommandPool&& other) noexcept
    {
        if (this == &other)
            return *this;

        if (m_CommandPool != VK_NULL_HANDLE && m_Device)
        {
            m_Device->GetDevice().destroyCommandPool(m_CommandPool);
            m_CommandPool = VK_NULL_HANDLE;
        }

        m_Device = std::move(other.m_Device);
        m_Logger = std::move(other.m_Logger);
        m_CommandPool = other.m_CommandPool;

        other.m_CommandPool = VK_NULL_HANDLE;
        other.m_Device = nullptr;
        other.m_Logger = nullptr;

        return *this;
    }

    VulkanCommandBuffer::~VulkanCommandBuffer()
    {
        if (m_CommandBuffer != VK_NULL_HANDLE)
            Free();
    }

    VulkanCommandPool::~VulkanCommandPool()
    {
        if (m_CommandPool != VK_NULL_HANDLE && m_Device)
        {
            m_Device->GetDevice().destroyCommandPool(m_CommandPool);
            m_CommandPool = VK_NULL_HANDLE;
        }
    }

    void VulkanCommandBuffer::Begin() const
    {
        if (m_CommandBuffer == VK_NULL_HANDLE || !m_Device)
        {
            m_Logger->Error("Tried to begin a uninitialized command buffer");
            return;
        }

        vk::CommandBufferBeginInfo beginInfo{};
        if (m_IsSingleUse)
            beginInfo.flags |= vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        if (m_RenderPassContinue)
            beginInfo.flags |= vk::CommandBufferUsageFlagBits::eRenderPassContinue;
        if (m_SimultaneousUse)
            beginInfo.flags |= vk::CommandBufferUsageFlagBits::eSimultaneousUse;
        try
        {
            m_CommandBuffer.begin(beginInfo);
        }
        catch (const vk::SystemError& err)
        {
            if (m_Logger)
                m_Logger->Error("Failed to begin vulkan command buffer: {}", err.what());
        }
    }

    void VulkanCommandBuffer::End() const
    {
        if (m_CommandBuffer == VK_NULL_HANDLE || !m_Device)
        {
            m_Logger->Error("Tried to end a uninitialized command buffer");
            return;
        }

        try
        {
            m_CommandBuffer.end();
        }
        catch (const vk::SystemError& err)
        {
            if (m_Logger)
                m_Logger->Error("Failed to end vulkan command buffer: {}", err.what());
        }
    }

    void VulkanCommandBuffer::Submit(vk::Queue target)
    {
        if (m_CommandBuffer == VK_NULL_HANDLE || !m_Device)
        {
            m_Logger->Error("Tried to submit a uninitialized command buffer");
            return;
        }

        vk::SubmitInfo submitInfo{};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &m_CommandBuffer;

        try
        {
            const vk::Result result = target.submit(1, &submitInfo, nullptr);
            if (result != vk::Result::eSuccess)
            {
                if (m_Logger)
                    m_Logger->Error("Failed to submit vulkan command buffer: {}", vk::to_string(result));
                return;
            }
        }
        catch (const vk::SystemError& err)
        {
            if (m_Logger)
                m_Logger->Error("Failed to submit vulkan command buffer: {}", err.what());
            return;
        }

        if (m_IsSingleUse)
        {
            target.waitIdle();
            Free();
        }
    }

    void VulkanCommandBuffer::Submit(vk::Queue target, vk::Fence fence)
    {
        if (m_CommandBuffer == VK_NULL_HANDLE || !m_Device)
        {
            m_Logger->Error("Tried to submit a uninitialized command buffer");
            return;
        }

        vk::SubmitInfo submitInfo{};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &m_CommandBuffer;

        try
        {
            const vk::Result result = target.submit(1, &submitInfo, fence);
            if (result != vk::Result::eSuccess)
            {
                if (m_Logger)
                    m_Logger->Error("Failed to submit vulkan command buffer: {}", vk::to_string(result));
                return;
            }
        }
        catch (const vk::SystemError& err)
        {
            if (m_Logger)
                m_Logger->Error("Failed to submit vulkan command buffer: {}", err.what());
        }

        if (m_IsSingleUse)
        {
            target.waitIdle();
            Free();
        }
    }

    void VulkanCommandBuffer::Submit(vk::Queue target,
                                     vk::Semaphore waitSemaphore,
                                     vk::Semaphore signalSemaphore,
                                     vk::Fence fence,
                                     vk::PipelineStageFlags waitStage)
    {
        if (m_CommandBuffer == VK_NULL_HANDLE || !m_Device)
        {
            m_Logger->Error("Tried to submit a uninitialized command buffer");
            return;
        }

        vk::SubmitInfo submitInfo{};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &waitSemaphore;
        submitInfo.pWaitDstStageMask = &waitStage;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &m_CommandBuffer;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &signalSemaphore;

        try
        {
            const vk::Result result = target.submit(1, &submitInfo, fence);
            if (result != vk::Result::eSuccess)
            {
                if (m_Logger)
                    m_Logger->Error("Failed to submit vulkan command buffer: {}", vk::to_string(result));
                return;
            }
        }
        catch (const vk::SystemError& err)
        {
            if (m_Logger)
                m_Logger->Error("Failed to submit vulkan command buffer: {}", err.what());
        }

        if (m_IsSingleUse)
        {
            target.waitIdle();
            Free();
        }
    }

    void VulkanCommandBuffer::Free()
    {
        if (m_CommandBuffer == VK_NULL_HANDLE || m_CommandPool == VK_NULL_HANDLE || !m_Device)
            return;

        m_Device->GetDevice().freeCommandBuffers(m_CommandPool, 1, &m_CommandBuffer);
        m_CommandBuffer = VK_NULL_HANDLE;
    }

    VulkanCommandBuffer VulkanCommandPool::AllocateCommandBuffer(bool renderPassContinue, bool simultaneousUse) const
    {
        return VulkanCommandBuffer(m_CommandPool, m_Device, m_Logger, false, renderPassContinue, simultaneousUse);
    }

    VulkanCommandBuffer VulkanCommandPool::AllocateSingleUseCommandBuffer() const
    {
        return VulkanCommandBuffer(m_CommandPool, m_Device, m_Logger, true, false, false);
    }
}
