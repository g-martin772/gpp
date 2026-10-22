export module GPP.Core:IO.AtomicFile;

import std;

namespace GPP
{
    export [[nodiscard]] std::expected<void, std::string> WriteFileAtomic(
        const std::filesystem::path& path, std::string_view content);
}
