export module GPP.Graphics:Vulkan.Command;

import vulkan;
import GPP.Core;
import :Vulkan.Device;

namespace GPP
{
    export template <typename T>
    class RecycleList
    {
    public:
        explicit RecycleList(const std::size_t capacity = 16) : m_Capacity(capacity) {}

        [[nodiscard]] std::optional<T> Take()
        {
            std::scoped_lock lock(m_Mutex);
            if (m_Items.empty()) return std::nullopt;
            T item = std::move(m_Items.back());
            m_Items.pop_back();
            return item;
        }

        [[nodiscard]] bool Give(T item)
        {
            std::scoped_lock lock(m_Mutex);
            if (m_Closed || m_Items.size() >= m_Capacity) return false;
            m_Items.push_back(std::move(item));
            return true;
        }

        std::vector<T> Close()
        {
            std::scoped_lock lock(m_Mutex);
            m_Closed = true;
            return std::exchange(m_Items, {});
        }

        [[nodiscard]] bool Closed() const
        {
            std::scoped_lock lock(m_Mutex);
            return m_Closed;
        }

        [[nodiscard]] std::size_t Size() const
        {
            std::scoped_lock lock(m_Mutex);
            return m_Items.size();
        }

    private:
        mutable std::mutex m_Mutex;
        std::vector<T> m_Items;
        std::size_t m_Capacity;
        bool m_Closed = false;
    };

    export struct SubmitState;
    export struct SubmitRecycler;

    export class SubmitHandle
    {
    public:
        SubmitHandle() = default;
        explicit SubmitHandle(std::shared_ptr<SubmitState> state) : m_State(std::move(state)) {}

        [[nodiscard]] bool Valid() const noexcept { return m_State != nullptr; }
        // Polls without blocking and true once the GPU work finished
        [[nodiscard]] bool Done() const;
        void Wait() const;

    private:
        std::shared_ptr<SubmitState> m_State;
    };

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
        void SubmitInternal(vk::Queue target, vk::Semaphore waitSemaphore, vk::PipelineStageFlags waitStage,
                            vk::Semaphore signalSemaphore, vk::Fence fence);

        vk::CommandBuffer m_CommandBuffer = nullptr;
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
                          std::uint32_t queueFamilyIndex,
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
        [[nodiscard]] const std::shared_ptr<SubmitRecycler>& Recycler() const noexcept { return m_Recycler; }

    private:
        void Release();

        std::shared_ptr<VulkanDevice> m_Device;
        std::shared_ptr<Logger> m_Logger = nullptr;
        vk::CommandPool m_CommandPool;
        std::shared_ptr<SubmitRecycler> m_Recycler;
    };

    export [[nodiscard]] SubmitHandle SubmitAsync(const VulkanCommandPool& pool, vk::Queue queue,
                                                  const std::function<void(vk::CommandBuffer)>& record,
                                                  std::vector<std::shared_ptr<void>> keepAlive = {});

    export void ImmediateSubmit(const VulkanCommandPool& pool, vk::Queue queue,
                               const std::function<void(vk::CommandBuffer)>& record);
}
