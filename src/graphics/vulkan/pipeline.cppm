module;
#include <vulkan/vulkan.hpp>
export module GPP.Graphics:Vulkan.Pipeline;

import std;
import GPP.Core;
import :Vulkan.Device;
import :Shader;

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
        ) : VulkanPipeline(device, colorFormat, depthFormat, vertexSpirv, fragmentSpirv, {})
        {
        }

        VulkanPipeline(
            const std::shared_ptr<VulkanDevice>& device,
            vk::Format colorFormat,
            vk::Format depthFormat,
            std::span<const uint32_t> vertexSpirv,
            std::span<const uint32_t> fragmentSpirv,
            const ShaderReflection& reflection
        ) : m_Device(device)
        {
            if (!m_Device)
            {
                throw std::runtime_error("VulkanPipeline requires a valid VulkanDevice instance.");
            }

            CreatePipeline(colorFormat, depthFormat, vertexSpirv, fragmentSpirv, reflection);
        }

        ~VulkanPipeline()
        {
            if (!m_Device)
            {
                return;
            }
            auto logicalDevice = m_Device->GetDevice();
            if (m_Pipeline)
            {
                logicalDevice.destroyPipeline(m_Pipeline);
            }
            if (m_PipelineLayout)
            {
                logicalDevice.destroyPipelineLayout(m_PipelineLayout);
            }
            for (auto layout : m_DescriptorSetLayouts)
            {
                if (layout) logicalDevice.destroyDescriptorSetLayout(layout);
            }
        }

        VulkanPipeline(const VulkanPipeline&) = delete;
        VulkanPipeline& operator=(const VulkanPipeline&) = delete;

        VulkanPipeline(VulkanPipeline&& other) noexcept
            : m_Device(std::move(other.m_Device)),
              m_PipelineLayout(other.m_PipelineLayout),
              m_Pipeline(other.m_Pipeline),
              m_DescriptorSetLayouts(std::move(other.m_DescriptorSetLayouts))
        {
            other.m_PipelineLayout = nullptr;
            other.m_Pipeline = nullptr;
        }

        VulkanPipeline& operator=(VulkanPipeline&& other) noexcept
        {
            if (this != &other)
            {
                if (m_Device)
                {
                    auto logicalDevice = m_Device->GetDevice();
                    if (m_Pipeline) logicalDevice.destroyPipeline(m_Pipeline);
                    if (m_PipelineLayout) logicalDevice.destroyPipelineLayout(m_PipelineLayout);
                    for (auto layout : m_DescriptorSetLayouts)
                    {
                        if (layout) logicalDevice.destroyDescriptorSetLayout(layout);
                    }
                }

                m_Device = std::move(other.m_Device);
                m_PipelineLayout = other.m_PipelineLayout;
                m_Pipeline = other.m_Pipeline;
                m_DescriptorSetLayouts = std::move(other.m_DescriptorSetLayouts);

                other.m_PipelineLayout = nullptr;
                other.m_Pipeline = nullptr;
            }
            return *this;
        }

        [[nodiscard]] vk::Pipeline GetPipeline() const noexcept { return m_Pipeline; }
        [[nodiscard]] vk::PipelineLayout GetLayout() const noexcept { return m_PipelineLayout; }
        [[nodiscard]] const std::vector<vk::DescriptorSetLayout>& GetDescriptorSetLayouts() const noexcept
        {
            return m_DescriptorSetLayouts;
        }

    private:
        static vk::DescriptorType ToVulkanDescriptorType(ShaderDescriptorType type)
        {
            switch (type)
            {
            case ShaderDescriptorType::Sampler: return vk::DescriptorType::eSampler;
            case ShaderDescriptorType::CombinedImageSampler:
                return vk::DescriptorType::eCombinedImageSampler;
            case ShaderDescriptorType::SampledImage: return vk::DescriptorType::eSampledImage;
            case ShaderDescriptorType::StorageImage: return vk::DescriptorType::eStorageImage;
            case ShaderDescriptorType::UniformTexelBuffer:
                return vk::DescriptorType::eUniformTexelBuffer;
            case ShaderDescriptorType::StorageTexelBuffer:
                return vk::DescriptorType::eStorageTexelBuffer;
            case ShaderDescriptorType::UniformBuffer: return vk::DescriptorType::eUniformBuffer;
            case ShaderDescriptorType::StorageBuffer: return vk::DescriptorType::eStorageBuffer;
            case ShaderDescriptorType::InputAttachment: return vk::DescriptorType::eInputAttachment;
            case ShaderDescriptorType::AccelerationStructure:
                return vk::DescriptorType::eAccelerationStructureKHR;
            default: throw std::runtime_error("Unsupported shader descriptor type.");
            }
        }

        static vk::ShaderStageFlags ToVulkanShaderStages(ShaderStageFlags stages)
        {
            vk::ShaderStageFlags result{};
            if (HasShaderStage(stages, ShaderStageFlags::Vertex)) result |= vk::ShaderStageFlagBits::eVertex;
            if (HasShaderStage(stages, ShaderStageFlags::Fragment)) result |= vk::ShaderStageFlagBits::eFragment;
            if (HasShaderStage(stages, ShaderStageFlags::Compute)) result |= vk::ShaderStageFlagBits::eCompute;
            if (HasShaderStage(stages, ShaderStageFlags::Geometry)) result |= vk::ShaderStageFlagBits::eGeometry;
            if (HasShaderStage(stages, ShaderStageFlags::TessellationControl))
                result |= vk::ShaderStageFlagBits::eTessellationControl;
            if (HasShaderStage(stages, ShaderStageFlags::TessellationEvaluation))
                result |= vk::ShaderStageFlagBits::eTessellationEvaluation;
            return result;
        }

        static vk::Format VertexFormat(const ShaderVertexInput& input)
        {
            if (input.bitWidth == 32)
            {
                if (input.scalarType == ShaderScalarType::Float)
                {
                    switch (input.components)
                    {
                    case 1: return vk::Format::eR32Sfloat;
                    case 2: return vk::Format::eR32G32Sfloat;
                    case 3: return vk::Format::eR32G32B32Sfloat;
                    case 4: return vk::Format::eR32G32B32A32Sfloat;
                    default: break;
                    }
                }
                if (input.scalarType == ShaderScalarType::SignedInteger)
                {
                    switch (input.components)
                    {
                    case 1: return vk::Format::eR32Sint;
                    case 2: return vk::Format::eR32G32Sint;
                    case 3: return vk::Format::eR32G32B32Sint;
                    case 4: return vk::Format::eR32G32B32A32Sint;
                    default: break;
                    }
                }
                if (input.scalarType == ShaderScalarType::UnsignedInteger)
                {
                    switch (input.components)
                    {
                    case 1: return vk::Format::eR32Uint;
                    case 2: return vk::Format::eR32G32Uint;
                    case 3: return vk::Format::eR32G32B32Uint;
                    case 4: return vk::Format::eR32G32B32A32Uint;
                    default: break;
                    }
                }
            }
            throw std::runtime_error("Unsupported reflected vertex input format.");
        }

        void CreatePipeline(
            vk::Format colorFormat,
            vk::Format depthFormat,
            std::span<const uint32_t> vertexSpirv,
            std::span<const uint32_t> fragmentSpirv,
            const ShaderReflection& reflection
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

            // 2. Vertex Input State (reflection keeps the legacy empty-input path intact)
            std::vector<ShaderVertexInput> vertexInputs = reflection.vertexInputs;
            std::ranges::sort(vertexInputs, {}, &ShaderVertexInput::location);
            std::vector<vk::VertexInputAttributeDescription> attributes;
            attributes.reserve(vertexInputs.size());
            uint32_t vertexStride = 0;
            for (const auto& input : vertexInputs)
            {
                attributes.emplace_back(input.location, 0, VertexFormat(input), vertexStride);
                vertexStride += input.components * (input.bitWidth / 8);
            }
            vk::VertexInputBindingDescription binding(0, vertexStride, vk::VertexInputRate::eVertex);
            vk::PipelineVertexInputStateCreateInfo vertexInputInfo(
                {}, vertexStride == 0 ? 0u : 1u,
                vertexStride == 0 ? nullptr : &binding,
                static_cast<uint32_t>(attributes.size()),
                attributes.data());

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

            // 10. Build reflected descriptor set and push-constant layout.
            uint32_t setCount = 0;
            for (const auto& descriptor : reflection.descriptorBindings)
            {
                setCount = std::max(setCount, descriptor.set + 1);
            }
            m_DescriptorSetLayouts.resize(setCount);
            for (uint32_t set = 0; set < setCount; ++set)
            {
                std::vector<vk::DescriptorSetLayoutBinding> bindings;
                for (const auto& descriptor : reflection.descriptorBindings)
                {
                    if (descriptor.set != set) continue;
                    bindings.emplace_back(
                        descriptor.binding, ToVulkanDescriptorType(descriptor.type),
                        std::max(descriptor.descriptorCount, 1u),
                        ToVulkanShaderStages(descriptor.stages));
                }
                vk::DescriptorSetLayoutCreateInfo setInfo(
                    {}, static_cast<uint32_t>(bindings.size()), bindings.data());
                m_DescriptorSetLayouts[set] = logicalDevice.createDescriptorSetLayout(setInfo);
            }
            std::vector<vk::PushConstantRange> pushConstants;
            pushConstants.reserve(reflection.pushConstants.size());
            for (const auto& range : reflection.pushConstants)
            {
                pushConstants.emplace_back(ToVulkanShaderStages(range.stages), range.offset, range.size);
            }
            vk::PipelineLayoutCreateInfo pipelineLayoutInfo(
                {}, static_cast<uint32_t>(m_DescriptorSetLayouts.size()),
                m_DescriptorSetLayouts.data(), static_cast<uint32_t>(pushConstants.size()),
                pushConstants.data());
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
        std::vector<vk::DescriptorSetLayout> m_DescriptorSetLayouts;
    };
}
