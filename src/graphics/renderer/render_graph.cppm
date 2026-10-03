module;
#include <vk_mem_alloc.h>
export module GPP.Graphics:RenderGraph;

import std;
import vulkan;
import GPP.Core;
import :Vulkan;

namespace GPP
{
    export using RenderGraphHandle = std::uint32_t;
    export constexpr RenderGraphHandle kInvalidRenderGraphHandle = static_cast<RenderGraphHandle>(-1);

    // Describes a foreign image the graph does not own (a swapchain image, or an image owned by
    // a VulkanImage the caller keeps alive) so the graph can track and transition its layout.
    export struct RenderGraphExternalImage
    {
        vk::Image Image;
        vk::ImageView View;
        vk::Format Format = vk::Format::eUndefined;
        vk::Extent3D Extent{};
    };

    // One image a pass touches and the layout it must be in to do so. The graph inserts a
    // transition before the pass runs if the resource isn't already in that layout - passes never
    // call TransitionImageLayout themselves.
    export struct RenderGraphImageUse
    {
        RenderGraphHandle Handle = kInvalidRenderGraphHandle;
        vk::ImageLayout Layout = vk::ImageLayout::eGeneral;
    };

    // A color or depth attachment for a graphics pass. LoadOp::eLoad preserves whatever is already
    // in the image (for an overlay drawn on top of earlier passes); eClear clears it first.
    export struct RenderGraphAttachment
    {
        RenderGraphHandle Handle = kInvalidRenderGraphHandle;
        vk::AttachmentLoadOp LoadOp = vk::AttachmentLoadOp::eLoad;
        vk::ClearValue Clear{};
    };

    export class RenderGraph;

    export using RenderGraphRecordFn = std::move_only_function<void(vk::CommandBuffer, RenderGraph&)>;

    // A single frame's worth of compute/graphics passes for one render target (a window or an
    // offscreen buffer target). Layers contribute passes via GuiLayer::OnRenderGraph(); Renderer
    // compiles and executes the result once per target per frame.
    //
    // v1 scope (see MoleHole migration plan §1.1): passes run in declaration order - the graph does
    // not reorder them from a dependency analysis, so a layer that reads another layer's output
    // must be later in the layer stack. Resource state tracking covers image layouts only; it does
    // not track buffer read/write hazards (a pass that needs a buffer barrier records it itself).
    // Everything runs on the single graphics queue, matching how VulkanDevice is configured today.
    // Transient resources are cached by name across frames (recreated only if their spec changes),
    // but nothing is aliased in memory between resources that are never alive at the same time.
    export class RenderGraph
    {
    public:
        // framesInFlight must match the number of frame-in-flight slots the caller cycles through
        // (Renderer uses 2) - Begin(frameIndex) only resets that slot's descriptor pools, and the
        // caller only reaches a given frameIndex again after fencing on the GPU work that last used
        // it, so resetting it there can never race a still-executing command buffer.
        explicit RenderGraph(std::shared_ptr<VulkanDevice> device, std::shared_ptr<Logger> logger,
                            std::uint32_t framesInFlight = 2);
        ~RenderGraph();

        RenderGraph(const RenderGraph&) = delete;
        RenderGraph& operator=(const RenderGraph&) = delete;

        // Clears the pass list for a new frame and resets frameIndex's descriptor pool slot for
        // reuse. Named transient resources are kept and reused rather than recreated.
        void Begin(std::uint32_t frameIndex);

        RenderGraphHandle ImportImage(std::string name, const RenderGraphExternalImage& image,
                                      vk::ImageLayout currentLayout);
        RenderGraphHandle ImportImage(std::string name, const VulkanImage& image,
                                      vk::ImageLayout currentLayout);
        RenderGraphHandle CreateImage(std::string name, const VulkanImageSpecification& specification);
        RenderGraphHandle ImportBuffer(std::string name, const VulkanBuffer& buffer);
        RenderGraphHandle CreateBuffer(std::string name, const VulkanBufferSpecification& specification);

        void AddComputePass(std::string name, std::vector<RenderGraphImageUse> imageUses,
                            std::vector<RenderGraphHandle> bufferUses, RenderGraphRecordFn record);

        // extent sizes the implicit viewport/scissor (and the rendering area) the graph sets up
        // before invoking record; a pass can still override either with its own setViewport/
        // setScissor calls inside record.
        void AddGraphicsPass(std::string name, std::vector<RenderGraphImageUse> imageUses,
                             std::vector<RenderGraphHandle> bufferUses,
                             std::vector<RenderGraphAttachment> colorAttachments,
                             std::optional<RenderGraphAttachment> depthAttachment,
                             RenderGraphRecordFn record);

        // Allocates any newly-declared transient resources for this frame. No pass reordering
        // happens here in v1 - call this once after every layer has contributed its passes.
        void Compile();

        // Records layout transitions and each pass's callback, in declaration order.
        void Execute(vk::CommandBuffer commandBuffer);

        // The color/depth image this target's owning window or buffer target ultimately presents
        // or samples - set by Renderer right after importing them, before any layer runs, so a
        // layer can render "into whatever I'm attached to" without knowing Renderer's internal
        // resource names.
        void SetPrimaryColorTarget(RenderGraphHandle handle) noexcept { m_PrimaryColor = handle; }
        void SetPrimaryDepthTarget(RenderGraphHandle handle) noexcept { m_PrimaryDepth = handle; }
        [[nodiscard]] RenderGraphHandle GetPrimaryColorTarget() const noexcept { return m_PrimaryColor; }
        [[nodiscard]] RenderGraphHandle GetPrimaryDepthTarget() const noexcept { return m_PrimaryDepth; }

        [[nodiscard]] vk::ImageLayout GetCurrentLayout(RenderGraphHandle handle) const;
        [[nodiscard]] vk::Extent3D GetImageExtent(RenderGraphHandle handle) const;
        [[nodiscard]] vk::Format GetImageFormat(RenderGraphHandle handle) const;
        [[nodiscard]] const VulkanImage* TryGetOwnedImage(RenderGraphHandle handle) const;

        // Used from a pass's record callback to resolve a handle and build/bind descriptor sets.
        [[nodiscard]] vk::Image GetImage(RenderGraphHandle handle) const;
        [[nodiscard]] vk::ImageView GetImageView(RenderGraphHandle handle) const;
        [[nodiscard]] vk::Buffer GetBuffer(RenderGraphHandle handle) const;
        [[nodiscard]] vk::DescriptorSet AllocateDescriptorSet(vk::DescriptorSetLayout layout);

    private:
        struct ImageResource
        {
            std::string Name;
            bool Owned = false;
            VulkanImage Storage;
            RenderGraphExternalImage External;
            vk::ImageLayout CurrentLayout = vk::ImageLayout::eUndefined;
            VulkanImageSpecification Specification;
            bool HasSpecification = false;
        };

        struct BufferResource
        {
            std::string Name;
            bool Owned = false;
            VulkanBuffer Storage;
            vk::Buffer External;
            VulkanBufferSpecification Specification;
            bool HasSpecification = false;
        };

        enum class PassKind : std::uint8_t { Compute, Graphics };

        struct Pass
        {
            PassKind Kind = PassKind::Compute;
            std::string Name;
            std::vector<RenderGraphImageUse> ImageUses;
            std::vector<RenderGraphHandle> BufferUses;
            std::vector<RenderGraphAttachment> ColorAttachments;
            std::optional<RenderGraphAttachment> DepthAttachment;
            RenderGraphRecordFn Record;
        };

        void TransitionTo(vk::CommandBuffer commandBuffer, RenderGraphHandle handle,
                          vk::ImageLayout layout);
        [[nodiscard]] vk::Extent3D ResolveRenderArea(const Pass& pass) const;
        const ImageResource& Image(RenderGraphHandle handle) const;
        ImageResource& Image(RenderGraphHandle handle);

        std::shared_ptr<VulkanDevice> m_Device;
        std::shared_ptr<Logger> m_Logger;
        std::vector<VulkanDescriptorAllocator> m_DescriptorAllocators;
        std::uint32_t m_ActiveFrameIndex = 0;

        std::unordered_map<std::string, RenderGraphHandle> m_ImageHandlesByName;
        std::vector<ImageResource> m_Images;
        std::unordered_map<std::string, RenderGraphHandle> m_BufferHandlesByName;
        std::vector<BufferResource> m_Buffers;
        std::vector<Pass> m_Passes;
        RenderGraphHandle m_PrimaryColor = kInvalidRenderGraphHandle;
        RenderGraphHandle m_PrimaryDepth = kInvalidRenderGraphHandle;
    };
}
