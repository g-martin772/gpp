module;
#include <nfd.h>
module GPP.Graphics;

import :UI.FileDialog;
import std;

namespace GPP
{
    namespace
    {
        void EnsureInitialized()
        {
            thread_local bool initialized = false;
            if (!initialized)
            {
                NFD_Init();
                initialized = true;
            }
        }

        std::vector<nfdu8filteritem_t> ToNfdFilters(const std::vector<FileDialogFilter>& filters)
        {
            std::vector<nfdu8filteritem_t> result;
            result.reserve(filters.size());
            for (const auto& filter : filters)
            {
                result.push_back(nfdu8filteritem_t{filter.Name.c_str(), filter.Spec.c_str()});
            }
            return result;
        }

        void LogIfError(const std::shared_ptr<Logger>& logger, const nfdresult_t result)
        {
            if (result == NFD_ERROR && logger)
            {
                const auto* error = NFD_GetError();
                logger->Error("File dialog failed: {}", error ? error : "unknown error");
                NFD_ClearError();
            }
        }
    }

    std::optional<std::filesystem::path> FileDialog::OpenFile(
        const std::vector<FileDialogFilter>& filters, const std::filesystem::path& defaultPath) const
    {
        EnsureInitialized();
        const auto nfdFilters = ToNfdFilters(filters);
        const auto defaultPathStr = defaultPath.string();
        nfdu8char_t* outPath = nullptr;
        const auto result = NFD_OpenDialogU8(
            &outPath, nfdFilters.empty() ? nullptr : nfdFilters.data(),
            static_cast<nfdfiltersize_t>(nfdFilters.size()),
            defaultPath.empty() ? nullptr : defaultPathStr.c_str());
        LogIfError(m_Logger, result);
        if (result != NFD_OKAY)
        {
            return std::nullopt;
        }
        std::filesystem::path path(outPath);
        NFD_FreePathU8(outPath);
        return path;
    }

    std::optional<std::filesystem::path> FileDialog::SaveFile(
        const std::vector<FileDialogFilter>& filters, const std::string& defaultName,
        const std::filesystem::path& defaultPath) const
    {
        EnsureInitialized();
        const auto nfdFilters = ToNfdFilters(filters);
        const auto defaultPathStr = defaultPath.string();
        nfdu8char_t* outPath = nullptr;
        const auto result = NFD_SaveDialogU8(
            &outPath, nfdFilters.empty() ? nullptr : nfdFilters.data(),
            static_cast<nfdfiltersize_t>(nfdFilters.size()),
            defaultPath.empty() ? nullptr : defaultPathStr.c_str(),
            defaultName.empty() ? nullptr : defaultName.c_str());
        LogIfError(m_Logger, result);
        if (result != NFD_OKAY)
        {
            return std::nullopt;
        }
        std::filesystem::path path(outPath);
        NFD_FreePathU8(outPath);
        return path;
    }

    std::optional<std::filesystem::path> FileDialog::PickFolder(
        const std::filesystem::path& defaultPath) const
    {
        EnsureInitialized();
        const auto defaultPathStr = defaultPath.string();
        nfdu8char_t* outPath = nullptr;
        const auto result =
            NFD_PickFolderU8(&outPath, defaultPath.empty() ? nullptr : defaultPathStr.c_str());
        LogIfError(m_Logger, result);
        if (result != NFD_OKAY)
        {
            return std::nullopt;
        }
        std::filesystem::path path(outPath);
        NFD_FreePathU8(outPath);
        return path;
    }
}
