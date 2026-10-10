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
    export ImGuiConfigFlags ParseImGuiConfigFlags(const std::vector<std::string>& names)
    {
        static const std::unordered_map<std::string, ImGuiConfigFlags_> kFlagsByName{
            {"NavEnableKeyboard", ImGuiConfigFlags_NavEnableKeyboard},
            {"NavEnableGamepad", ImGuiConfigFlags_NavEnableGamepad},
            {"NoMouse", ImGuiConfigFlags_NoMouse},
            {"NoMouseCursorChange", ImGuiConfigFlags_NoMouseCursorChange},
            {"NoKeyboard", ImGuiConfigFlags_NoKeyboard},
            {"DockingEnable", ImGuiConfigFlags_DockingEnable},
            {"ViewportsEnable", ImGuiConfigFlags_ViewportsEnable},
            {"IsSRGB", ImGuiConfigFlags_IsSRGB},
            {"IsTouchScreen", ImGuiConfigFlags_IsTouchScreen},
        };

        ImGuiConfigFlags flags = ImGuiConfigFlags_None;
        for (const auto& name : names)
        {
            if (const auto it = kFlagsByName.find(name); it != kFlagsByName.end())
            {
                flags |= it->second;
            }
        }
        return flags;
    }

    export ImGuiContext* InitializeImGui(std::shared_ptr<VulkanDevice> device,
                                         Renderer::WindowResources* windowResources,
                                         std::shared_ptr<Logger> logger)
    {
        ImGui::SetCurrentContext(ImGui::CreateContext());
        auto* imguiContext = ImGui::GetCurrentContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = windowResources->ImGuiIniPath.empty()
                             ? nullptr
                             : windowResources->ImGuiIniPath.c_str();
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
        imguiInfo.ImageCount = 64; //?
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
        case KeyCode::Delete: return ImGuiKey_Delete;
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
        case KeyCode::Num0: return ImGuiKey_0;
        case KeyCode::Keypad0: return ImGuiKey_Keypad0;
        case KeyCode::Num1: return ImGuiKey_1;
        case KeyCode::Keypad1: return ImGuiKey_Keypad1;
        case KeyCode::Num2: return ImGuiKey_2;
        case KeyCode::Keypad2: return ImGuiKey_Keypad2;
        case KeyCode::Num3: return ImGuiKey_3;
        case KeyCode::Keypad3: return ImGuiKey_Keypad3;
        case KeyCode::Num4: return ImGuiKey_4;
        case KeyCode::Keypad4: return ImGuiKey_Keypad4;
        case KeyCode::Num5: return ImGuiKey_5;
        case KeyCode::Keypad5: return ImGuiKey_Keypad5;
        case KeyCode::Num6: return ImGuiKey_6;
        case KeyCode::Keypad6: return ImGuiKey_Keypad6;
        case KeyCode::Num7: return ImGuiKey_7;
        case KeyCode::Keypad7: return ImGuiKey_Keypad7;
        case KeyCode::Num8: return ImGuiKey_8;
        case KeyCode::Keypad8: return ImGuiKey_Keypad8;
        case KeyCode::Num9: return ImGuiKey_9;
        case KeyCode::Keypad9: return ImGuiKey_Keypad9;
        case KeyCode::F1: return ImGuiKey_F1;
        case KeyCode::F2: return ImGuiKey_F2;
        case KeyCode::F3: return ImGuiKey_F3;
        case KeyCode::F4: return ImGuiKey_F4;
        case KeyCode::F5: return ImGuiKey_F5;
        case KeyCode::F6: return ImGuiKey_F6;
        case KeyCode::F7: return ImGuiKey_F7;
        case KeyCode::F8: return ImGuiKey_F8;
        case KeyCode::F9: return ImGuiKey_F9;
        case KeyCode::F10: return ImGuiKey_F10;
        case KeyCode::F11: return ImGuiKey_F11;
        case KeyCode::F12: return ImGuiKey_F12;
        case KeyCode::F13: return ImGuiKey_F13;
        case KeyCode::F14: return ImGuiKey_F14;
        case KeyCode::F15: return ImGuiKey_F15;
        case KeyCode::F16: return ImGuiKey_F16;
        case KeyCode::F17: return ImGuiKey_F17;
        case KeyCode::F18: return ImGuiKey_F18;
        case KeyCode::F19: return ImGuiKey_F19;
        case KeyCode::F20: return ImGuiKey_F20;
        case KeyCode::F21: return ImGuiKey_F21;
        case KeyCode::F22: return ImGuiKey_F22;
        case KeyCode::F23: return ImGuiKey_F23;
        case KeyCode::F24: return ImGuiKey_F24;
        case KeyCode::Insert: return ImGuiKey_Insert;
        case KeyCode::Home: return ImGuiKey_Home;
        case KeyCode::End: return ImGuiKey_End;
        case KeyCode::PageUp: return ImGuiKey_PageUp;
        case KeyCode::PageDown: return ImGuiKey_PageDown;
        case KeyCode::CapsLock: return ImGuiKey_CapsLock;
        case KeyCode::NumLock: return ImGuiKey_NumLock;
        case KeyCode::ScrollLock: return ImGuiKey_ScrollLock;
        case KeyCode::PrintScreen: return ImGuiKey_PrintScreen;
        case KeyCode::Pause: return ImGuiKey_Pause;
        case KeyCode::LeftBracket: return ImGuiKey_LeftBracket;
        case KeyCode::RightBracket: return ImGuiKey_RightBracket;
        case KeyCode::Backslash: return ImGuiKey_Backslash;
        case KeyCode::Semicolon: return ImGuiKey_Semicolon;
        case KeyCode::Apostrophe: return ImGuiKey_Apostrophe;
        case KeyCode::Comma: return ImGuiKey_Comma;
        case KeyCode::Period: return ImGuiKey_Period;
        case KeyCode::Slash: return ImGuiKey_Slash;
        case KeyCode::Minus: return ImGuiKey_Minus;
        case KeyCode::Equals: return ImGuiKey_Equal;
        case KeyCode::Grave: return ImGuiKey_GraveAccent;
        case KeyCode::KeypadDivide: return ImGuiKey_KeypadDivide;
        case KeyCode::KeypadMultiply: return ImGuiKey_KeypadMultiply;
        case KeyCode::KeypadMinus: return ImGuiKey_KeypadSubtract;
        case KeyCode::KeypadPlus: return ImGuiKey_KeypadAdd;
        case KeyCode::KeypadEnter: return ImGuiKey_KeypadEnter;
        case KeyCode::KeypadPeriod: return ImGuiKey_KeypadDecimal;
        case KeyCode::KeypadEquals: return ImGuiKey_KeypadEqual;
        case KeyCode::Application:
        case KeyCode::Menu: return ImGuiKey_Menu;
        case KeyCode::LeftControl: return ImGuiKey_LeftCtrl;
        case KeyCode::LeftShift: return ImGuiKey_LeftShift;
        case KeyCode::LeftAlt: return ImGuiKey_LeftAlt;
        case KeyCode::LeftGui: return ImGuiKey_LeftSuper;
        case KeyCode::RightControl: return ImGuiKey_RightCtrl;
        case KeyCode::RightShift: return ImGuiKey_RightShift;
        case KeyCode::RightAlt: return ImGuiKey_RightAlt;
        case KeyCode::RightGui: return ImGuiKey_RightSuper;
        default: return ImGuiKey_None;
        }
    }
}
