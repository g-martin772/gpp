module;
#include <vulkan/vulkan.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

module GPP.Graphics;

import std;
import GPP.Core;
import :Vulkan.Context;

namespace GPP
{
#ifdef NDEBUG
    inline static constexpr bool s_EnableValidationLayers = false;
#else
    inline static constexpr bool s_EnableValidationLayers = true;
#endif

    inline static const std::vector s_ValidationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };

    static VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData)
    {
        auto* logger = static_cast<Logger*>(pUserData);
        if (!logger) return VK_FALSE;

        switch (messageSeverity)
        {
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
            logger->Error("[Vulkan Validation] {}", pCallbackData->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
            logger->Warn("[Vulkan Validation] {}", pCallbackData->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
            logger->Info("[Vulkan Validation] {}", pCallbackData->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
            logger->LogDebug("[Vulkan Validation] {}", pCallbackData->pMessage);
            break;
        default:
            logger->Info("[Vulkan Validation] {}", pCallbackData->pMessage);
            break;
        }

        return VK_FALSE;
    }


    VulkanContext::VulkanContext(const std::shared_ptr<Logger>& logger, const std::shared_ptr<WindowManager>& windowManager)
        : m_Logger(logger), m_WindowManager(windowManager)
    {
    }

    VulkanContext::~VulkanContext()
    {
        if (m_DebugMessenger)
        {
            auto pfnDestroyDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                m_Instance.getProcAddr("vkDestroyDebugUtilsMessengerEXT")
            );
            if (pfnDestroyDebugUtilsMessengerEXT)
            {
                pfnDestroyDebugUtilsMessengerEXT(m_Instance, m_DebugMessenger, nullptr);
            }
        }
        if (m_Instance)
        {
            m_Instance.destroy();
        }
    }

    Task<void> VulkanContext::Init()
    {
        co_await m_WindowManager->AwaitReady();
        CreateInstance();
        SetupDebugMessenger();
        m_PhysicalDevices = m_Instance.enumeratePhysicalDevices();
        m_IsInitialized = true;
        co_return;
    }

    void VulkanContext::CreateInstance()
    {
        if (s_EnableValidationLayers && !CheckValidationLayerSupport())
        {
            throw std::runtime_error("Vulkan validation layers requested, but not available on this system.");
        }

        vk::ApplicationInfo appInfo(
            "GPP App",
            VK_MAKE_API_VERSION(0, 1, 0, 0),
            "GPP Engine",
            VK_MAKE_API_VERSION(0, 1, 0, 0),
            VK_API_VERSION_1_3
        );

        auto requiredExtensions = GetRequiredExtensions();

        vk::InstanceCreateInfo createInfo(
            {},
            &appInfo,
            0, nullptr,
            static_cast<uint32_t>(requiredExtensions.size()), requiredExtensions.data()
        );

        vk::DebugUtilsMessengerCreateInfoEXT debugCreateInfo;
        if (s_EnableValidationLayers)
        {
            createInfo.setEnabledLayerCount(static_cast<uint32_t>(s_ValidationLayers.size()));
            createInfo.setPpEnabledLayerNames(s_ValidationLayers.data());

            debugCreateInfo = CreateDebugMessengerCreateInfo();
            createInfo.setPNext(&debugCreateInfo);
        }
        else
        {
            createInfo.setEnabledLayerCount(0);
            createInfo.setPNext(nullptr);
        }

        m_Instance = vk::createInstance(createInfo);
        m_Logger->Info("Vulkan Instance initialized successfully.");
    }

    void VulkanContext::SetupDebugMessenger()
    {
        if (!s_EnableValidationLayers) return;

        auto createInfo = CreateDebugMessengerCreateInfo();

        auto pfnCreateDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            m_Instance.getProcAddr("vkCreateDebugUtilsMessengerEXT")
        );

        if (pfnCreateDebugUtilsMessengerEXT)
        {
            VkDebugUtilsMessengerEXT rawMessenger;
            VkDebugUtilsMessengerCreateInfoEXT rawCreateInfo = createInfo;

            if (pfnCreateDebugUtilsMessengerEXT(m_Instance, &rawCreateInfo, nullptr, &rawMessenger) == VK_SUCCESS)
            {
                m_DebugMessenger = rawMessenger;
                m_Logger->Info("Vulkan validation debug messenger hooked successfully.");
            }
            else
            {
                throw std::runtime_error("Failed to register raw debug messenger handle.");
            }
        }
        else
        {
            m_Logger->Error(
                "Extension 'vkCreateDebugUtilsMessengerEXT' is not supported or active on this hardware.");
        }
    }

    vk::DebugUtilsMessengerCreateInfoEXT VulkanContext::CreateDebugMessengerCreateInfo() const noexcept
    {
        return vk::DebugUtilsMessengerCreateInfoEXT(
            {},
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eError |
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo,
            vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
            vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
            vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
            VulkanDebugCallback,
            m_Logger.get()
        );
    }

    bool VulkanContext::CheckValidationLayerSupport()
    {
        std::vector<vk::LayerProperties> availableLayers = vk::enumerateInstanceLayerProperties();

        for (const char* layerName : s_ValidationLayers)
        {
            bool layerFound = false;

            for (const auto& layerProperties : availableLayers)
            {
                if (std::strcmp(layerName, layerProperties.layerName) == 0)
                {
                    layerFound = true;
                    break;
                }
            }

            if (!layerFound)
            {
                return false;
            }
        }

        return true;
    }

    std::vector<const char*> VulkanContext::GetRequiredExtensions()
    {
        uint32_t sdlExtensionCount = 0;
        const char* const* sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&sdlExtensionCount);

        if (!sdlExtensions)
        {
            const auto error = std::format("Failed to query SDL3 Vulkan platform extensions: {}", SDL_GetError());
            throw std::runtime_error(error);
        }

        std::vector extensions(sdlExtensions, sdlExtensions + sdlExtensionCount);

        if (s_EnableValidationLayers)
        {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        return extensions;
    }
}
