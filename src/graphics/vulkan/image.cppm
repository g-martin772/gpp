module;
#include <vk_mem_alloc.h>
export module GPP.Graphics:Vulkan.Image;

import vulkan;
import GPP.Core;
import :Vulkan.Device;

namespace GPP
{
    export struct VulkanImageSpecification
    {
        vk::Extent3D extent{1, 1, 1};
        vk::Format format = vk::Format::eR8G8B8A8Unorm;
        vk::ImageType type = vk::ImageType::e2D;
        vk::ImageViewType viewType = vk::ImageViewType::e2D;
        vk::ImageTiling tiling = vk::ImageTiling::eOptimal;
        vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eSampled;
        vk::ImageCreateFlags flags{};
        vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;
        vk::ImageLayout initialLayout = vk::ImageLayout::eUndefined;
        vk::ImageAspectFlags aspectMask{};
        VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        uint32_t mipLevels = 1;
        uint32_t arrayLayers = 1;
        bool createView = true;
        bool createSampler = false;
        vk::Filter samplerFilter = vk::Filter::eLinear;
        vk::SamplerAddressMode samplerAddressMode = vk::SamplerAddressMode::eRepeat;
        std::string debugName;
    };

    export vk::ImageAspectFlags GetImageAspectMask(vk::Format format,
                                                   vk::ImageLayout layout = vk::ImageLayout::eUndefined);
    export vk::ImageView CreateImageView(vk::Device device, vk::Image image, vk::Format format,
                                         vk::ImageViewType viewType, vk::ImageAspectFlags aspectMask,
                                         uint32_t mipLevels = 1, uint32_t arrayLayers = 1);
    export vk::ImageMemoryBarrier2 MakeImageTransition(vk::Image image, vk::Format format,
                                                       vk::ImageLayout oldLayout,
                                                       vk::ImageLayout newLayout,
                                                       uint32_t mipLevels = 1,
                                                       uint32_t arrayLayers = 1,
                                                       vk::ImageAspectFlags aspectMask = {});
    export void TransitionImageLayout(vk::CommandBuffer commandBuffer, vk::Image image,
                                      vk::Format format, vk::ImageLayout oldLayout,
                                      vk::ImageLayout newLayout, uint32_t mipLevels = 1,
                                      uint32_t arrayLayers = 1, vk::ImageAspectFlags aspectMask = {});
    export void CopyBufferToImage(vk::CommandBuffer commandBuffer, vk::Buffer buffer, vk::Image image,
                                  vk::Extent3D extent,
                                  vk::ImageLayout imageLayout = vk::ImageLayout::eTransferDstOptimal);
    export void CopyImage(vk::CommandBuffer commandBuffer, vk::Image source, vk::Image destination,
                          vk::Extent3D extent,
                          vk::ImageLayout sourceLayout = vk::ImageLayout::eTransferSrcOptimal,
                          vk::ImageLayout destinationLayout = vk::ImageLayout::eTransferDstOptimal);
    export void BlitImage(vk::CommandBuffer commandBuffer, vk::Image source, vk::Image destination,
                          vk::Extent3D sourceExtent, vk::Extent3D destinationExtent,
                          vk::Filter filter = vk::Filter::eLinear);

    export class VulkanSampler
    {
    public:
        VulkanSampler() = default;
        VulkanSampler(const std::shared_ptr<VulkanDevice>& device, vk::Filter filter,
                      vk::SamplerAddressMode addressMode, bool anisotropy = true);
        ~VulkanSampler();
        VulkanSampler(const VulkanSampler&) = delete;
        VulkanSampler& operator=(const VulkanSampler&) = delete;
        VulkanSampler(VulkanSampler&& other) noexcept;
        VulkanSampler& operator=(VulkanSampler&& other) noexcept;
        vk::Sampler GetSampler() const noexcept { return m_Sampler; }

    private:
        vk::Sampler m_Sampler{};
        std::shared_ptr<VulkanDevice> m_Device;
    };

    export class VulkanImage
    {
    public:
        VulkanImage() = default;
        VulkanImage(const std::shared_ptr<VulkanDevice>& device,
                    const VulkanImageSpecification& specification,
                    const std::shared_ptr<Logger>& logger = nullptr);
        ~VulkanImage();
        VulkanImage(const VulkanImage&) = delete;
        VulkanImage& operator=(const VulkanImage&) = delete;
        VulkanImage(VulkanImage&& other) noexcept;
        VulkanImage& operator=(VulkanImage&& other) noexcept;

        void Create(const std::shared_ptr<VulkanDevice>& device,
                    const VulkanImageSpecification& specification);
        void Destroy();
        void Resize(vk::Extent3D extent);
        void SetDebugName(std::string name);
        vk::Image GetImage() const noexcept { return m_Image; }
        vk::ImageView GetImageView() const noexcept { return m_View; }
        vk::Sampler GetSampler() const noexcept { return m_Sampler.GetSampler(); }
        VmaAllocation GetAllocation() const noexcept { return m_Allocation; }
        const VulkanImageSpecification& GetSpecification() const noexcept { return m_Specification; }

    private:
        std::shared_ptr<VulkanDevice> m_Device;
        std::shared_ptr<Logger> m_Logger;
        VulkanImageSpecification m_Specification;
        vk::Image m_Image{};
        vk::ImageView m_View{};
        VmaAllocation m_Allocation = nullptr;
        VulkanSampler m_Sampler;
    };
}
