module GPP.Core;

import std;
import :IO.AtomicFile;
import :Platform.Relaunch;

namespace GPP
{
    std::expected<void, std::string> WriteFileAtomic(const std::filesystem::path& path, const std::string_view content)
    {
        static std::atomic<std::uint64_t> counter{0};

        std::error_code ec;
        if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);

        auto temporary = path;
        temporary += std::format(".{}.{}.tmp", CurrentProcessId(), counter.fetch_add(1));
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            if (!file) return std::unexpected(std::format("cannot open '{}' for writing", temporary.string()));
            file.write(content.data(), static_cast<std::streamsize>(content.size()));
            file.close();
            if (!file)
            {
                std::filesystem::remove(temporary, ec);
                return std::unexpected(std::format("cannot write '{}'", temporary.string()));
            }
        }
        std::filesystem::rename(temporary, path, ec);
        if (ec)
        {
            const auto message = ec.message();
            std::filesystem::remove(temporary, ec);
            return std::unexpected(std::format("cannot rename onto '{}': {}", path.string(), message));
        }
        return {};
    }
}
