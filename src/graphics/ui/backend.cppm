module;
#if  defined(__CLION_IDE__)
#include "vulkan/vulkan.h" // because CLion doesn't understand the module import of vulkan enums that are not marked enum class

#endif
export module GPP.Graphics:UI.Backend;

import std;
import GPP.Core;
import :Windowing.Events;
export import imgui;
import :Renderer;

namespace GPP
{
    export ImGuiContext* InitializeImGui(std::shared_ptr<VulkanDevice> device,
                                         Renderer::WindowResources* windowResources,
                                         std::shared_ptr<Logger> logger)
    {
        ImGui::SetCurrentContext(ImGui::CreateContext());
        auto* imguiContext = ImGui::GetCurrentContext();
        auto& io = ImGui::GetIO();
        //io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
        io.IniFilename = ".gpp/imgui.ini"; // TODO Why do you refuse my asset path and use some obfuscated idkw
        ImGui::StyleColorsDark();
        if (!ImGui_ImplSDL3_InitForVulkan(
            static_cast<SDL_Window*>(windowResources->Window->GetNativeHandle())))
        {
            ImGui::DestroyContext(imguiContext);
            throw std::runtime_error("Failed to initialize ImGui SDL3 backend.");
        }
        ImGui_ImplVulkan_InitInfo imguiInfo{};
        imguiInfo.ApiVersion = vk::ApiVersion13;
        imguiInfo.Instance = device->GetInstance();
        imguiInfo.PhysicalDevice = device->GetPhysicalDevice();
        imguiInfo.Device = device->GetDevice();
        imguiInfo.QueueFamily = device->GetQueueIndices().Graphics;
        imguiInfo.Queue = device->GetGraphicsQueue();
        imguiInfo.DescriptorPoolSize = 1024;
        imguiInfo.MinImageCount = 2;
        imguiInfo.ImageCount = windowResources->SwapChain->GetImageCount();
        imguiInfo.UseDynamicRendering = true;
        imguiInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        imguiInfo.PipelineInfoMain.PipelineRenderingCreateInfo.sType =
            VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        const auto colorFormat = static_cast<VkFormat>(
            windowResources->SwapChain->GetImageFormat());
        imguiInfo.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
        imguiInfo.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats =
            &colorFormat;
        imguiInfo.PipelineInfoMain.PipelineRenderingCreateInfo.depthAttachmentFormat =
            static_cast<VkFormat>(windowResources->SwapChain->GetDepthImageFormat());
        imguiInfo.PipelineInfoForViewports.PipelineRenderingCreateInfo.depthAttachmentFormat =
            VK_FORMAT_UNDEFINED;
        if (!ImGui_ImplVulkan_Init(&imguiInfo))
        {
            ImGui_ImplSDL3_Shutdown();
            ImGui::DestroyContext(imguiContext);
            throw std::runtime_error("Failed to initialize ImGui Vulkan backend.");
        }

        bool platformHasViewports = (io.BackendFlags & ImGuiBackendFlags_PlatformHasViewports);
        bool rendererHasViewports = (io.BackendFlags & ImGuiBackendFlags_RendererHasViewports);
        logger->Debug("Platform Viewports: {}, Renderer Viewports: {}", platformHasViewports, rendererHasViewports);

        return imguiContext;
    }

    export ImGuiKey ToImGuiKey(const KeyCode key)
    {
        switch (key)
        {
        case KeyCode::Return: return ImGuiKey_Enter;
        case KeyCode::Escape: return ImGuiKey_Escape;
        case KeyCode::Backspace: return ImGuiKey_Backspace;
        case KeyCode::Tab: return ImGuiKey_Tab;
        case KeyCode::Space: return ImGuiKey_Space;
        case KeyCode::Left: return ImGuiKey_LeftArrow;
        case KeyCode::Right: return ImGuiKey_RightArrow;
        case KeyCode::Up: return ImGuiKey_UpArrow;
        case KeyCode::Down: return ImGuiKey_DownArrow;
        case KeyCode::A: return ImGuiKey_A;
        case KeyCode::B: return ImGuiKey_B;
        case KeyCode::C: return ImGuiKey_C;
        case KeyCode::D: return ImGuiKey_D;
        case KeyCode::E: return ImGuiKey_E;
        case KeyCode::F: return ImGuiKey_F;
        case KeyCode::G: return ImGuiKey_G;
        case KeyCode::H: return ImGuiKey_H;
        case KeyCode::I: return ImGuiKey_I;
        case KeyCode::J: return ImGuiKey_J;
        case KeyCode::K: return ImGuiKey_K;
        case KeyCode::L: return ImGuiKey_L;
        case KeyCode::M: return ImGuiKey_M;
        case KeyCode::N: return ImGuiKey_N;
        case KeyCode::O: return ImGuiKey_O;
        case KeyCode::P: return ImGuiKey_P;
        case KeyCode::Q: return ImGuiKey_Q;
        case KeyCode::R: return ImGuiKey_R;
        case KeyCode::S: return ImGuiKey_S;
        case KeyCode::T: return ImGuiKey_T;
        case KeyCode::U: return ImGuiKey_U;
        case KeyCode::V: return ImGuiKey_V;
        case KeyCode::W: return ImGuiKey_W;
        case KeyCode::X: return ImGuiKey_X;
        case KeyCode::Y: return ImGuiKey_Y;
        case KeyCode::Z: return ImGuiKey_Z;
        default: return ImGuiKey_None;
        }
    }
}
