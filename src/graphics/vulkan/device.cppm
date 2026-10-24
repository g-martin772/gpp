module;
#include <vk_mem_alloc.h>
export module GPP.Graphics:Vulkan.Device;

import std;
import vulkan;
import GPP.Core;
import :Vulkan.Context;
export import :Vulkan.Capabilities;

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

    export struct VulkanMemoryStats
    {
        uint32_t allocationCount = 0;
        uint32_t blockCount = 0;
        vk::DeviceSize allocationBytes = 0;
        vk::DeviceSize blockBytes = 0;
    };

    export [[nodiscard]] bool IsPipelineCacheDataCompatible(
        std::span<const std::byte> data, const vk::PhysicalDeviceProperties& properties);

    export class VulkanDevice
    {
    public:
        VulkanDevice(const DeviceRequirements& requirements,
                     const std::shared_ptr<VulkanContext>& context,
                     const std::shared_ptr<Logger>& logger,
                     std::filesystem::path pipelineCachePath = {});
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
        [[nodiscard]] bool SupportsShaderFloat64() const noexcept { return m_Capabilities.ShaderFloat64; }
        [[nodiscard]] const GpuCapabilities& GetCapabilities() const noexcept { return m_Capabilities; }
        VmaAllocator GetAllocator() const noexcept { return m_Allocator; }
        vk::PipelineCache GetPipelineCache() const noexcept { return m_PipelineCache; }
        void SavePipelineCache() const;
        VulkanMemoryStats GetMemoryStats() const;
        void DumpMemoryStats() const;

        vk::Queue GetGraphicsQueue() const { return m_Queues[m_GraphicsIndex]; }
        vk::Queue GetTransferQueue() const { return m_Queues[m_TransferIndex]; }
        vk::Queue GetComputeQueue() const { return m_Queues[m_ComputeIndex]; }
        vk::Queue GetSparseQueue() const { return m_Queues[m_SparseIndex]; }
        vk::Queue GetPresentQueue() const { return m_Queues[m_PresentIndex]; }

        vk::Queue GetBackgroundQueue() const { return m_BackgroundQueue ? m_BackgroundQueue : GetGraphicsQueue(); }
        [[nodiscard]] bool HasDedicatedBackgroundQueue() const noexcept
        {
            return m_BackgroundQueue && m_BackgroundQueue != GetGraphicsQueue();
        }

        [[nodiscard]] std::mutex& GetQueueMutex(vk::Queue queue) const;
        [[nodiscard]] std::vector<vk::Queue> GetUniqueQueues() const;

        [[nodiscard]] const vk::detail::DispatchLoaderDynamic& GetDispatcher() const noexcept { return m_Dispatcher; }

    private:
        void PickPhysicalDevice();
        void CreatePipelineCache();
        std::vector<std::byte> ReadPipelineCacheFile() const;
        DeviceRequirements m_Requirements = {};
        std::shared_ptr<VulkanContext> m_VulkanContext{nullptr};
        std::shared_ptr<Logger> m_Logger{nullptr};
        vk::Device m_Device;
        vk::PhysicalDevice m_PhysicalDevice;
        vk::detail::DispatchLoaderDynamic m_Dispatcher;
        VulkanQueueIndices m_QueueIndices;
        std::vector<vk::Queue> m_Queues;
        vk::Queue m_BackgroundQueue;
        std::vector<std::pair<vk::Queue, std::unique_ptr<std::mutex>>> m_QueueMutexes;
        uint32_t m_GraphicsIndex = 0, m_TransferIndex = 0, m_ComputeIndex = 0,
                 m_SparseIndex = 0, m_PresentIndex = 0;
        vk::Format m_DepthFormat = vk::Format::eD24UnormS8Uint;
        vk::SurfaceCapabilitiesKHR m_SurfaceCapabilities;
        std::vector<vk::SurfaceFormatKHR> m_SurfaceFormats;
        std::vector<vk::PresentModeKHR> m_SurfacePresentModes;
        VmaAllocator m_Allocator = nullptr;
        vk::PipelineCache m_PipelineCache;
        std::filesystem::path m_PipelineCachePath;
        GpuCapabilities m_Capabilities;
    };

    export class QueueIdleFence
    {
    public:
        explicit QueueIdleFence(VulkanDevice& device);
        ~QueueIdleFence();

        QueueIdleFence(const QueueIdleFence&) = delete;
        QueueIdleFence& operator=(const QueueIdleFence&) = delete;

        [[nodiscard]] bool Signaled() const;

    private:
        vk::Device m_Device;
        std::vector<vk::Fence> m_Fences;
    };

    export class RetiredObjects
    {
    public:
        explicit RetiredObjects(std::shared_ptr<VulkanDevice> device,
                                std::chrono::milliseconds grace = std::chrono::milliseconds(2000));
        ~RetiredObjects();

        RetiredObjects(const RetiredObjects&) = delete;
        RetiredObjects& operator=(const RetiredObjects&) = delete;

        void Retire(std::shared_ptr<void> object);
        void Collect(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());
        [[nodiscard]] std::size_t Pending() const noexcept { return m_Pending.load(std::memory_order_relaxed); }

    private:
        struct Entry
        {
            std::shared_ptr<void> Object;
            std::chrono::steady_clock::time_point RetiredAt;
            std::unique_ptr<QueueIdleFence> Fence;
        };

        std::shared_ptr<VulkanDevice> m_Device;
        std::chrono::milliseconds m_Grace;
        std::mutex m_Mutex;
        std::deque<Entry> m_Entries;
        std::atomic<std::size_t> m_Pending{0};
    };

    export std::shared_ptr<VulkanDevice> CreateHeadlessDevice(const std::shared_ptr<Logger>& logger);

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
