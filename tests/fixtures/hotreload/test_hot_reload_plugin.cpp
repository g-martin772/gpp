// Minimal GPP.Core-only hot-reload plugin fixture used by tests/unit/hotreload/asset_tests.cpp and
// tests/unit/hotreload/dynamic_library_tests.cpp. It deliberately does NOT depend on GPP.Graphics:
// GPP::HotReloadAsset's mechanics (dlopen, ABI version check, Stage/Commit/Retire) are agnostic of
// whatever concrete type a plugin actually hands back, and this fixture proves it by handing back a
// plain `int*` instead of a HotReloadableLayer.
//
// Create/destroy calls are logged to the file named by the GPP_TEST_HOT_RELOAD_LOG environment
// variable (one line per call, "create" or "destroy") so tests can assert exactly when and how many
// times each one ran -- including across shadow-copy-induced separate library loads, where a static
// counter inside the plugin itself would not be shared between copies.
import GPP.Core;
import std;

namespace
{
    void LogEvent(const char* what)
    {
        const char* path = std::getenv("GPP_TEST_HOT_RELOAD_LOG");
        if (!path)
        {
            return;
        }
        std::ofstream log(path, std::ios::app);
        log << what << "\n";
    }
}

extern "C"
#if defined(_WIN32)
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
std::uint32_t GPP_HotReloadModuleAbiVersion()
{
    return GPP::kHotReloadAbiVersion;
}

extern "C"
#if defined(_WIN32)
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
void* GPP_CreateHotReloadModule(GPP::ServiceProvider*)
{
    LogEvent("create");
    return new int(777);
}

extern "C"
#if defined(_WIN32)
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
void GPP_DestroyHotReloadModule(void* instance)
{
    LogEvent("destroy");
    delete static_cast<int*>(instance);
}
