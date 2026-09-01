module;
#include <vulkan/vulkan.hpp>
export module GPP.Graphics:Vulkan.Context;

import std;
import GPP.Core;

namespace GPP
{
    class WindowManager;

    export class VulkanContext : public IService
    {
    public:
        using Dependencies = std::tuple<Logger, WindowManager>;
        explicit VulkanContext(const std::shared_ptr<Logger>& logger, const std::shared_ptr<WindowManager>& windowManager);
        ~VulkanContext() override;

        Task<void> Init();
        bool IsInitialized() const noexcept { return m_IsInitialized; }

        VulkanContext(const VulkanContext&) = delete;
        VulkanContext& operator=(const VulkanContext&) = delete;
        VulkanContext(VulkanContext&& other) = delete;
        VulkanContext& operator=(VulkanContext&& other) noexcept = delete;

        [[nodiscard]] vk::Instance GetInstance() const noexcept { return m_Instance; }
    private:
        void CreateInstance();
        void SetupDebugMessenger();

        [[nodiscard]] vk::DebugUtilsMessengerCreateInfoEXT CreateDebugMessengerCreateInfo() const noexcept;
        [[nodiscard]] bool CheckValidationLayerSupport();
        [[nodiscard]] static std::vector<const char*> GetRequiredExtensions();

        vk::Instance m_Instance{nullptr};
        vk::DebugUtilsMessengerEXT m_DebugMessenger{nullptr};
        std::shared_ptr<Logger> m_Logger{nullptr};
        std::shared_ptr<WindowManager> m_WindowManager{nullptr};
        bool m_IsInitialized{false};
    };
}
