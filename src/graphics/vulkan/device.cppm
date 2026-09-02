module;
#include <vulkan/vulkan.hpp>
export module GPP.Graphics:Vulkan.Device;

import std;
import GPP.Core;
import :Vulkan.Context;

namespace GPP
{
    export struct DeviceRequirements
    {
        bool Graphics, Compute, Transfer, Sparse, Present;
        vk::SurfaceKHR Surface;
    };

    export struct VulkanQueueIndices
    {
        uint32_t Graphics = -1, Compute = -1, Transfer = -1, Sparse = -1, Present = -1;
    };

    export class VulkanDevice
    {
    public:
        VulkanDevice(const DeviceRequirements& requirements,
                     const std::shared_ptr<VulkanContext>& context,
                     const std::shared_ptr<Logger>& logger);
        ~VulkanDevice();

        void WaitIdle();

        vk::Instance GetInstance() const { return m_VulkanContext->GetInstance(); }
        vk::Device GetDevice() const { return m_Device; }
        vk::PhysicalDevice GetPhysicalDevice() const { return m_PhysicalDevice; }
        DeviceRequirements GetRequirements() const { return m_Requirements; }
        VulkanQueueIndices GetQueueIndices() const { return m_QueueIndices; }
        vk::Format GetDepthFormat() const { return m_DepthFormat; }
        vk::SurfaceCapabilitiesKHR GetSurfaceCapabilities() const { return m_SurfaceCapabilities; }
        std::vector<vk::SurfaceFormatKHR> GetSurfaceFormats() const { return m_SurfaceFormats; }
        std::vector<vk::PresentModeKHR> GetSurfacePresentModes() const { return m_SurfacePresentModes; }

        vk::Queue GetGraphicsQueue() const { return m_Queues[m_GraphicsIndex]; }
        vk::Queue GetTransferQueue() const { return m_Queues[m_TransferIndex]; }
        vk::Queue GetComputeQueue() const { return m_Queues[m_ComputeIndex]; }
        vk::Queue GetSparseQueue() const { return m_Queues[m_SparseIndex]; }
        vk::Queue GetPresentQueue() const { return m_Queues[m_PresentIndex]; }

        //const vk::detail::DispatchLoaderDynamic& GetDispatcher() const { return m_Dispatcher; }

    private:
        void PickPhysicalDevice();
        DeviceRequirements m_Requirements = {};
        std::shared_ptr<VulkanContext> m_VulkanContext{nullptr};
        std::shared_ptr<Logger> m_Logger{nullptr};
        vk::Device m_Device;
        vk::PhysicalDevice m_PhysicalDevice;
        //vk::detail::DispatchLoaderDynamic m_Dispatcher;
        VulkanQueueIndices m_QueueIndices;
        std::vector<vk::Queue> m_Queues;
        uint32_t m_GraphicsIndex = 0, m_TransferIndex = 0, m_ComputeIndex = 0,
                 m_SparseIndex = 0, m_PresentIndex = 0;
        vk::Format m_DepthFormat = vk::Format::eD24UnormS8Uint;
        vk::SurfaceCapabilitiesKHR m_SurfaceCapabilities;
        std::vector<vk::SurfaceFormatKHR> m_SurfaceFormats;
        std::vector<vk::PresentModeKHR> m_SurfacePresentModes;
    };

    class VulkanFence {
    public:
        VulkanFence(vk::Device device, bool isSignaled = true);
        ~VulkanFence();

        VulkanFence(const VulkanFence&) = delete;
        VulkanFence& operator=(const VulkanFence&) = delete;

        VulkanFence(VulkanFence&& other) noexcept
            : m_Fence(other.m_Fence), m_Device(other.m_Device)
        { other.m_Fence = VK_NULL_HANDLE; other.m_Device = nullptr; }

        VulkanFence& operator=(VulkanFence&& other) noexcept {
            if (this != &other) {
                if (m_Fence && m_Device) m_Device.destroyFence(m_Fence);
                m_Fence = other.m_Fence; m_Device = other.m_Device;
                other.m_Fence = VK_NULL_HANDLE; other.m_Device = nullptr;
            }
            return *this;
        }

        void Reset() const;
        void Wait() const;
        void WaitAndReset() const;

        vk::Fence GetFence() const { return m_Fence; }
    private:
        vk::Fence m_Fence;
        vk::Device m_Device = nullptr;
    };

    class VulkanSemaphore {
    public:
        VulkanSemaphore(vk::Device device);
        ~VulkanSemaphore();

        VulkanSemaphore(const VulkanSemaphore&) = delete;
        VulkanSemaphore& operator=(const VulkanSemaphore&) = delete;

        VulkanSemaphore(VulkanSemaphore&& other) noexcept
            : m_Semaphore(other.m_Semaphore), m_Device(other.m_Device)
        { other.m_Semaphore = VK_NULL_HANDLE; other.m_Device = nullptr; }

        VulkanSemaphore& operator=(VulkanSemaphore&& other) noexcept {
            if (this != &other) {
                if (m_Semaphore && m_Device) m_Device.destroySemaphore(m_Semaphore);
                m_Semaphore = other.m_Semaphore; m_Device = other.m_Device;
                other.m_Semaphore = VK_NULL_HANDLE; other.m_Device = nullptr;
            }
            return *this;
        }

        vk::Semaphore GetSemaphore() const { return m_Semaphore; }

    private:
        vk::Semaphore m_Semaphore;
        vk::Device m_Device = nullptr;
    };

}
