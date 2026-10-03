module;
#include <nfd.h>
export module GPP.Graphics:UI.FileDialog;

import std;
import GPP.Core;

namespace GPP
{
    export struct FileDialogFilter
    {
        std::string Name;
        std::string Spec;
    };

    export class FileDialog : public IService
    {
    public:
        using Dependencies = std::tuple<Logger>;
        explicit FileDialog(std::shared_ptr<Logger> logger) : m_Logger(std::move(logger))
        {
        }

        [[nodiscard]] std::optional<std::filesystem::path> OpenFile(
            const std::vector<FileDialogFilter>& filters = {},
            const std::filesystem::path& defaultPath = {}) const;

        [[nodiscard]] std::optional<std::filesystem::path> SaveFile(
            const std::vector<FileDialogFilter>& filters = {},
            const std::string& defaultName = {},
            const std::filesystem::path& defaultPath = {}) const;

        [[nodiscard]] std::optional<std::filesystem::path> PickFolder(
            const std::filesystem::path& defaultPath = {}) const;

    private:
        std::shared_ptr<Logger> m_Logger;
    };
}
