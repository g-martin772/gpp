module;
#include <vulkan/vulkan.hpp>
//VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
module GPP.Graphics;

import std;
import GPP.Core;
import :Vulkan.Device;

namespace GPP
{
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

    void VulkanDevice::PickPhysicalDevice()
    {
        const std::vector<vk::PhysicalDevice>& devices = m_VulkanContext->GetPhysicalDevices();

        VulkanDevice::m_Logger->Info("Available Graphics Devices:");
        for (const auto& device : devices)
        {
            bool suitable = true;
            vk::PhysicalDeviceProperties properties = device.getProperties();
            vk::PhysicalDeviceFeatures features = device.getFeatures();
            std::vector<vk::QueueFamilyProperties> queueFamilyProperties = device.getQueueFamilyProperties();

            VulkanDevice::m_Logger->Info("\t{}", properties.deviceName.data());
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
            VulkanDevice::m_Logger->Trace("\t\t Max memory allocations: {}", properties.limits.maxMemoryAllocationCount);
            VulkanDevice::m_Logger->Trace("\t\t Max dynamic Storage Buffers: {}",
                                          properties.limits.maxDescriptorSetStorageBuffersDynamic);
            VulkanDevice::m_Logger->Trace("\t\t Max dynamic Uniform Buffers: {}",
                                          properties.limits.maxDescriptorSetUniformBuffersDynamic);
            VulkanDevice::m_Logger->Trace("\t\t Max Storage Buffers: {}", properties.limits.maxDescriptorSetStorageBuffers);
            VulkanDevice::m_Logger->Trace("\t\t Max Uniform Buffers: {}", properties.limits.maxDescriptorSetUniformBuffers);
            VulkanDevice::m_Logger->Trace("\t\t Max Sampler: {}", properties.limits.maxDescriptorSetSamplers);
            VulkanDevice::m_Logger->Trace("\t\t Max input attachments: {}", properties.limits.maxDescriptorSetInputAttachments);
            VulkanDevice::m_Logger->Trace("\t\t Max sampled images: {}", properties.limits.maxDescriptorSetSampledImages);
            VulkanDevice::m_Logger->Trace("\t\t Max draw index: {}", properties.limits.maxDrawIndexedIndexValue);

            VulkanDevice::m_Logger->Trace("\t\t Supported device features:");
            VulkanDevice::m_Logger->Trace("\t\t\t TessellationShader: {}",
                                          (features.tessellationShader ? "Supported" : "Not Supported"));
            VulkanDevice::m_Logger->Trace("\t\t\t GeometryShader: {}", (features.geometryShader ? "Supported" : "Not Supported"));
            VulkanDevice::m_Logger->Trace("\t\t\t MultiViewport: {}", (features.multiViewport ? "Supported" : "Not Supported"));
            VulkanDevice::m_Logger->Trace("\t\t\t SamplerAnisotropy: {}", (features.samplerAnisotropy ? "Supported" : "Not Supported"));
            VulkanDevice::m_Logger->Trace("\t\t\t FillModeNonSolid: {}", (features.fillModeNonSolid ? "Supported" : "Not Supported"));
            VulkanDevice::m_Logger->Trace("\t\t\t SparseBinding: {}", (features.sparseBinding ? "Supported" : "Not Supported"));

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

            if (suitable && !VulkanDevice::m_PhysicalDevice)
            {
                VulkanDevice::m_PhysicalDevice = device;
                VulkanDevice::m_QueueIndices = queueIndices;
                break;
            }
        }
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
        m_Logger->Info("Picked {}", m_PhysicalDevice.getProperties().deviceName.data());

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
        if (m_Requirements.Graphics)
            AddQueueToCreateInfo(deviceQueueInfos, m_QueueIndices.Graphics, &m_GraphicsIndex);
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

        std::vector deviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
            VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
            VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
            VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
            VK_KHR_SPIRV_1_4_EXTENSION_NAME,
            VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME
        };
        deviceCreateInfo.enabledExtensionCount = deviceExtensions.size();
        deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();

        // Enable features using pNext chain
        vk::PhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures;
        bufferDeviceAddressFeatures.bufferDeviceAddress = VK_TRUE;

        vk::PhysicalDeviceAccelerationStructureFeaturesKHR asFeatures;
        asFeatures.accelerationStructure = VK_TRUE;
        asFeatures.pNext = &bufferDeviceAddressFeatures;

        vk::PhysicalDeviceRayTracingPipelineFeaturesKHR rtFeatures;
        rtFeatures.rayTracingPipeline = VK_TRUE;
        rtFeatures.pNext = &asFeatures;

        vk::PhysicalDeviceFeatures2 deviceFeatures2;
        if (m_PhysicalDevice.getFeatures().fillModeNonSolid) deviceFeatures2.features.fillModeNonSolid = VK_TRUE;
        if (m_PhysicalDevice.getFeatures().samplerAnisotropy) deviceFeatures2.features.samplerAnisotropy = VK_TRUE;
        if (m_PhysicalDevice.getFeatures().wideLines) deviceFeatures2.features.wideLines = VK_TRUE;
        // Enable shaderInt64 for buffer addresses if needed, but usually implied or separate
        deviceFeatures2.features.shaderInt64 = VK_TRUE;

        deviceFeatures2.pNext = &rtFeatures;

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

        // Initialize Dynamic Dispatcher (Local)
        // m_Dispatcher = vk::detail::DispatchLoaderDynamic(m_Instance->GetInstance(), vkGetInstanceProcAddr, m_Device);
        // Initialize Default Dispatcher (Global)
        // VULKAN_HPP_DEFAULT_DISPATCHER.init(m_Device);

        for (auto& queueInfo : deviceQueueInfos)
        {
            vk::Queue queue = m_Device.getQueue(queueInfo.queueFamilyIndex, 0);
            m_Queues.push_back(queue);
        }

        m_SurfaceCapabilities = m_PhysicalDevice.getSurfaceCapabilitiesKHR(m_Requirements.Surface);
        m_SurfaceFormats = m_PhysicalDevice.getSurfaceFormatsKHR(m_Requirements.Surface);
        m_SurfacePresentModes = m_PhysicalDevice.getSurfacePresentModesKHR(m_Requirements.Surface);

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
        m_Device.destroy();
    }

    void VulkanDevice::WaitIdle()
    {
        m_Device.waitIdle();
    }
}
