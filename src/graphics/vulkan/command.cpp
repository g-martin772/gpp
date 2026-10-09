module GPP.Graphics;

import vulkan;
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
        if (!m_Device || m_CommandPool == nullptr)
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
            m_CommandBuffer = nullptr;
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
        other.m_CommandBuffer = nullptr;
        other.m_IsSingleUse = false;
        other.m_RenderPassContinue = false;
        other.m_SimultaneousUse = false;
        other.m_CommandPool = nullptr;
        other.m_Device = nullptr;
        other.m_Logger = nullptr;
    }

    VulkanCommandBuffer& VulkanCommandBuffer::operator=(VulkanCommandBuffer&& other) noexcept
    {
        if (this == &other)
            return *this;

        if (m_CommandBuffer != nullptr && m_CommandPool != nullptr && m_Device)
            Free();

        m_CommandBuffer = other.m_CommandBuffer;
        m_IsSingleUse = other.m_IsSingleUse;
        m_RenderPassContinue = other.m_RenderPassContinue;
        m_SimultaneousUse = other.m_SimultaneousUse;
        m_CommandPool = other.m_CommandPool;
        m_Device = std::move(other.m_Device);
        m_Logger = std::move(other.m_Logger);

        other.m_CommandBuffer = nullptr;
        other.m_IsSingleUse = false;
        other.m_RenderPassContinue = false;
        other.m_SimultaneousUse = false;
        other.m_CommandPool = nullptr;
        other.m_Device = nullptr;
        other.m_Logger = nullptr;

        return *this;
    }

    VulkanCommandPool::VulkanCommandPool(const std::shared_ptr<VulkanDevice>& device,
                                         const std::shared_ptr<Logger>& logger,
                                         std::uint32_t queueFamilyIndex,
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
            m_CommandPool = nullptr;
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
        other.m_CommandPool = nullptr;
        other.m_Device = nullptr;
        other.m_Logger = nullptr;
    }

    VulkanCommandPool& VulkanCommandPool::operator=(VulkanCommandPool&& other) noexcept
    {
        if (this == &other)
            return *this;

        if (m_CommandPool != nullptr && m_Device)
        {
            m_Device->GetDevice().destroyCommandPool(m_CommandPool);
            m_CommandPool = nullptr;
        }

        m_Device = std::move(other.m_Device);
        m_Logger = std::move(other.m_Logger);
        m_CommandPool = other.m_CommandPool;

        other.m_CommandPool = nullptr;
        other.m_Device = nullptr;
        other.m_Logger = nullptr;

        return *this;
    }

    VulkanCommandBuffer::~VulkanCommandBuffer()
    {
        if (m_CommandBuffer != nullptr)
            Free();
    }

    VulkanCommandPool::~VulkanCommandPool()
    {
        if (m_CommandPool != nullptr && m_Device)
        {
            m_Device->GetDevice().destroyCommandPool(m_CommandPool);
            m_CommandPool = nullptr;
        }
    }

    void VulkanCommandBuffer::Begin() const
    {
        if (m_CommandBuffer == nullptr || !m_Device)
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
        if (m_CommandBuffer == nullptr || !m_Device)
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
        SubmitInternal(target, nullptr, {}, nullptr, nullptr);
    }

    void VulkanCommandBuffer::Submit(vk::Queue target, vk::Fence fence)
    {
        SubmitInternal(target, nullptr, {}, nullptr, fence);
    }

    void VulkanCommandBuffer::Submit(vk::Queue target,
                                     vk::Semaphore waitSemaphore,
                                     vk::Semaphore signalSemaphore,
                                     vk::Fence fence,
                                     vk::PipelineStageFlags waitStage)
    {
        SubmitInternal(target, waitSemaphore, waitStage, signalSemaphore, fence);
    }

    void VulkanCommandBuffer::SubmitInternal(vk::Queue target, vk::Semaphore waitSemaphore,
                                             vk::PipelineStageFlags waitStage, vk::Semaphore signalSemaphore,
                                             vk::Fence fence)
    {
        if (m_CommandBuffer == nullptr || !m_Device)
        {
            m_Logger->Error("Tried to submit a uninitialized command buffer");
            return;
        }

        const auto device = m_Device->GetDevice();

        vk::Fence ownedFence;
        vk::Fence completionFence = fence;
        if (m_IsSingleUse && !completionFence)
        {
            try
            {
                ownedFence = device.createFence(vk::FenceCreateInfo{});
            }
            catch (const vk::SystemError& err)
            {
                if (m_Logger)
                    m_Logger->Error("Failed to create fence for single-use submit: {}", err.what());
                return;
            }
            completionFence = ownedFence;
        }

        vk::SubmitInfo submitInfo{};
        if (waitSemaphore)
        {
            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores = &waitSemaphore;
            submitInfo.pWaitDstStageMask = &waitStage;
        }
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &m_CommandBuffer;
        if (signalSemaphore)
        {
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores = &signalSemaphore;
        }

        bool submitted = false;
        try
        {
            std::scoped_lock lock(m_Device->GetQueueMutex(target));
            const vk::Result result = target.submit(1, &submitInfo, completionFence);
            submitted = result == vk::Result::eSuccess;
            if (!submitted && m_Logger)
                m_Logger->Error("Failed to submit vulkan command buffer: {}", vk::to_string(result));
        }
        catch (const vk::SystemError& err)
        {
            if (m_Logger)
                m_Logger->Error("Failed to submit vulkan command buffer: {}", err.what());
        }

        if (m_IsSingleUse)
        {
            if (submitted)
            {
                try
                {
                    // waits for this submission only nothing else
                    [[maybe_unused]] const auto waitResult = device.waitForFences(
                        1, &completionFence, vk::True, std::numeric_limits<std::uint64_t>::max());
                }
                catch (const vk::SystemError& err)
                {
                    if (m_Logger)
                        m_Logger->Error("Failed waiting for single-use command buffer: {}", err.what());
                }
            }
            if (ownedFence)
            {
                device.destroyFence(ownedFence);
            }
            Free();
        }
    }

    void VulkanCommandBuffer::Free()
    {
        if (m_CommandBuffer == nullptr || m_CommandPool == nullptr || !m_Device)
            return;

        m_Device->GetDevice().freeCommandBuffers(m_CommandPool, 1, &m_CommandBuffer);
        m_CommandBuffer = nullptr;
    }

    VulkanCommandBuffer VulkanCommandPool::AllocateCommandBuffer(bool renderPassContinue, bool simultaneousUse) const
    {
        return VulkanCommandBuffer(m_CommandPool, m_Device, m_Logger, false, renderPassContinue, simultaneousUse);
    }

    VulkanCommandBuffer VulkanCommandPool::AllocateSingleUseCommandBuffer() const
    {
        return VulkanCommandBuffer(m_CommandPool, m_Device, m_Logger, true, false, false);
    }

    void ImmediateSubmit(const VulkanCommandPool& pool, const vk::Queue queue,
                        const std::function<void(vk::CommandBuffer)>& record)
    {
        auto commandBuffer = pool.AllocateSingleUseCommandBuffer();
        commandBuffer.Begin();
        record(commandBuffer.GetCommandBuffer());
        commandBuffer.End();
        commandBuffer.Submit(queue);
    }
}
