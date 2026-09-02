module;
#include <vulkan/vulkan.hpp>
export module GPP.Graphics:Vulkan.Pipeline;

import std;
import GPP.Core;
import :Vulkan.Device;

namespace GPP
{
    // ai_slop.please_work();
    export class VulkanPipeline
    {
    public:
        VulkanPipeline(
            const std::shared_ptr<VulkanDevice>& device,
            vk::Format colorFormat,
            vk::Format depthFormat,
            std::span<const uint32_t> vertexSpirv,
            std::span<const uint32_t> fragmentSpirv
        ) : m_Device(device)
        {
            if (!m_Device)
            {
                throw std::runtime_error("VulkanPipeline requires a valid VulkanDevice instance.");
            }

            CreatePipeline(colorFormat, depthFormat, vertexSpirv, fragmentSpirv);
        }

        ~VulkanPipeline()
        {
            auto logicalDevice = m_Device->GetDevice();
            if (m_Pipeline)
            {
                logicalDevice.destroyPipeline(m_Pipeline);
            }
            if (m_PipelineLayout)
            {
                logicalDevice.destroyPipelineLayout(m_PipelineLayout);
            }
        }

        VulkanPipeline(const VulkanPipeline&) = delete;
        VulkanPipeline& operator=(const VulkanPipeline&) = delete;

        VulkanPipeline(VulkanPipeline&& other) noexcept
            : m_Device(std::move(other.m_Device)),
              m_PipelineLayout(other.m_PipelineLayout),
              m_Pipeline(other.m_Pipeline)
        {
            other.m_PipelineLayout = nullptr;
            other.m_Pipeline = nullptr;
        }

        VulkanPipeline& operator=(VulkanPipeline&& other) noexcept
        {
            if (this != &other)
            {
                auto logicalDevice = m_Device->GetDevice();
                if (m_Pipeline) logicalDevice.destroyPipeline(m_Pipeline);
                if (m_PipelineLayout) logicalDevice.destroyPipelineLayout(m_PipelineLayout);

                m_Device = std::move(other.m_Device);
                m_PipelineLayout = other.m_PipelineLayout;
                m_Pipeline = other.m_Pipeline;

                other.m_PipelineLayout = nullptr;
                other.m_Pipeline = nullptr;
            }
            return *this;
        }

        [[nodiscard]] vk::Pipeline GetPipeline() const noexcept { return m_Pipeline; }
        [[nodiscard]] vk::PipelineLayout GetLayout() const noexcept { return m_PipelineLayout; }

    private:
        void CreatePipeline(
            vk::Format colorFormat,
            vk::Format depthFormat,
            std::span<const uint32_t> vertexSpirv,
            std::span<const uint32_t> fragmentSpirv
        ) {
            auto logicalDevice = m_Device->GetDevice();

            // 1. Create Shader Modules from SPIR-V bytecode
            vk::ShaderModuleCreateInfo vertCreateInfo({}, vertexSpirv.size() * sizeof(uint32_t), vertexSpirv.data());
            vk::ShaderModuleCreateInfo fragCreateInfo({}, fragmentSpirv.size() * sizeof(uint32_t), fragmentSpirv.data());

            vk::ShaderModule vertModule = logicalDevice.createShaderModule(vertCreateInfo);
            vk::ShaderModule fragModule = logicalDevice.createShaderModule(fragCreateInfo);

            // Shader stage creations
            vk::PipelineShaderStageCreateInfo shaderStages[] = {
                vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex, vertModule, "main"),
                vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment, fragModule, "main")
            };

            // 2. Vertex Input State (Empty for hardcoded triangle shaders)
            vk::PipelineVertexInputStateCreateInfo vertexInputInfo({}, 0, nullptr, 0, nullptr);

            // 3. Input Assembly State (Draw solid triangles)
            vk::PipelineInputAssemblyStateCreateInfo inputAssembly({}, vk::PrimitiveTopology::eTriangleList, VK_FALSE);

            // 4. Viewport & Scissor State (Marked dynamic so we don't have to specify dimensions here)
            vk::PipelineViewportStateCreateInfo viewportState({}, 1, nullptr, 1, nullptr);

            // 5. Rasterization State
            vk::PipelineRasterizationStateCreateInfo rasterizer{};
            rasterizer.depthClampEnable = VK_FALSE;
            rasterizer.rasterizerDiscardEnable = VK_FALSE;
            rasterizer.polygonMode = vk::PolygonMode::eFill;
            rasterizer.lineWidth = 1.0f;
            rasterizer.cullMode = vk::CullModeFlagBits::eNone; // No culling to easily draw basic geometry
            rasterizer.frontFace = vk::FrontFace::eClockwise;
            rasterizer.depthBiasEnable = VK_FALSE;

            // 6. Multisample State (No multisampling)
            vk::PipelineMultisampleStateCreateInfo multisampling{};
            multisampling.sampleShadingEnable = VK_FALSE;
            multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

            // 7. Depth/Stencil State (Only configured if a depth format is specified)
            vk::PipelineDepthStencilStateCreateInfo depthStencil{};
            if (depthFormat != vk::Format::eUndefined)
            {
                depthStencil.depthTestEnable = VK_TRUE;
                depthStencil.depthWriteEnable = VK_TRUE;
                depthStencil.depthCompareOp = vk::CompareOp::eLess;
                depthStencil.depthBoundsTestEnable = VK_FALSE;
                depthStencil.stencilTestEnable = VK_FALSE;
            }
            else
            {
                depthStencil.depthTestEnable = VK_FALSE;
                depthStencil.depthWriteEnable = VK_FALSE;
            }

            // 8. Color Blend Attachment State
            vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
            colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | 
                                                  vk::ColorComponentFlagBits::eG | 
                                                  vk::ColorComponentFlagBits::eB | 
                                                  vk::ColorComponentFlagBits::eA;
            colorBlendAttachment.blendEnable = VK_TRUE;
            colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
            colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
            colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
            colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
            colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eZero;
            colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;

            vk::PipelineColorBlendStateCreateInfo colorBlending{};
            colorBlending.logicOpEnable = VK_FALSE;
            colorBlending.logicOp = vk::LogicOp::eCopy;
            colorBlending.attachmentCount = 1;
            colorBlending.pAttachments = &colorBlendAttachment;

            // 9. Dynamic States Configuration (Allows on-the-fly Viewport and Scissor resizing!)
            std::vector<vk::DynamicState> dynamicStates = {
                vk::DynamicState::eViewport,
                vk::DynamicState::eScissor
            };
            vk::PipelineDynamicStateCreateInfo dynamicState({}, static_cast<uint32_t>(dynamicStates.size()), dynamicStates.data());

            // 10. Pipeline Layout Creation (Empty for now - no descriptor sets or push constants yet)
            vk::PipelineLayoutCreateInfo pipelineLayoutInfo({}, 0, nullptr, 0, nullptr);
            m_PipelineLayout = logicalDevice.createPipelineLayout(pipelineLayoutInfo);

            // ====================================================================
            // VULKAN 1.3 DYNAMIC RENDERING HANDSHAKE
            // This replaces vk::RenderPass by declaring target formats on the fly!
            // ====================================================================
            vk::PipelineRenderingCreateInfo renderingCreateInfo{};
            renderingCreateInfo.colorAttachmentCount = 1;
            renderingCreateInfo.pColorAttachmentFormats = &colorFormat;
            renderingCreateInfo.depthAttachmentFormat = depthFormat;
            renderingCreateInfo.stencilAttachmentFormat = vk::Format::eUndefined;

            // Chain the dynamic rendering info into the graphics pipeline's pNext chain
            vk::GraphicsPipelineCreateInfo pipelineInfo{};
            pipelineInfo.pNext = &renderingCreateInfo; // ✅ CRITICAL: The Vulkan 1.3 Link
            pipelineInfo.stageCount = 2;
            pipelineInfo.pStages = shaderStages;
            pipelineInfo.pVertexInputState = &vertexInputInfo;
            pipelineInfo.pInputAssemblyState = &inputAssembly;
            pipelineInfo.pViewportState = &viewportState;
            pipelineInfo.pRasterizationState = &rasterizer;
            pipelineInfo.pMultisampleState = &multisampling;
            pipelineInfo.pDepthStencilState = &depthStencil;
            pipelineInfo.pColorBlendState = &colorBlending;
            pipelineInfo.pDynamicState = &dynamicState;
            pipelineInfo.layout = m_PipelineLayout;
            pipelineInfo.renderPass = nullptr; // ✅ Replaced entirely by renderingCreateInfo in pNext!
            pipelineInfo.subpass = 0;

            try
            {
                // Create the Pipeline using standard vulkan.hpp bindings
                auto result = logicalDevice.createGraphicsPipeline(nullptr, pipelineInfo);
                if (result.result != vk::Result::eSuccess)
                {
                    throw std::runtime_error("Failed to compile Vulkan Graphics Pipeline.");
                }
                m_Pipeline = result.value;
            }
            catch (const std::exception&)
            {
                logicalDevice.destroyShaderModule(vertModule);
                logicalDevice.destroyShaderModule(fragModule);
                throw;
            }

            // Shader modules are only needed during pipeline compilation; we can safely discard them now
            logicalDevice.destroyShaderModule(vertModule);
            logicalDevice.destroyShaderModule(fragModule);
        }

    private:
        std::shared_ptr<VulkanDevice> m_Device{ nullptr };
        vk::PipelineLayout m_PipelineLayout{ nullptr };
        vk::Pipeline m_Pipeline{ nullptr };
    };
}
