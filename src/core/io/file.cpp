module;

#ifdef _WIN32
#include <windows.h>
#endif

#if defined(__linux__)
#include <unistd.h>
#include <linux/limits.h>
#endif

module GPP.Core;

import std;
import :IO.File;

namespace GPP
{
    IFileSystem::~IFileSystem() = default;

    std::filesystem::path FileSystem::GetBinaryDirectory() const noexcept
    {
        return m_BinaryDir;
    }

    std::filesystem::path FileSystem::GetWorkingDirectory() const noexcept
    {
        return m_WorkingDir;
    }

    void FileSystem::SetWorkingDirectory(const std::filesystem::path& path)
    {
        if (std::filesystem::exists(path) && std::filesystem::is_directory(path))
        {
            m_WorkingDir = std::filesystem::canonical(path);
        }
        else
        {
            throw std::runtime_error("Invalid working directory path: " + path.string());
        }
    }

    std::filesystem::path FileSystem::ResolvePath(const std::filesystem::path& relativePath, PathAnchor anchor) const
    {
        if (relativePath.is_absolute())
        {
            return relativePath;
        }

        switch (anchor)
        {
        case PathAnchor::WorkingDir:
            return m_WorkingDir / relativePath;
        case PathAnchor::BinaryDir:
            return m_BinaryDir / relativePath;
        case PathAnchor::Auto:
        default:
            auto workingPath = m_WorkingDir / relativePath;
            if (std::filesystem::exists(workingPath))
            {
                return workingPath;
            }
            return m_BinaryDir / relativePath;
        }
    }

    std::vector<std::byte> FileSystem::ReadAllBytes(const std::filesystem::path& path)
    {
        auto resolvedPath = ResolvePath(path);
        std::ifstream file(resolvedPath, std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            throw std::runtime_error("Failed to open file: " + resolvedPath.string());
        }

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<std::byte> buffer(size);
        if (!file.read(reinterpret_cast<char*>(buffer.data()), size))
        {
            throw std::runtime_error("Failed to read file bytes: " + resolvedPath.string());
        }

        return buffer;
    }

    std::string FileSystem::ReadAllText(const std::filesystem::path& path, FileEncoding encoding)
    {
        auto resolvedPath = ResolvePath(path);

        if (encoding == FileEncoding::Binary)
        {
            throw std::invalid_argument("Cannot read binary encoding as text string directly.");
        }

        std::ifstream file(resolvedPath, std::ios::binary);
        if (!file.is_open())
        {
            throw std::runtime_error("Failed to open text file: " + resolvedPath.string());
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string rawData = buffer.str();

        return ConvertToUTF8(rawData, encoding);
    }

    Task<std::vector<std::byte>> FileSystem::ReadAllBytesAsync(std::filesystem::path path)
    {
        co_await ResumeOn(ThreadPool::Instance());
        co_return ReadAllBytes(path);
    }

    Task<std::string> FileSystem::ReadAllTextAsync(std::filesystem::path path, FileEncoding encoding)
    {
        co_await ResumeOn(ThreadPool::Instance());
        co_return ReadAllText(path, encoding);
    }

    std::string FileSystem::ConvertToUTF8(const std::string& rawBytes, FileEncoding encoding) const
    {
        if (encoding == FileEncoding::UTF8)
        {
            return rawBytes;
        }

        if (encoding == FileEncoding::UTF16LE || encoding == FileEncoding::UTF16BE)
        {
            if (rawBytes.size() % 2 != 0)
            {
                throw std::runtime_error("Invalid UTF-16 stream size.");
            }

            std::u16string u16str;
            u16str.resize(rawBytes.size() / 2);
            std::memcpy(u16str.data(), rawBytes.data(), rawBytes.size());

            if (encoding == FileEncoding::UTF16BE)
            {
                for (auto& ch : u16str)
                {
                    ch = (ch >> 8) | (ch << 8);
                }
            }

            std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
            return convert.to_bytes(u16str);
        }

        return rawBytes;
    }

    FileSystem::FileSystem()
    {
        m_BinaryDir = ResolveBinaryDirectory();
        m_WorkingDir = std::filesystem::current_path();
    }

    std::filesystem::path FileSystem::ResolveBinaryDirectory() const
    {
#ifdef _WIN32
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(NULL, path, MAX_PATH);
        return std::filesystem::path(path).parent_path();
#elif defined(__linux__)
        char path[PATH_MAX];
        ssize_t count = readlink("/proc/self/exe", path, PATH_MAX);
        if (count == -1)
        {
            return std::filesystem::current_path();
        }
        return std::filesystem::path(std::string(path, count)).parent_path();
#else
        return std::filesystem::current_path();
#endif
    }
}
