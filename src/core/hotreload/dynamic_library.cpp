module;

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

module GPP.Core;

import :HotReload.DynamicLibrary;
import std;

namespace GPP
{
    std::string DynamicLibrary::GetPlatformError()
    {
#ifdef _WIN32
        const DWORD errorCode = GetLastError();
        if (errorCode == 0)
        {
            return "Unknown error";
        }
        LPSTR buffer = nullptr;
        const auto size = FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<LPSTR>(&buffer), 0, nullptr);
        std::string message(buffer, size);
        if (buffer)
        {
            LocalFree(buffer);
        }
        while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
        {
            message.pop_back();
        }
        return message;
#else
        const char* error = dlerror();
        return error ? std::string(error) : std::string("Unknown error");
#endif
    }

    DynamicLibrary::DynamicLibrary(const std::filesystem::path& path)
        : m_Path(path)
    {
#ifdef _WIN32
        m_Handle = static_cast<void*>(LoadLibraryW(path.wstring().c_str()));
#else
        m_Handle = dlopen(path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
        if (!m_Handle)
        {
            throw std::runtime_error(std::format(
                "Failed to load dynamic library '{}': {}", path.string(), GetPlatformError()));
        }
    }

    DynamicLibrary::~DynamicLibrary()
    {
        Unload();
    }

    DynamicLibrary::DynamicLibrary(DynamicLibrary&& other) noexcept
        : m_Handle(std::exchange(other.m_Handle, nullptr)),
          m_Path(std::exchange(other.m_Path, {}))
    {
    }

    DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& other) noexcept
    {
        if (this != &other)
        {
            Unload();
            m_Handle = std::exchange(other.m_Handle, nullptr);
            m_Path = std::exchange(other.m_Path, {});
        }
        return *this;
    }

    void* DynamicLibrary::GetSymbolRaw(const std::string& name) const noexcept
    {
        if (!m_Handle)
        {
            return nullptr;
        }
#ifdef _WIN32
        return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(m_Handle), name.c_str()));
#else
        return dlsym(m_Handle, name.c_str());
#endif
    }

    void DynamicLibrary::Unload() noexcept
    {
        if (!m_Handle)
        {
            return;
        }
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(m_Handle));
#else
        dlclose(m_Handle);
#endif
        m_Handle = nullptr;
    }
}
