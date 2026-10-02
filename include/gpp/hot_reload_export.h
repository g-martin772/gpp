#pragma once

// USAGE

/*

import GPP;
import std;
#include <gpp/hot_reload_export.h>

struct MyLayer : GPP::HotReloadableLayer
{
    using Dependencies = std::tuple<GPP::Logger>;
    explicit MyLayer(const std::shared_ptr<GPP::Logger>& logger) : HotReloadableLayer(logger) {}

    void OnAttach() override { m_Logger->Info("MyLayer attached"); }
    void OnUiRender() override {  }
};

GPP_DEFINE_HOT_RELOAD_LAYER(MyLayer)

*/

#if defined(_WIN32)
#define GPP_HOT_RELOAD_EXPORT extern "C" __declspec(dllexport)
#else
#define GPP_HOT_RELOAD_EXPORT extern "C" __attribute__((visibility("default")))
#endif

#define GPP_DEFINE_HOT_RELOAD_LAYER(LayerType)                                                         \
    GPP_HOT_RELOAD_EXPORT std::uint32_t GPP_HotReloadModuleAbiVersion()                                \
    {                                                                                                  \
        return GPP::kHotReloadAbiVersion;                                                              \
    }                                                                                                  \
    GPP_HOT_RELOAD_EXPORT void* GPP_CreateHotReloadModule(GPP::ServiceProvider* provider)              \
    {                                                                                                  \
        GPP::HotReloadableLayer* layer = GPP::ActivateRaw<LayerType>(*provider);                       \
        return static_cast<void*>(layer);                                                              \
    }                                                                                                   \
    GPP_HOT_RELOAD_EXPORT void GPP_DestroyHotReloadModule(void* instance)                               \
    {                                                                                                  \
        delete static_cast<GPP::HotReloadableLayer*>(instance);                                        \
    }
