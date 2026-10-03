module;
#include <vk_mem_alloc.h>
module GPP.Graphics;

import std;
import vulkan;
import GPP.Core;
import :RenderGraph;

namespace GPP
{
    namespace
    {
        bool SameImageShape(const VulkanImageSpecification& a, const VulkanImageSpecification& b)
        {
            return a.extent.width == b.extent.width && a.extent.height == b.extent.height &&
                   a.extent.depth == b.extent.depth && a.format == b.format &&
                   a.usage == b.usage && a.type == b.type && a.viewType == b.viewType &&
                   a.mipLevels == b.mipLevels && a.arrayLayers == b.arrayLayers;
        }

        constexpr std::uint32_t kDefaultFramesInFlight = 2;
    }

    RenderGraph::RenderGraph(std::shared_ptr<VulkanDevice> device, std::shared_ptr<Logger> logger,
                            const std::uint32_t framesInFlight)
        : m_Device(std::move(device)), m_Logger(std::move(logger))
    {
        if (!m_Device)
        {
            throw std::invalid_argument("RenderGraph requires a valid VulkanDevice.");
        }
        m_DescriptorAllocators.reserve(std::max(framesInFlight, 1u));
        for (std::uint32_t i = 0; i < std::max(framesInFlight, 1u); ++i)
        {
            m_DescriptorAllocators.emplace_back(m_Device);
        }
    }

    RenderGraph::~RenderGraph() = default;

    void RenderGraph::Begin(const std::uint32_t frameIndex)
    {
        m_Passes.clear();
        m_PrimaryColor = kInvalidRenderGraphHandle;
        m_PrimaryDepth = kInvalidRenderGraphHandle;
        m_ActiveFrameIndex = frameIndex % static_cast<std::uint32_t>(m_DescriptorAllocators.size());
        m_DescriptorAllocators[m_ActiveFrameIndex].ResetPools();
    }

    RenderGraph::ImageResource& RenderGraph::Image(const RenderGraphHandle handle)
    {
        return m_Images.at(handle);
    }

    const RenderGraph::ImageResource& RenderGraph::Image(const RenderGraphHandle handle) const
    {
        return m_Images.at(handle);
    }

    RenderGraphHandle RenderGraph::ImportImage(std::string name, const RenderGraphExternalImage& image,
                                               const vk::ImageLayout currentLayout)
    {
        if (const auto it = m_ImageHandlesByName.find(name); it != m_ImageHandlesByName.end())
        {
            auto& resource = m_Images[it->second];
            resource.Owned = false;
            resource.External = image;
            resource.CurrentLayout = currentLayout;
            resource.HasSpecification = false;
            return it->second;
        }
        ImageResource resource;
        resource.Name = name;
        resource.Owned = false;
        resource.External = image;
        resource.CurrentLayout = currentLayout;
        const auto handle = static_cast<RenderGraphHandle>(m_Images.size());
        m_Images.push_back(std::move(resource));
        m_ImageHandlesByName.emplace(std::move(name), handle);
        return handle;
    }

    RenderGraphHandle RenderGraph::ImportImage(std::string name, const VulkanImage& image,
                                               const vk::ImageLayout currentLayout)
    {
        const auto& spec = image.GetSpecification();
        return ImportImage(std::move(name),
                           RenderGraphExternalImage{image.GetImage(), image.GetImageView(),
                                                    spec.format, spec.extent},
                           currentLayout);
    }

    RenderGraphHandle RenderGraph::CreateImage(std::string name,
                                               const VulkanImageSpecification& specification)
    {
        if (const auto it = m_ImageHandlesByName.find(name); it != m_ImageHandlesByName.end())
        {
            auto& resource = m_Images[it->second];
            if (resource.Owned && resource.HasSpecification &&
                SameImageShape(resource.Specification, specification))
            {
                return it->second;
            }
            resource.Storage.Create(m_Device, specification);
            resource.Specification = specification;
            resource.HasSpecification = true;
            resource.Owned = true;
            resource.CurrentLayout = specification.initialLayout;
            return it->second;
        }
        ImageResource resource;
        resource.Name = name;
        resource.Owned = true;
        resource.Storage.Create(m_Device, specification);
        resource.Specification = specification;
        resource.HasSpecification = true;
        resource.CurrentLayout = specification.initialLayout;
        const auto handle = static_cast<RenderGraphHandle>(m_Images.size());
        m_Images.push_back(std::move(resource));
        m_ImageHandlesByName.emplace(std::move(name), handle);
        return handle;
    }

    RenderGraphHandle RenderGraph::ImportBuffer(std::string name, const VulkanBuffer& buffer)
    {
        if (const auto it = m_BufferHandlesByName.find(name); it != m_BufferHandlesByName.end())
        {
            auto& resource = m_Buffers[it->second];
            resource.Owned = false;
            resource.External = buffer.GetBuffer();
            resource.HasSpecification = false;
            return it->second;
        }
        BufferResource resource;
        resource.Name = name;
        resource.Owned = false;
        resource.External = buffer.GetBuffer();
        const auto handle = static_cast<RenderGraphHandle>(m_Buffers.size());
        m_Buffers.push_back(std::move(resource));
        m_BufferHandlesByName.emplace(std::move(name), handle);
        return handle;
    }

    RenderGraphHandle RenderGraph::CreateBuffer(std::string name,
                                                const VulkanBufferSpecification& specification)
    {
        if (const auto it = m_BufferHandlesByName.find(name); it != m_BufferHandlesByName.end())
        {
            auto& resource = m_Buffers[it->second];
            if (resource.Owned && resource.HasSpecification &&
                resource.Specification.size == specification.size)
            {
                return it->second;
            }
            resource.Storage.Create(m_Device, specification);
            resource.Specification = specification;
            resource.HasSpecification = true;
            resource.Owned = true;
            return it->second;
        }
        BufferResource resource;
        resource.Name = name;
        resource.Owned = true;
        resource.Storage.Create(m_Device, specification);
        resource.Specification = specification;
        resource.HasSpecification = true;
        const auto handle = static_cast<RenderGraphHandle>(m_Buffers.size());
        m_Buffers.push_back(std::move(resource));
        m_BufferHandlesByName.emplace(std::move(name), handle);
        return handle;
    }

    void RenderGraph::AddComputePass(std::string name, std::vector<RenderGraphImageUse> imageUses,
                                     std::vector<RenderGraphHandle> bufferUses,
                                     RenderGraphRecordFn record)
    {
        m_Passes.push_back(Pass{
            .Kind = PassKind::Compute,
            .Name = std::move(name),
            .ImageUses = std::move(imageUses),
            .BufferUses = std::move(bufferUses),
            .Record = std::move(record)
        });
    }

    void RenderGraph::AddGraphicsPass(std::string name, std::vector<RenderGraphImageUse> imageUses,
                                      std::vector<RenderGraphHandle> bufferUses,
                                      std::vector<RenderGraphAttachment> colorAttachments,
                                      std::optional<RenderGraphAttachment> depthAttachment,
                                      RenderGraphRecordFn record)
    {
        m_Passes.push_back(Pass{
            .Kind = PassKind::Graphics,
            .Name = std::move(name),
            .ImageUses = std::move(imageUses),
            .BufferUses = std::move(bufferUses),
            .ColorAttachments = std::move(colorAttachments),
            .DepthAttachment = depthAttachment,
            .Record = std::move(record)
        });
    }

    void RenderGraph::Compile()
    {
        // v1: resources are created eagerly and passes execute in declaration order, so there is
        // nothing to schedule yet. This exists so a real dependency-driven scheduler can be added
        // later without changing call sites (see the class-level comment in render_graph.cppm).
    }

    void RenderGraph::TransitionTo(const vk::CommandBuffer commandBuffer, const RenderGraphHandle handle,
                                   const vk::ImageLayout layout)
    {
        auto& resource = Image(handle);
        const auto image = resource.Owned ? resource.Storage.GetImage() : resource.External.Image;
        const auto format = resource.Owned
                                 ? resource.Storage.GetSpecification().format
                                 : resource.External.Format;
        TransitionImageLayout(commandBuffer, image, format, resource.CurrentLayout, layout);
        resource.CurrentLayout = layout;
    }

    vk::Extent3D RenderGraph::ResolveRenderArea(const Pass& pass) const
    {
        if (!pass.ColorAttachments.empty())
        {
            return GetImageExtent(pass.ColorAttachments.front().Handle);
        }
        if (pass.DepthAttachment)
        {
            return GetImageExtent(pass.DepthAttachment->Handle);
        }
        return vk::Extent3D{1, 1, 1};
    }

    void RenderGraph::Execute(const vk::CommandBuffer commandBuffer)
    {
        for (auto& pass : m_Passes)
        {
            for (const auto& use : pass.ImageUses)
            {
                TransitionTo(commandBuffer, use.Handle, use.Layout);
            }

            if (pass.Kind == PassKind::Compute)
            {
                pass.Record(commandBuffer, *this);
                continue;
            }

            std::vector<vk::RenderingAttachmentInfo> colorInfos;
            colorInfos.reserve(pass.ColorAttachments.size());
            for (const auto& attachment : pass.ColorAttachments)
            {
                TransitionTo(commandBuffer, attachment.Handle, vk::ImageLayout::eColorAttachmentOptimal);
                vk::RenderingAttachmentInfo info{};
                info.imageView = GetImageView(attachment.Handle);
                info.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
                info.loadOp = attachment.LoadOp;
                info.storeOp = vk::AttachmentStoreOp::eStore;
                info.clearValue = attachment.Clear;
                colorInfos.push_back(info);
            }

            std::optional<vk::RenderingAttachmentInfo> depthInfo;
            if (pass.DepthAttachment)
            {
                const auto format = GetImageFormat(pass.DepthAttachment->Handle);
                const auto hasStencil =
                    (GetImageAspectMask(format) & vk::ImageAspectFlagBits::eStencil) !=
                    vk::ImageAspectFlags{};
                const auto layout = hasStencil
                                         ? vk::ImageLayout::eDepthStencilAttachmentOptimal
                                         : vk::ImageLayout::eDepthAttachmentOptimal;
                TransitionTo(commandBuffer, pass.DepthAttachment->Handle, layout);
                vk::RenderingAttachmentInfo info{};
                info.imageView = GetImageView(pass.DepthAttachment->Handle);
                info.imageLayout = layout;
                info.loadOp = pass.DepthAttachment->LoadOp;
                info.storeOp = vk::AttachmentStoreOp::eDontCare;
                info.clearValue = pass.DepthAttachment->Clear;
                depthInfo = info;
            }

            const auto area = ResolveRenderArea(pass);
            vk::RenderingInfo renderingInfo{};
            renderingInfo.renderArea = vk::Rect2D{{0, 0}, {area.width, area.height}};
            renderingInfo.layerCount = 1;
            renderingInfo.colorAttachmentCount = static_cast<std::uint32_t>(colorInfos.size());
            renderingInfo.pColorAttachments = colorInfos.empty() ? nullptr : colorInfos.data();
            renderingInfo.pDepthAttachment = depthInfo ? &*depthInfo : nullptr;

            commandBuffer.beginRendering(renderingInfo);
            const vk::Viewport viewport{
                0.0f, 0.0f, static_cast<float>(area.width), static_cast<float>(area.height), 0.0f, 1.0f
            };
            const vk::Rect2D scissor{{0, 0}, {area.width, area.height}};
            commandBuffer.setViewport(0, 1, &viewport);
            commandBuffer.setScissor(0, 1, &scissor);
            pass.Record(commandBuffer, *this);
            commandBuffer.endRendering();
        }
    }

    vk::ImageLayout RenderGraph::GetCurrentLayout(const RenderGraphHandle handle) const
    {
        return Image(handle).CurrentLayout;
    }

    vk::Extent3D RenderGraph::GetImageExtent(const RenderGraphHandle handle) const
    {
        const auto& resource = Image(handle);
        return resource.Owned ? resource.Storage.GetSpecification().extent : resource.External.Extent;
    }

    vk::Format RenderGraph::GetImageFormat(const RenderGraphHandle handle) const
    {
        const auto& resource = Image(handle);
        return resource.Owned ? resource.Storage.GetSpecification().format : resource.External.Format;
    }

    const VulkanImage* RenderGraph::TryGetOwnedImage(const RenderGraphHandle handle) const
    {
        const auto& resource = Image(handle);
        return resource.Owned ? &resource.Storage : nullptr;
    }

    vk::Image RenderGraph::GetImage(const RenderGraphHandle handle) const
    {
        const auto& resource = Image(handle);
        return resource.Owned ? resource.Storage.GetImage() : resource.External.Image;
    }

    vk::ImageView RenderGraph::GetImageView(const RenderGraphHandle handle) const
    {
        const auto& resource = Image(handle);
        return resource.Owned ? resource.Storage.GetImageView() : resource.External.View;
    }

    vk::Buffer RenderGraph::GetBuffer(const RenderGraphHandle handle) const
    {
        const auto& resource = m_Buffers.at(handle);
        return resource.Owned ? resource.Storage.GetBuffer() : resource.External;
    }

    vk::DescriptorSet RenderGraph::AllocateDescriptorSet(const vk::DescriptorSetLayout layout)
    {
        return m_DescriptorAllocators[m_ActiveFrameIndex].Allocate(layout);
    }
}
