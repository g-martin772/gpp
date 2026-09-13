module;
#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
module GPP.Graphics;

import std;
import GPP.Core;
import :Vulkan.Image;

namespace GPP
{
    vk::ImageAspectFlags GetImageAspectMask(const vk::Format format, const vk::ImageLayout layout)
    {
        const bool depthLayout = layout == vk::ImageLayout::eDepthAttachmentOptimal ||
                                 layout == vk::ImageLayout::eDepthStencilAttachmentOptimal ||
                                 layout == vk::ImageLayout::eDepthReadOnlyOptimal ||
                                 layout == vk::ImageLayout::eDepthStencilReadOnlyOptimal;
        const bool depthFormat = format == vk::Format::eD16Unorm ||
                                 format == vk::Format::eD32Sfloat ||
                                 format == vk::Format::eD24UnormS8Uint ||
                                 format == vk::Format::eD32SfloatS8Uint ||
                                 format == vk::Format::eD16UnormS8Uint;
        if (!depthLayout && !depthFormat)
            return vk::ImageAspectFlagBits::eColor;
        vk::ImageAspectFlags flags = vk::ImageAspectFlagBits::eDepth;
        if (format == vk::Format::eD24UnormS8Uint ||
            format == vk::Format::eD32SfloatS8Uint ||
            format == vk::Format::eD16UnormS8Uint)
            flags |= vk::ImageAspectFlagBits::eStencil;
        return flags;
    }

    vk::ImageView CreateImageView(const vk::Device device, const vk::Image image,
                                  const vk::Format format, const vk::ImageViewType viewType,
                                  const vk::ImageAspectFlags aspectMask, const uint32_t mipLevels,
                                  const uint32_t arrayLayers)
    {
        vk::ImageViewCreateInfo info{};
        info.image = image;
        info.viewType = viewType;
        info.format = format;
        info.subresourceRange = {aspectMask, 0, mipLevels, 0, arrayLayers};
        return device.createImageView(info);
    }

    static void GetTransitionStages(const vk::ImageLayout layout,
                                    vk::PipelineStageFlags2& stages, vk::AccessFlags2& access)
    {
        switch (layout)
        {
        case vk::ImageLayout::eUndefined:
            stages = vk::PipelineStageFlagBits2::eTopOfPipe;
            access = vk::AccessFlagBits2::eNone;
            break;
        case vk::ImageLayout::eTransferDstOptimal:
            stages = vk::PipelineStageFlagBits2::eTransfer;
            access = vk::AccessFlagBits2::eTransferWrite;
            break;
        case vk::ImageLayout::eTransferSrcOptimal:
            stages = vk::PipelineStageFlagBits2::eTransfer;
            access = vk::AccessFlagBits2::eTransferRead;
            break;
        case vk::ImageLayout::eColorAttachmentOptimal:
            stages = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
            access = vk::AccessFlagBits2::eColorAttachmentRead |
                     vk::AccessFlagBits2::eColorAttachmentWrite;
            break;
        case vk::ImageLayout::eDepthAttachmentOptimal:
        case vk::ImageLayout::eDepthStencilAttachmentOptimal:
            stages = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                     vk::PipelineStageFlagBits2::eLateFragmentTests;
            access = vk::AccessFlagBits2::eDepthStencilAttachmentRead |
                     vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
            break;
        case vk::ImageLayout::eShaderReadOnlyOptimal:
        case vk::ImageLayout::eDepthReadOnlyOptimal:
        case vk::ImageLayout::eDepthStencilReadOnlyOptimal:
            stages = vk::PipelineStageFlagBits2::eFragmentShader |
                     vk::PipelineStageFlagBits2::eComputeShader;
            access = vk::AccessFlagBits2::eShaderSampledRead;
            break;
        case vk::ImageLayout::ePresentSrcKHR:
            stages = vk::PipelineStageFlagBits2::eBottomOfPipe;
            access = vk::AccessFlagBits2::eNone;
            break;
        default:
            stages = vk::PipelineStageFlagBits2::eAllCommands;
            access = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite;
            break;
        }
    }

    vk::ImageMemoryBarrier2 MakeImageTransition(const vk::Image image, const vk::Format format,
                                                const vk::ImageLayout oldLayout,
                                                const vk::ImageLayout newLayout,
                                                const uint32_t mipLevels, const uint32_t arrayLayers,
                                                vk::ImageAspectFlags aspectMask)
    {
        if (!aspectMask)
            aspectMask = GetImageAspectMask(format, newLayout);
        vk::PipelineStageFlags2 srcStages{}, dstStages{};
        vk::AccessFlags2 srcAccess{}, dstAccess{};
        GetTransitionStages(oldLayout, srcStages, srcAccess);
        GetTransitionStages(newLayout, dstStages, dstAccess);
        vk::ImageMemoryBarrier2 barrier{};
        barrier.srcStageMask = srcStages;
        barrier.srcAccessMask = srcAccess;
        barrier.dstStageMask = dstStages;
        barrier.dstAccessMask = dstAccess;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange = {aspectMask, 0, mipLevels, 0, arrayLayers};
        return barrier;
    }

    void TransitionImageLayout(const vk::CommandBuffer commandBuffer, const vk::Image image,
                               const vk::Format format, const vk::ImageLayout oldLayout,
                               const vk::ImageLayout newLayout, const uint32_t mipLevels,
                               const uint32_t arrayLayers, const vk::ImageAspectFlags aspectMask)
    {
        const auto barrier = MakeImageTransition(image, format, oldLayout, newLayout,
                                                 mipLevels, arrayLayers, aspectMask);
        vk::DependencyInfo dependency{};
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &barrier;
        commandBuffer.pipelineBarrier2(dependency);
    }

    void CopyBufferToImage(const vk::CommandBuffer commandBuffer, const vk::Buffer buffer,
                           const vk::Image image, const vk::Extent3D extent,
                           const vk::ImageLayout imageLayout)
    {
        vk::BufferImageCopy region{};
        region.imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
        region.imageExtent = extent;
        commandBuffer.copyBufferToImage(buffer, image, imageLayout, 1, &region);
    }

    void CopyImage(const vk::CommandBuffer commandBuffer, const vk::Image source,
                   const vk::Image destination, const vk::Extent3D extent,
                   const vk::ImageLayout sourceLayout, const vk::ImageLayout destinationLayout)
    {
        vk::ImageCopy region{};
        region.srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
        region.dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
        region.extent = extent;
        commandBuffer.copyImage(source, sourceLayout, destination, destinationLayout, 1, &region);
    }

    void BlitImage(const vk::CommandBuffer commandBuffer, const vk::Image source,
                   const vk::Image destination, const vk::Extent3D sourceExtent,
                   const vk::Extent3D destinationExtent, const vk::Filter filter)
    {
        vk::ImageBlit region{};
        region.srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
        region.dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
        region.srcOffsets[1] = vk::Offset3D{static_cast<int32_t>(sourceExtent.width),
                                            static_cast<int32_t>(sourceExtent.height),
                                            static_cast<int32_t>(sourceExtent.depth)};
        region.dstOffsets[1] = vk::Offset3D{static_cast<int32_t>(destinationExtent.width),
                                            static_cast<int32_t>(destinationExtent.height),
                                            static_cast<int32_t>(destinationExtent.depth)};
        commandBuffer.blitImage(source, vk::ImageLayout::eTransferSrcOptimal,
                                destination, vk::ImageLayout::eTransferDstOptimal, 1, &region, filter);
    }

    VulkanSampler::VulkanSampler(const std::shared_ptr<VulkanDevice>& device, const vk::Filter filter,
                                 const vk::SamplerAddressMode addressMode, const bool anisotropy)
        : m_Device(device)
    {
        vk::SamplerCreateInfo info{};
        info.magFilter = filter;
        info.minFilter = filter;
        info.mipmapMode = vk::SamplerMipmapMode::eLinear;
        info.addressModeU = addressMode;
        info.addressModeV = addressMode;
        info.addressModeW = addressMode;
        info.maxLod = VK_LOD_CLAMP_NONE;
        if (anisotropy && device->GetPhysicalDevice().getFeatures().samplerAnisotropy)
        {
            info.anisotropyEnable = VK_TRUE;
            info.maxAnisotropy = device->GetPhysicalDevice().getProperties().limits.maxSamplerAnisotropy;
        }
        m_Sampler = device->GetDevice().createSampler(info);
    }

    VulkanSampler::~VulkanSampler()
    {
        if (m_Sampler && m_Device)
            m_Device->GetDevice().destroySampler(m_Sampler);
    }

    VulkanSampler::VulkanSampler(VulkanSampler&& other) noexcept
        : m_Sampler(other.m_Sampler), m_Device(std::move(other.m_Device))
    {
        other.m_Sampler = nullptr;
    }

    VulkanSampler& VulkanSampler::operator=(VulkanSampler&& other) noexcept
    {
        if (this == &other) return *this;
        if (m_Sampler && m_Device) m_Device->GetDevice().destroySampler(m_Sampler);
        m_Sampler = other.m_Sampler;
        m_Device = std::move(other.m_Device);
        other.m_Sampler = nullptr;
        return *this;
    }

    VulkanImage::VulkanImage(const std::shared_ptr<VulkanDevice>& device,
                             const VulkanImageSpecification& specification,
                             const std::shared_ptr<Logger>& logger)
        : m_Logger(logger)
    {
        Create(device, specification);
    }

    VulkanImage::~VulkanImage() { Destroy(); }

    VulkanImage::VulkanImage(VulkanImage&& other) noexcept
        : m_Device(std::move(other.m_Device)), m_Logger(std::move(other.m_Logger)),
          m_Specification(std::move(other.m_Specification)), m_Image(other.m_Image),
          m_View(other.m_View), m_Allocation(other.m_Allocation), m_Sampler(std::move(other.m_Sampler))
    {
        other.m_Image = nullptr;
        other.m_View = nullptr;
        other.m_Allocation = nullptr;
    }

    VulkanImage& VulkanImage::operator=(VulkanImage&& other) noexcept
    {
        if (this == &other) return *this;
        Destroy();
        m_Device = std::move(other.m_Device);
        m_Logger = std::move(other.m_Logger);
        m_Specification = std::move(other.m_Specification);
        m_Image = other.m_Image;
        m_View = other.m_View;
        m_Allocation = other.m_Allocation;
        m_Sampler = std::move(other.m_Sampler);
        other.m_Image = nullptr;
        other.m_View = nullptr;
        other.m_Allocation = nullptr;
        return *this;
    }

    void VulkanImage::Create(const std::shared_ptr<VulkanDevice>& device,
                             const VulkanImageSpecification& specification)
    {
        Destroy();
        if (!device || !device->GetAllocator())
            throw std::invalid_argument("VulkanImage requires a device with a VMA allocator");
        m_Device = device;
        m_Specification = specification;
        if (!m_Specification.aspectMask)
            m_Specification.aspectMask = GetImageAspectMask(m_Specification.format,
                                                             m_Specification.initialLayout);
        vk::ImageCreateInfo createInfo{};
        createInfo.flags = specification.flags;
        createInfo.imageType = specification.type;
        createInfo.format = specification.format;
        createInfo.extent = specification.extent;
        createInfo.mipLevels = specification.mipLevels;
        createInfo.arrayLayers = specification.arrayLayers;
        createInfo.samples = specification.samples;
        createInfo.tiling = specification.tiling;
        createInfo.usage = specification.usage;
        createInfo.sharingMode = vk::SharingMode::eExclusive;
        createInfo.initialLayout = specification.initialLayout;
        const VkImageCreateInfo rawCreateInfo = createInfo;
        VmaAllocationCreateInfo allocationInfo{};
        allocationInfo.usage = specification.memoryUsage;
        VkImage rawImage = VK_NULL_HANDLE;
        if (vmaCreateImage(device->GetAllocator(), &rawCreateInfo, &allocationInfo, &rawImage,
                           &m_Allocation, nullptr) != VK_SUCCESS)
            throw std::runtime_error("Failed to create VMA-backed Vulkan image");
        m_Image = rawImage;
        if (m_Specification.createView)
            m_View = CreateImageView(device->GetDevice(), m_Image, m_Specification.format,
                                     m_Specification.viewType, m_Specification.aspectMask,
                                     m_Specification.mipLevels, m_Specification.arrayLayers);
        if (m_Specification.createSampler)
            m_Sampler = VulkanSampler(device, m_Specification.samplerFilter,
                                      m_Specification.samplerAddressMode);
        SetDebugName(m_Specification.debugName);
    }

    void VulkanImage::Destroy()
    {
        if (!m_Device) return;
        m_Sampler = {};
        if (m_View) m_Device->GetDevice().destroyImageView(m_View);
        if (m_Allocation) vmaDestroyImage(m_Device->GetAllocator(), m_Image, m_Allocation);
        m_View = nullptr;
        m_Image = nullptr;
        m_Allocation = nullptr;
        m_Device.reset();
    }

    void VulkanImage::Resize(const vk::Extent3D extent)
    {
        auto specification = m_Specification;
        specification.extent = extent;
        Create(m_Device, specification);
    }

    void VulkanImage::SetDebugName(std::string name)
    {
        if (!name.empty() && m_Allocation)
            vmaSetAllocationName(m_Device->GetAllocator(), m_Allocation, name.c_str());
    }
}
