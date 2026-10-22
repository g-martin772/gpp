export module GPP.Core:IO.AtomicFile;

import std;

namespace GPP
{
    // Writes to a temp file next to `path`, then renames over it: readers never see a partial file.
    export [[nodiscard]] std::expected<void, std::string> WriteFileAtomic(
        const std::filesystem::path& path, std::string_view content);
}
