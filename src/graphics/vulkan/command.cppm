module;
#include <vulkan/vulkan.hpp>
export module GPP.Graphics:Vulkan.Command;

import std;
import GPP.Core;
import :Vulkan.Device;

namespace GPP
{
    export class VulkanCommandBuffer
    {
    public:
        VulkanCommandBuffer(vk::CommandPool pool,
                            const std::shared_ptr<VulkanDevice>& device,
                            const std::shared_ptr<Logger>& logger,
                            bool singleUse = false,
                            bool renderPassContinue = false,
                            bool simultaneousUse = false);
        ~VulkanCommandBuffer();

        VulkanCommandBuffer(const VulkanCommandBuffer&) = delete;
        VulkanCommandBuffer& operator=(const VulkanCommandBuffer&) = delete;
        VulkanCommandBuffer(VulkanCommandBuffer&& other) noexcept;
        VulkanCommandBuffer& operator=(VulkanCommandBuffer&& other) noexcept;

        void Begin() const;
        void End() const;
        void Submit(vk::Queue target);
        void Submit(vk::Queue target, vk::Fence fence);
        void Submit(vk::Queue target,
                    vk::Semaphore waitSemaphore,
                    vk::Semaphore signalSemaphore,
                    vk::Fence fence,
                    vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput);
        void Free();

        vk::CommandBuffer GetCommandBuffer() const { return m_CommandBuffer; }

    private:
        vk::CommandBuffer m_CommandBuffer = VK_NULL_HANDLE;
        bool m_IsSingleUse = false, m_RenderPassContinue = false, m_SimultaneousUse = false;
        vk::CommandPool m_CommandPool;
        std::shared_ptr<VulkanDevice> m_Device = nullptr;
        std::shared_ptr<Logger> m_Logger = nullptr;
    };

    export class VulkanCommandPool
    {
    public:
        VulkanCommandPool(const std::shared_ptr<VulkanDevice>& device,
                          const std::shared_ptr<Logger>& logger,
                          uint32_t queueFamilyIndex,
                          vk::CommandPoolCreateFlags flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
        ~VulkanCommandPool();

        VulkanCommandPool(const VulkanCommandPool&) = delete;
        VulkanCommandPool& operator=(const VulkanCommandPool&) = delete;
        VulkanCommandPool(VulkanCommandPool&& other) noexcept;
        VulkanCommandPool& operator=(VulkanCommandPool&& other) noexcept;

        VulkanCommandBuffer AllocateCommandBuffer(bool renderPassContinue = false,
                                                  bool simultaneousUse = false) const;
        VulkanCommandBuffer AllocateSingleUseCommandBuffer() const;
        vk::CommandPool GetPool() const { return m_CommandPool; }

    private:
        std::shared_ptr<VulkanDevice> m_Device;
        std::shared_ptr<Logger> m_Logger = nullptr;
        vk::CommandPool m_CommandPool;
    };
}
