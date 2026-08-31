export module GPP.Core:IO.File;

import std;
import :Threading.Task;

namespace GPP
{
    export enum class PathAnchor
    {
        WorkingDir,
        BinaryDir,
        Auto
    };

    export enum class FileEncoding
    {
        Binary,
        UTF8,
        UTF16LE,
        UTF16BE
    };

    export class IFileSystem : public IService
    {
    public:
        virtual ~IFileSystem() override;

        [[nodiscard]] virtual std::filesystem::path GetBinaryDirectory() const noexcept = 0;
        [[nodiscard]] virtual std::filesystem::path GetWorkingDirectory() const noexcept = 0;
        virtual void SetWorkingDirectory(const std::filesystem::path& path) = 0;

        [[nodiscard]] virtual std::filesystem::path ResolvePath(
            const std::filesystem::path& relativePath,
            PathAnchor anchor = PathAnchor::Auto) const = 0;

        [[nodiscard]] virtual std::vector<std::byte> ReadAllBytes(const std::filesystem::path& path) = 0;
        [[nodiscard]] virtual std::string ReadAllText(const std::filesystem::path& path,
                                                      FileEncoding encoding = FileEncoding::UTF8) = 0;

        [[nodiscard]] virtual Task<std::vector<std::byte>> ReadAllBytesAsync(std::filesystem::path path) = 0;
        [[nodiscard]] virtual Task<std::string> ReadAllTextAsync(std::filesystem::path path,
                                                                 FileEncoding encoding = FileEncoding::UTF8) = 0;
    };


    export class FileSystem : public IFileSystem
    {
    public:
        FileSystem();

        [[nodiscard]] std::filesystem::path GetBinaryDirectory() const noexcept override;
        [[nodiscard]] std::filesystem::path GetWorkingDirectory() const noexcept override;

        void SetWorkingDirectory(const std::filesystem::path& path) override;

        [[nodiscard]] std::filesystem::path ResolvePath(
            const std::filesystem::path& relativePath,
            PathAnchor anchor = PathAnchor::Auto) const override;

        [[nodiscard]] std::vector<std::byte> ReadAllBytes(const std::filesystem::path& path) override;
        [[nodiscard]] std::string ReadAllText(const std::filesystem::path& path, FileEncoding encoding) override;

        [[nodiscard]] Task<std::vector<std::byte>> ReadAllBytesAsync(std::filesystem::path path) override;
        [[nodiscard]] Task<std::string> ReadAllTextAsync(std::filesystem::path path, FileEncoding encoding) override;
    private:
        [[nodiscard]] std::filesystem::path ResolveBinaryDirectory() const;
        [[nodiscard]] std::string ConvertToUTF8(const std::string& rawBytes, FileEncoding encoding) const;

        std::filesystem::path m_BinaryDir;
        std::filesystem::path m_WorkingDir;
    };
}
