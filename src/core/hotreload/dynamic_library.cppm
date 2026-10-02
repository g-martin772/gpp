module;

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

export module GPP.Core:HotReload.DynamicLibrary;

import std;

namespace GPP
{
    export class DynamicLibrary
    {
    public:
        DynamicLibrary() = default;


        explicit DynamicLibrary(const std::filesystem::path& path);

        ~DynamicLibrary();

        DynamicLibrary(const DynamicLibrary&) = delete;
        DynamicLibrary& operator=(const DynamicLibrary&) = delete;

        DynamicLibrary(DynamicLibrary&& other) noexcept;
        DynamicLibrary& operator=(DynamicLibrary&& other) noexcept;

        [[nodiscard]] bool IsLoaded() const noexcept { return m_Handle != nullptr; }
        [[nodiscard]] const std::filesystem::path& GetPath() const noexcept { return m_Path; }

        // may be null if not found
        [[nodiscard]] void* GetSymbolRaw(const std::string& name) const noexcept;

        template <typename TSignature>
        [[nodiscard]] TSignature GetSymbol(const std::string& name) const noexcept
        {
            return reinterpret_cast<TSignature>(GetSymbolRaw(name));
        }

        void Unload() noexcept;

        [[nodiscard]] static std::string GetPlatformError();

    private:
        void* m_Handle = nullptr;
        std::filesystem::path m_Path;
    };
}
