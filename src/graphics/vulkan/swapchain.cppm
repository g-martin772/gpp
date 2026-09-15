export module GPP.Graphics:Vulkan.Swapchain;

import glm;
import vulkan;
import GPP.Core;
import :Vulkan.Context;
import :Vulkan.Image;

namespace GPP
{
    class VulkanDevice;

    export class VulkanSwapChain
    {
    public:
        VulkanSwapChain(const std::shared_ptr<VulkanDevice>& device,
                        const std::shared_ptr<Logger>& logger,
                        glm::uvec2 size,
                        vk::SurfaceKHR surface,
                        std::uint32_t framesInFlight = 3);
        ~VulkanSwapChain();

        void AcquireNextImage(vk::Semaphore semaphore, vk::Fence fence, std::uint64_t timeout = -1);
        void Present(vk::Queue presentQueue, vk::Semaphore waitSemaphore);
        void Update(glm::uvec2 size);
        void AdvanceSemaphoreIndex();
        void SetVSync(bool enabled);

        const std::vector<vk::Image>& GetImages() const { return m_Images; }
        const std::vector<vk::ImageView>& GetImageViews() const { return m_Views; }
        //vk::ImageView GetDepthImageView() const { return m_DepthImage.GetImageView(0); }
        vk::Extent2D GetExtent() const { return m_Extent; }

        vk::Format GetImageFormat() const { return m_Format; }
        vk::Format GetDepthImageFormat() const { return m_DepthFormat; }

        std::uint32_t GetImageCount() const { return m_Images.size(); }
        std::uint32_t GetCurrentImageIndex() const { return m_CurrentFrame; }
        std::uint32_t GetSemaphoreIndex() const { return m_SemaphoreIndex; }

        vk::SwapchainKHR GetSwapChain() const { return m_SwapChain; }

    private:
        void CreateSwapChain();
        void DestroySwapChain();

    private:
        glm::vec2 m_Size = {0.0f, 0.0f};
        vk::Extent2D m_Extent = {0, 0};
        vk::SurfaceKHR m_Surface;
        std::uint32_t m_FramesInFlight = 3, m_CurrentFrame = 0, m_SemaphoreIndex = 0;
        bool m_VSync = true;
        std::vector<vk::Image> m_Images{};
        std::vector<vk::ImageView> m_Views{};
        std::shared_ptr<VulkanDevice> m_Device;
        std::shared_ptr<Logger> m_Logger;
        vk::SwapchainKHR m_SwapChain{};
        vk::Format m_Format = vk::Format::eB8G8R8A8Unorm, m_DepthFormat = vk::Format::eD32Sfloat;
        vk::ColorSpaceKHR m_ColorSpace = vk::ColorSpaceKHR::eSrgbNonlinear;
        //VulkanImage2D m_DepthImage;
    };
}
