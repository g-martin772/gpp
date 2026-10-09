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

    export struct RenderGraphWorkSlice
    {
        std::uint32_t First = 0;
        std::uint32_t Count = 0;
        std::uint32_t Total = 0;
    };

    export using RenderGraphSliceRecordFn =
        std::move_only_function<void(vk::CommandBuffer, RenderGraph&, const RenderGraphWorkSlice&)>;

    export struct IRenderGraphChunkSink
    {
        virtual ~IRenderGraphChunkSink() = default;
        virtual vk::CommandBuffer BeginChunk() = 0;
        virtual double SubmitChunkAndWait() = 0;
        [[nodiscard]] virtual bool Cancelled() const { return false; }
    };


    export class RenderGraph
    {
    public:
        explicit RenderGraph(std::shared_ptr<VulkanDevice> device, std::shared_ptr<Logger> logger,
                            std::uint32_t framesInFlight = 2);
        ~RenderGraph();

        RenderGraph(const RenderGraph&) = delete;
        RenderGraph& operator=(const RenderGraph&) = delete;

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

        void AddGraphicsPass(std::string name, std::vector<RenderGraphImageUse> imageUses,
                             std::vector<RenderGraphHandle> bufferUses,
                             std::vector<RenderGraphAttachment> colorAttachments,
                             std::optional<RenderGraphAttachment> depthAttachment,
                             RenderGraphRecordFn record);

        void AddBudgetedComputePass(std::string name, std::vector<RenderGraphImageUse> imageUses,
                                    std::vector<RenderGraphHandle> bufferUses, std::uint32_t totalItems,
                                    RenderGraphSliceRecordFn record);


        void Compile();
        void Execute(vk::CommandBuffer commandBuffer);

        vk::CommandBuffer ExecuteSliced(IRenderGraphChunkSink& sink,
                                        const std::function<AdaptiveSlicer&(const std::string&)>& slicerFor);

        void SetPrimaryColorTarget(RenderGraphHandle handle) noexcept { m_PrimaryColor = handle; }
        void SetPrimaryDepthTarget(RenderGraphHandle handle) noexcept { m_PrimaryDepth = handle; }
        [[nodiscard]] RenderGraphHandle GetPrimaryColorTarget() const noexcept { return m_PrimaryColor; }
        [[nodiscard]] RenderGraphHandle GetPrimaryDepthTarget() const noexcept { return m_PrimaryDepth; }

        [[nodiscard]] std::size_t PassCount() const noexcept { return m_Passes.size(); }

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

        enum class PassKind : std::uint8_t { Compute, Graphics, BudgetedCompute };

        struct Pass
        {
            PassKind Kind = PassKind::Compute;
            std::string Name;
            std::vector<RenderGraphImageUse> ImageUses;
            std::vector<RenderGraphHandle> BufferUses;
            std::vector<RenderGraphAttachment> ColorAttachments;
            std::optional<RenderGraphAttachment> DepthAttachment;
            RenderGraphRecordFn Record;
            std::uint32_t TotalItems = 0;
            RenderGraphSliceRecordFn RecordSlice;
        };

        void RecordPass(vk::CommandBuffer commandBuffer, Pass& pass);
        static void RecordPassBarrier(vk::CommandBuffer commandBuffer);

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
