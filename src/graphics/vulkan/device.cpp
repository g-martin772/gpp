module;
#include <vk_mem_alloc.h>
module GPP.Graphics;

import vulkan;
import GPP.Core;
import :Vulkan.Capabilities;
import :Vulkan.Device;

namespace GPP
{
    static constexpr float kGraphicsQueuePriorities[2] = {1.0f, 0.5f};

    static void AddQueueToCreateInfo(std::vector<vk::DeviceQueueCreateInfo>& queueInfos,
                                     uint32_t queueFamilyIndex,
                                     uint32_t* resultIndex)
    {
        for (uint32_t i = 0; i < queueInfos.size(); i++)
        {
            if (queueInfos[i].queueFamilyIndex == queueFamilyIndex)
            {
                *resultIndex = i;
                return;
            }
        }

        vk::DeviceQueueCreateInfo queueCreateInfo = {};
        queueCreateInfo.queueFamilyIndex = queueFamilyIndex;
        queueCreateInfo.queueCount = 1;
        static float queuePriority = 1.0f;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueInfos.push_back(queueCreateInfo);
        *resultIndex = queueInfos.size() - 1;
    }

    namespace
    {
        bool HasExtension(const std::vector<vk::ExtensionProperties>& extensions, const char* name)
        {
            return std::ranges::any_of(extensions, [name](const vk::ExtensionProperties& e)
            {
                return std::string_view(e.extensionName.data()) == name;
            });
        }

        GpuType ToGpuType(const vk::PhysicalDeviceType type)
        {
            switch (type)
            {
            case vk::PhysicalDeviceType::eDiscreteGpu: return GpuType::Discrete;
            case vk::PhysicalDeviceType::eIntegratedGpu: return GpuType::Integrated;
            case vk::PhysicalDeviceType::eVirtualGpu: return GpuType::Virtual;
            case vk::PhysicalDeviceType::eCpu: return GpuType::Cpu;
            default: return GpuType::Other;
            }
        }

        GpuCapabilities ProbeCapabilities(const vk::PhysicalDevice device)
        {
            const auto properties = device.getProperties();
            const auto extensions = device.enumerateDeviceExtensionProperties();

            GpuCapabilities caps;
            caps.DeviceName = properties.deviceName.data();
            caps.VendorId = properties.vendorID;
            caps.ApiVersion = properties.apiVersion;
            caps.Type = ToGpuType(properties.deviceType);
            caps.DriverVersion = std::format("{}.{}.{}", VK_VERSION_MAJOR(properties.driverVersion),
                                             VK_VERSION_MINOR(properties.driverVersion),
                                             VK_VERSION_PATCH(properties.driverVersion));
            const auto memory = device.getMemoryProperties();
            for (uint32_t i = 0; i < memory.memoryHeapCount; ++i)
            {
                if (memory.memoryHeaps[i].flags & vk::MemoryHeapFlagBits::eDeviceLocal)
                    caps.DeviceLocalBytes = std::max<std::uint64_t>(caps.DeviceLocalBytes, memory.memoryHeaps[i].size);
            }

            const bool hasAs = HasExtension(extensions, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)
                               && HasExtension(extensions, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
            const bool hasRayQueryExt = HasExtension(extensions, VK_KHR_RAY_QUERY_EXTENSION_NAME);
            const bool hasRtPipelineExt = HasExtension(extensions, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
            const bool hasAtomicFloatExt = HasExtension(extensions, VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME);
            caps.HdrMetadata = HasExtension(extensions, VK_EXT_HDR_METADATA_EXTENSION_NAME);

            vk::PhysicalDeviceFeatures2 features2;
            vk::PhysicalDeviceAccelerationStructureFeaturesKHR asFeatures;
            vk::PhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures;
            vk::PhysicalDeviceRayTracingPipelineFeaturesKHR rtPipelineFeatures;
            vk::PhysicalDeviceShaderAtomicFloatFeaturesEXT atomicFloatFeatures;
            vk::PhysicalDeviceVulkan12Features vulkan12Features;
            const bool api12 = properties.apiVersion >= VK_API_VERSION_1_2;
            void* head = nullptr;
            const auto link = [&head](auto& feature)
            {
                feature.pNext = head;
                head = &feature;
            };
            if (hasAs) link(asFeatures);
            if (hasAs && hasRayQueryExt) link(rayQueryFeatures);
            if (hasAs && hasRtPipelineExt) link(rtPipelineFeatures);
            if (hasAtomicFloatExt) link(atomicFloatFeatures);
            if (api12) link(vulkan12Features);
            features2.pNext = head;
            device.getFeatures2(&features2);

            caps.ShaderFloat64 = features2.features.shaderFloat64 == VK_TRUE;
            caps.AccelerationStructure = hasAs && asFeatures.accelerationStructure == VK_TRUE;
            caps.RayQuery = caps.AccelerationStructure && hasRayQueryExt && rayQueryFeatures.rayQuery == VK_TRUE;
            caps.RayTracingPipeline = caps.AccelerationStructure && hasRtPipelineExt
                                      && rtPipelineFeatures.rayTracingPipeline == VK_TRUE;
            caps.AtomicFloat = hasAtomicFloatExt && atomicFloatFeatures.shaderBufferFloat32AtomicAdd == VK_TRUE;
            caps.DescriptorIndexing = api12 && vulkan12Features.runtimeDescriptorArray == VK_TRUE
                                      && vulkan12Features.descriptorBindingPartiallyBound == VK_TRUE
                                      && vulkan12Features.shaderSampledImageArrayNonUniformIndexing == VK_TRUE
                                      && vulkan12Features.descriptorBindingVariableDescriptorCount == VK_TRUE;

            if (const char* disabled = std::getenv("GPP_GPU_DISABLE"))
            {
                std::string_view list(disabled);
                while (!list.empty())
                {
                    const auto comma = list.find(',');
                    caps.Disable(list.substr(0, comma));
                    list = comma == std::string_view::npos ? std::string_view{} : list.substr(comma + 1);
                }
            }
            return caps;
        }
    }

    void VulkanDevice::PickPhysicalDevice()
    {
        const std::vector<vk::PhysicalDevice>& devices = m_VulkanContext->GetPhysicalDevices();

        std::vector<GpuCandidate> candidates;
        std::vector<VulkanQueueIndices> candidateQueues;
        VulkanDevice::m_Logger->Info("Available Graphics Devices:");
        for (const auto& device : devices)
        {
            vk::PhysicalDeviceProperties properties = device.getProperties();
            vk::PhysicalDeviceFeatures features = device.getFeatures();
            std::vector<vk::QueueFamilyProperties> queueFamilyProperties = device.getQueueFamilyProperties();

            VulkanDevice::m_Logger->Trace("\t{}", properties.deviceName.data());
            VulkanDevice::m_Logger->Trace("\t\t Driver Version: {}.{}.{}",
                                          VK_VERSION_MAJOR(properties.driverVersion),
                                          VK_VERSION_MINOR(properties.driverVersion),
                                          VK_VERSION_PATCH(properties.driverVersion));
            VulkanDevice::m_Logger->Trace("\t\t Type: {}", vk::to_string(properties.deviceType));
            VulkanDevice::m_Logger->Trace("\t\t VendorId: {}", properties.vendorID);
            VulkanDevice::m_Logger->Trace("\t\t Max Viewports: {}", properties.limits.maxViewports);
            VulkanDevice::m_Logger->Trace("\t\t Max Framebuffer Size: {}, {}", properties.limits.maxFramebufferHeight,
                                          properties.limits.maxFramebufferWidth);
            VulkanDevice::m_Logger->Trace("\t\t Max Descriptorsets: {}", properties.limits.maxBoundDescriptorSets);
            VulkanDevice::m_Logger->Trace("\t\t Max memory allocations: {}",
                                          properties.limits.maxMemoryAllocationCount);
            VulkanDevice::m_Logger->Trace("\t\t Max dynamic Storage Buffers: {}",
                                          properties.limits.maxDescriptorSetStorageBuffersDynamic);
            VulkanDevice::m_Logger->Trace("\t\t Max dynamic Uniform Buffers: {}",
                                          properties.limits.maxDescriptorSetUniformBuffersDynamic);
            VulkanDevice::m_Logger->Trace("\t\t Max Storage Buffers: {}",
                                          properties.limits.maxDescriptorSetStorageBuffers);
            VulkanDevice::m_Logger->Trace("\t\t Max Uniform Buffers: {}",
                                          properties.limits.maxDescriptorSetUniformBuffers);
            VulkanDevice::m_Logger->Trace("\t\t Max Sampler: {}", properties.limits.maxDescriptorSetSamplers);
            VulkanDevice::m_Logger->Trace("\t\t Max input attachments: {}",
                                          properties.limits.maxDescriptorSetInputAttachments);
            VulkanDevice::m_Logger->Trace("\t\t Max sampled images: {}",
                                          properties.limits.maxDescriptorSetSampledImages);
            VulkanDevice::m_Logger->Trace("\t\t Max draw index: {}", properties.limits.maxDrawIndexedIndexValue);

            VulkanDevice::m_Logger->Trace("\t\t Supported device features:");
            VulkanDevice::m_Logger->Trace("\t\t\t TessellationShader: {}",
                                          (features.tessellationShader ? "Supported" : "Not Supported"));
            VulkanDevice::m_Logger->Trace("\t\t\t GeometryShader: {}",
                                          (features.geometryShader ? "Supported" : "Not Supported"));
            VulkanDevice::m_Logger->Trace("\t\t\t MultiViewport: {}",
                                          (features.multiViewport ? "Supported" : "Not Supported"));
            VulkanDevice::m_Logger->Trace("\t\t\t SamplerAnisotropy: {}",
                                          (features.samplerAnisotropy ? "Supported" : "Not Supported"));
            VulkanDevice::m_Logger->Trace("\t\t\t FillModeNonSolid: {}",
                                          (features.fillModeNonSolid ? "Supported" : "Not Supported"));
            VulkanDevice::m_Logger->Trace("\t\t\t SparseBinding: {}",
                                          (features.sparseBinding ? "Supported" : "Not Supported"));

            VulkanQueueIndices queueIndices;

            VulkanDevice::m_Logger->Trace("\t\t Supported queues:");
            for (uint32_t queueFamilyIndex = 0; queueFamilyIndex < queueFamilyProperties.size(); ++queueFamilyIndex)
            {
                const vk::QueueFamilyProperties& queueFamily = queueFamilyProperties[queueFamilyIndex];

                VulkanDevice::m_Logger->Trace("\t\t\t Queue Family Index: {}", queueFamilyIndex);
                VulkanDevice::m_Logger->Trace("\t\t\t\t Queue Count:: {}", queueFamily.queueCount);
                VulkanDevice::m_Logger->Trace("\t\t\t\t Queue Flags: {}", vk::to_string(queueFamily.queueFlags));

                if (m_Requirements.Graphics && queueIndices.Graphics == -1 && queueFamily.queueFlags &
                    vk::QueueFlagBits::eGraphics)
                {
                    queueIndices.Graphics = queueFamilyIndex;
                }

                if (m_Requirements.Compute && queueFamily.queueFlags & vk::QueueFlagBits::eCompute)
                {
                    queueIndices.Compute = queueFamilyIndex;
                }

                if (m_Requirements.Transfer && queueFamily.queueFlags & vk::QueueFlagBits::eTransfer)
                {
                    queueIndices.Transfer = queueFamilyIndex;
                }

                if (m_Requirements.Sparse && queueFamily.queueFlags & vk::QueueFlagBits::eSparseBinding)
                {
                    queueIndices.Sparse = queueFamilyIndex;
                }

                if (m_Requirements.Present && device.getSurfaceSupportKHR(queueFamilyIndex, m_Requirements.Surface))
                {
                    queueIndices.Present = queueFamilyIndex;
                }
            }

            GpuCandidate candidate;
            candidate.Caps = ProbeCapabilities(device);
            if (queueIndices.Graphics != static_cast<std::uint32_t>(-1) && properties.limits.timestampComputeAndGraphics)
            {
                candidate.Caps.TimestampValidBits = queueFamilyProperties[queueIndices.Graphics].timestampValidBits;
                candidate.Caps.TimestampPeriodNs = properties.limits.timestampPeriod;
            }
            candidate.Suitable = (!m_Requirements.Graphics || queueIndices.Graphics != -1)
                                 && (!m_Requirements.Compute || queueIndices.Compute != -1)
                                 && (!m_Requirements.Transfer || queueIndices.Transfer != -1)
                                 && (!m_Requirements.Present || queueIndices.Present != -1);
            if (m_Requirements.Present)
            {
                candidate.Suitable = candidate.Suitable &&
                    HasExtension(device.enumerateDeviceExtensionProperties(), VK_KHR_SWAPCHAIN_EXTENSION_NAME);
            }
            VulkanDevice::m_Logger->Info("\t{} [{}, {} GiB, score {}{}]", candidate.Caps.DeviceName,
                                         GpuTypeName(candidate.Caps.Type), candidate.Caps.DeviceLocalBytes >> 30,
                                         ScoreGpu(candidate.Caps), candidate.Suitable ? "" : ", unsuitable");
            VulkanDevice::m_Logger->Info("\t\t rayQuery={} accelerationStructure={} shaderFloat64={} atomicFloat={} descriptorIndexing={} hdrMetadata={}",
                                         candidate.Caps.RayQuery, candidate.Caps.AccelerationStructure,
                                         candidate.Caps.ShaderFloat64, candidate.Caps.AtomicFloat,
                                         candidate.Caps.DescriptorIndexing, candidate.Caps.HdrMetadata);
            candidates.push_back(std::move(candidate));
            candidateQueues.push_back(queueIndices);
        }

        const char* preferred = std::getenv("GPP_GPU");
        const auto chosen = ChooseGpu(candidates, preferred ? preferred : "");
        if (!chosen) return;

        const auto lower = [](std::string_view text)
        {
            std::string out(text);
            std::ranges::transform(out, out.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return out;
        };
        const bool forced = preferred && *preferred
                            && lower(candidates[*chosen].Caps.DeviceName).contains(lower(preferred));
        m_PhysicalDevice = devices[*chosen];
        m_QueueIndices = candidateQueues[*chosen];
        m_Capabilities = candidates[*chosen].Caps;
        m_Capabilities.Reason = DescribeChoice(candidates, *chosen, forced);
    }

    VulkanDevice::VulkanDevice(const DeviceRequirements& requirements,
                               const std::shared_ptr<VulkanContext>& context,
                               const std::shared_ptr<Logger>& logger)
        : m_Requirements(requirements), m_VulkanContext(context), m_Logger(logger)
    {
        PickPhysicalDevice();

        if (!m_PhysicalDevice)
        {
            m_Logger->Error("No suitable Vulkan physical device found!");
            return;
        }
        m_Logger->Info("Picked {}: {}", m_Capabilities.DeviceName, m_Capabilities.Reason);

        if (m_Requirements.Graphics && m_QueueIndices.Graphics == -1)
        {
            m_Logger->Error("Required Graphics queue not found!");
            return;
        }
        if (m_Requirements.Transfer && m_QueueIndices.Transfer == -1)
        {
            m_Logger->Error("Required Transfer queue not found!");
            return;
        }
        if (m_Requirements.Compute && m_QueueIndices.Compute == -1)
        {
            m_Logger->Error("Required Compute queue not found!");
            return;
        }
        if (m_Requirements.Sparse && m_QueueIndices.Sparse == -1)
        {
            m_Logger->Error("Required Sparse queue not found!");
            return;
        }
        if (m_Requirements.Present && m_QueueIndices.Present == -1)
        {
            m_Logger->Error("Required Present queue not found!");
            return;
        }

        std::vector<vk::DeviceQueueCreateInfo> deviceQueueInfos;
        bool wantBackgroundQueue = false;
        if (m_Requirements.Graphics)
        {
            AddQueueToCreateInfo(deviceQueueInfos, m_QueueIndices.Graphics, &m_GraphicsIndex);
            const auto familyProperties = m_PhysicalDevice.getQueueFamilyProperties();
            if (m_QueueIndices.Graphics < familyProperties.size() &&
                familyProperties[m_QueueIndices.Graphics].queueCount >= 2)
            {
                deviceQueueInfos[m_GraphicsIndex].queueCount = 2;
                deviceQueueInfos[m_GraphicsIndex].pQueuePriorities = kGraphicsQueuePriorities;
                wantBackgroundQueue = true;
            }
        }
        if (m_Requirements.Transfer)
            AddQueueToCreateInfo(deviceQueueInfos, m_QueueIndices.Transfer, &m_TransferIndex);
        if (m_Requirements.Compute)
            AddQueueToCreateInfo(deviceQueueInfos, m_QueueIndices.Compute, &m_ComputeIndex);
        if (m_Requirements.Sparse)
            AddQueueToCreateInfo(deviceQueueInfos, m_QueueIndices.Sparse, &m_SparseIndex);
        if (m_Requirements.Present)
            AddQueueToCreateInfo(deviceQueueInfos, m_QueueIndices.Present, &m_PresentIndex);

        m_Logger->Trace("Selected Queue indices:");
        m_Logger->Trace("\t Graphics: {} ({})", m_QueueIndices.Graphics, m_GraphicsIndex);
        m_Logger->Trace("\t Transfer: {} ({})", m_QueueIndices.Transfer, m_TransferIndex);
        m_Logger->Trace("\t Compute: {} ({})", m_QueueIndices.Compute, m_ComputeIndex);
        m_Logger->Trace("\t Sparse: {} ({})", m_QueueIndices.Sparse, m_SparseIndex);
        m_Logger->Trace("\t Present: {} ({})", m_QueueIndices.Present, m_PresentIndex);

        vk::DeviceCreateInfo deviceCreateInfo;
        deviceCreateInfo.queueCreateInfoCount = deviceQueueInfos.size();
        deviceCreateInfo.pQueueCreateInfos = deviceQueueInfos.data();

        vk::PhysicalDeviceFeatures deviceFeatures{};
        if (m_PhysicalDevice.getFeatures().fillModeNonSolid) deviceFeatures.fillModeNonSolid = VK_TRUE;
        if (m_PhysicalDevice.getFeatures().samplerAnisotropy) deviceFeatures.samplerAnisotropy = VK_TRUE;
        if (m_PhysicalDevice.getFeatures().wideLines) deviceFeatures.wideLines = VK_TRUE;
        deviceCreateInfo.pEnabledFeatures = &deviceFeatures;

        std::vector<const char*> deviceExtensions = {
            VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
            VK_KHR_SPIRV_1_4_EXTENSION_NAME,
            VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME,
            VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME
        };
        if (m_Capabilities.AccelerationStructure)
        {
            deviceExtensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
            deviceExtensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
        }
        if (m_Capabilities.RayTracingPipeline) deviceExtensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
        if (m_Capabilities.RayQuery) deviceExtensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
        if (m_Capabilities.AtomicFloat) deviceExtensions.push_back(VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME);
        if (m_Requirements.Present)
        {
            deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
            // Needs the swapchain extension, so a headless device leaves it off
            if (m_Capabilities.HdrMetadata) deviceExtensions.push_back(VK_EXT_HDR_METADATA_EXTENSION_NAME);
        }
        deviceCreateInfo.enabledExtensionCount = deviceExtensions.size();
        deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();

        // Enable features using pNext chain
        vk::PhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures;
        bufferDeviceAddressFeatures.bufferDeviceAddress = VK_TRUE;

        vk::PhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures{};
        dynamicRenderingFeatures.dynamicRendering = VK_TRUE;
        dynamicRenderingFeatures.pNext = &bufferDeviceAddressFeatures;

        vk::PhysicalDeviceSynchronization2Features synchronization2Features{};
        synchronization2Features.synchronization2 = VK_TRUE;
        synchronization2Features.pNext = &dynamicRenderingFeatures;

        void* featureChain = &synchronization2Features;
        const auto link = [&featureChain](auto& feature)
        {
            feature.pNext = featureChain;
            featureChain = &feature;
        };
        vk::PhysicalDeviceAccelerationStructureFeaturesKHR asFeatures;
        asFeatures.accelerationStructure = VK_TRUE;
        if (m_Capabilities.AccelerationStructure) link(asFeatures);
        vk::PhysicalDeviceRayTracingPipelineFeaturesKHR rtFeatures;
        rtFeatures.rayTracingPipeline = VK_TRUE;
        if (m_Capabilities.RayTracingPipeline) link(rtFeatures);
        vk::PhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures;
        rayQueryFeatures.rayQuery = VK_TRUE;
        if (m_Capabilities.RayQuery) link(rayQueryFeatures);
        vk::PhysicalDeviceShaderAtomicFloatFeaturesEXT atomicFloatFeatures;
        atomicFloatFeatures.shaderBufferFloat32AtomicAdd = VK_TRUE;
        if (m_Capabilities.AtomicFloat) link(atomicFloatFeatures);
        vk::PhysicalDeviceDescriptorIndexingFeatures descriptorIndexingFeatures;
        descriptorIndexingFeatures.runtimeDescriptorArray = VK_TRUE;
        descriptorIndexingFeatures.descriptorBindingPartiallyBound = VK_TRUE;
        descriptorIndexingFeatures.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
        descriptorIndexingFeatures.descriptorBindingVariableDescriptorCount = VK_TRUE;
        if (m_Capabilities.DescriptorIndexing) link(descriptorIndexingFeatures);

        vk::PhysicalDeviceFeatures2 deviceFeatures2;
        const auto supported = m_PhysicalDevice.getFeatures();
        if (supported.fillModeNonSolid) deviceFeatures2.features.fillModeNonSolid = VK_TRUE;
        if (supported.samplerAnisotropy) deviceFeatures2.features.samplerAnisotropy = VK_TRUE;
        if (supported.wideLines) deviceFeatures2.features.wideLines = VK_TRUE;
        // Enable shaderInt64 for buffer addresses if needed, but usually implied or separate
        deviceFeatures2.features.shaderInt64 = VK_TRUE;
        // fp64 in shaders is optional (1/32 - 1/64 rate on consumer GPUs)
        if (m_Capabilities.ShaderFloat64) deviceFeatures2.features.shaderFloat64 = VK_TRUE;

        deviceFeatures2.pNext = featureChain;

        deviceCreateInfo.pEnabledFeatures = nullptr;
        deviceCreateInfo.pNext = &deviceFeatures2;

        try
        {
            m_Device = m_PhysicalDevice.createDevice(deviceCreateInfo);
        }
        catch (const vk::SystemError& err)
        {
            m_Logger->Error("Failed to create logical device: {}", err.what());
            return;
        }
        m_Logger->Trace("Created logical device: successful");

        VmaAllocatorCreateInfo allocatorInfo{};
        allocatorInfo.physicalDevice = m_PhysicalDevice;
        allocatorInfo.device = m_Device;
        allocatorInfo.instance = m_VulkanContext->GetInstance();
        allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
        allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
        if (vmaCreateAllocator(&allocatorInfo, &m_Allocator) != VK_SUCCESS)
        {
            m_Logger->Error("Failed to create Vulkan Memory Allocator.");
            m_Device.destroy();
            m_Device = nullptr;
            return;
        }
        m_Logger->Trace("Created Vulkan Memory Allocator: successful");

        m_Dispatcher.init(static_cast<VkInstance>(m_VulkanContext->GetInstance()), vkGetInstanceProcAddr,
                          static_cast<VkDevice>(m_Device), vkGetDeviceProcAddr);

        for (auto& queueInfo : deviceQueueInfos)
        {
            vk::Queue queue = m_Device.getQueue(queueInfo.queueFamilyIndex, 0);
            m_Queues.push_back(queue);
        }
        if (wantBackgroundQueue)
        {
            m_BackgroundQueue = m_Device.getQueue(m_QueueIndices.Graphics, 1);
        }
        m_Logger->Info("Background queue: {}",
                       wantBackgroundQueue ? "dedicated (graphics family queue 1, low priority)"
                                           : "shared with the graphics queue (device exposes a single queue)");

        const auto registerQueue = [this](const vk::Queue queue)
        {
            if (!queue) return;
            for (const auto& entry : m_QueueMutexes)
            {
                if (entry.first == queue) return;
            }
            m_QueueMutexes.emplace_back(queue, std::make_unique<std::mutex>());
        };
        for (const auto queue : m_Queues) registerQueue(queue);
        registerQueue(m_BackgroundQueue);

        if (m_Requirements.Present)
        {
            m_SurfaceCapabilities = m_PhysicalDevice.getSurfaceCapabilitiesKHR(m_Requirements.Surface);
            m_SurfaceFormats = m_PhysicalDevice.getSurfaceFormatsKHR(m_Requirements.Surface);
            m_SurfacePresentModes = m_PhysicalDevice.getSurfacePresentModesKHR(m_Requirements.Surface);
        }

        std::vector depthFormats = {
            vk::Format::eD32SfloatS8Uint,
            vk::Format::eD32Sfloat,
            vk::Format::eD24UnormS8Uint,
            vk::Format::eD16UnormS8Uint,
            vk::Format::eD16Unorm
        };

        auto flags = vk::FormatFeatureFlagBits::eDepthStencilAttachment;
        for (auto& depthFormat : depthFormats)
        {
            auto props = m_PhysicalDevice.getFormatProperties(depthFormat);
            if ((props.linearTilingFeatures & flags) == flags)
            {
                m_DepthFormat = depthFormat;
                break;
            }
            else if ((props.optimalTilingFeatures & flags) == flags)
            {
                m_DepthFormat = depthFormat;
                break;
            }
        }

        m_Logger->Info("Device and Queue allocation successful");
    }

    VulkanDevice::~VulkanDevice()
    {
        if (m_Device)
        {
            m_Device.waitIdle();
            if (m_Allocator)
            {
                DumpMemoryStats();
                vmaDestroyAllocator(m_Allocator);
                m_Allocator = nullptr;
            }
            m_Device.destroy();
        }
    }

    VulkanMemoryStats VulkanDevice::GetMemoryStats() const
    {
        VulkanMemoryStats result{};
        if (!m_Allocator)
            return result;
        VmaTotalStatistics stats{};
        vmaCalculateStatistics(m_Allocator, &stats);
        result.allocationCount = stats.total.statistics.allocationCount;
        result.blockCount = stats.total.statistics.blockCount;
        result.allocationBytes = stats.total.statistics.allocationBytes;
        result.blockBytes = stats.total.statistics.blockBytes;
        return result;
    }

    void VulkanDevice::DumpMemoryStats() const
    {
        if (!m_Allocator || !m_Logger)
            return;
        const auto stats = GetMemoryStats();
        m_Logger->Debug("VMA memory: {} allocations / {} blocks, {} allocated bytes / {} block bytes",
                        stats.allocationCount, stats.blockCount, stats.allocationBytes, stats.blockBytes);
    }

    std::mutex& VulkanDevice::GetQueueMutex(const vk::Queue queue) const
    {
        for (const auto& entry : m_QueueMutexes)
        {
            if (entry.first == queue)
            {
                return *entry.second;
            }
        }

        static std::mutex fallback; //?
        return fallback;
    }

    void VulkanDevice::WaitIdle()
    {
        if (!m_Device) return;
        
        std::vector<std::unique_lock<std::mutex>> locks;
        locks.reserve(m_QueueMutexes.size());
        for (const auto& entry : m_QueueMutexes)
        {
            locks.emplace_back(*entry.second);
        }
        m_Device.waitIdle();
    }

    VulkanFence::VulkanFence(vk::Device device, bool isSignaled)
    {
        m_Device = device;

        vk::FenceCreateInfo createInfo;
        createInfo.flags = isSignaled ? vk::FenceCreateFlagBits::eSignaled : vk::FenceCreateFlags();

        try
        {
            m_Fence = device.createFence(createInfo);
        }
        catch (const vk::SystemError& err)
        {
            throw new std::runtime_error("Failed to create vulkan fence: " + std::string(err.what()));
            return;
        }
    }

    VulkanFence::~VulkanFence()
    {
        if (m_Fence && m_Device)
            m_Device.destroyFence(m_Fence);
    }

    void VulkanFence::Reset() const
    {
        const vk::Result result = m_Device.resetFences(1, &m_Fence);
        if (result != vk::Result::eSuccess)
            throw new std::runtime_error("Failed to reset vulkan fence: " + vk::to_string(result));
    }

    void VulkanFence::Wait() const
    {
        const vk::Result result = m_Device.waitForFences(1, &m_Fence, VK_TRUE, UINT64_MAX);
        if (result != vk::Result::eSuccess)
            throw new std::runtime_error("Failed to create vulkan fence" + vk::to_string(result));
    }

    void VulkanFence::WaitAndReset() const
    {
        Wait();
        Reset();
    }

    VulkanSemaphore::VulkanSemaphore(vk::Device device)
    {
        m_Device = device;

        vk::SemaphoreCreateInfo createInfo;
        createInfo.flags = vk::SemaphoreCreateFlags();

        try
        {
            m_Semaphore = device.createSemaphore(createInfo);
        }
        catch (const vk::SystemError& err)
        {
            throw new std::runtime_error("Failed to create vulkan semaphore: " + std::string(err.what()));
        }
    }

    VulkanSemaphore::~VulkanSemaphore()
    {
        if (m_Semaphore && m_Device)
            m_Device.destroySemaphore(m_Semaphore);
    }

    std::shared_ptr<VulkanDevice> CreateHeadlessDevice(const std::shared_ptr<Logger>& logger)
    {
        try
        {
            auto context = std::make_shared<VulkanContext>(logger, nullptr);
            context->Init().get();
            auto device = std::make_shared<VulkanDevice>(
                DeviceRequirements{.Graphics = true, .Compute = false, .Transfer = true, .Sparse = false,
                                   .Present = false, .Surface = {}},
                context, logger);
            return device->GetDevice() ? device : nullptr;
        }
        catch (const std::exception& error)
        {
            if (logger) logger->Warn("Headless Vulkan device unavailable: {}", error.what());
            return nullptr;
        }
    }
}
