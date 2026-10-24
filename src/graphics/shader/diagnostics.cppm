export module GPP.Graphics:ShaderDiagnostics;

import std;

namespace GPP
{
    export struct ShaderErrorLocation
    {
        std::string File;
        int Line = 0;
        std::string Text;
    };

    export struct ShaderFailure
    {
        std::string Pipeline;
        std::string Source;
        std::string File;
        int Line = 0;
        std::string Message;
        std::uint64_t Serial = 0;
    };

    export [[nodiscard]] inline ShaderErrorLocation ParseShaderError(const std::string_view message)
    {
        ShaderErrorLocation result;
        std::size_t begin = 0;
        while (begin <= message.size())
        {
            auto end = message.find('\n', begin);
            if (end == std::string_view::npos) end = message.size();
            auto line = message.substr(begin, end - begin);
            begin = end + 1;

            if (line.starts_with("ERROR: ")) line.remove_prefix(7);
            std::size_t colon = 0;
            while ((colon = line.find(':', colon)) != std::string_view::npos)
            {
                std::size_t digits = colon + 1;
                while (digits < line.size() && line[digits] >= '0' && line[digits] <= '9') ++digits;
                if (digits > colon + 1 && digits < line.size() && line[digits] == ':' && colon > 0)
                {
                    int number = 0;
                    std::from_chars(line.data() + colon + 1, line.data() + digits, number);
                    result.File = std::string(line.substr(0, colon));
                    result.Line = number;
                    auto text = line.substr(digits + 1);
                    while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
                    result.Text = std::string(text);
                    return result;
                }
                ++colon;
            }
        }
        const auto newline = message.find('\n');
        result.Text = std::string(message.substr(0, newline));
        return result;
    }

    export [[nodiscard]] inline std::string DescribeShaderError(const std::string_view message)
    {
        const auto location = ParseShaderError(message);
        if (location.File.empty()) return location.Text;
        const auto file = std::filesystem::path(location.File).filename().string();
        return std::format("{}:{}: {}", file, location.Line, location.Text);
    }

    export class ShaderDiagnostics
    {
    public:
        static ShaderDiagnostics& Instance()
        {
            static ShaderDiagnostics instance;
            return instance;
        }

        void ReportFailure(const std::string& pipeline, const std::string& source, const std::string& message)
        {
            auto location = ParseShaderError(message);
            if (location.File.empty()) location.File = source;
            std::scoped_lock lock(m_Mutex);
            ShaderFailure failure{pipeline, source, std::move(location.File), location.Line, message, ++m_Serial};
            if (const auto existing = std::ranges::find(m_Failures, pipeline, &ShaderFailure::Pipeline);
                existing != m_Failures.end())
                *existing = std::move(failure);
            else
                m_Failures.push_back(std::move(failure));
            ++m_FailureCount;
            m_Revision.fetch_add(1, std::memory_order_release);
        }

        void ReportSuccess(const std::string& pipeline)
        {
            std::scoped_lock lock(m_Mutex);
            ++m_SuccessCount;
            if (std::erase_if(m_Failures, [&](const ShaderFailure& f) { return f.Pipeline == pipeline; }) > 0)
                m_Revision.fetch_add(1, std::memory_order_release);
        }

        void Forget(const std::string& pipeline)
        {
            std::scoped_lock lock(m_Mutex);
            if (std::erase_if(m_Failures, [&](const ShaderFailure& f) { return f.Pipeline == pipeline; }) > 0)
                m_Revision.fetch_add(1, std::memory_order_release);
        }

        [[nodiscard]] std::uint64_t Revision() const noexcept { return m_Revision.load(std::memory_order_acquire); }

        [[nodiscard]] std::vector<ShaderFailure> Failures() const
        {
            std::scoped_lock lock(m_Mutex);
            auto result = m_Failures;
            std::ranges::sort(result, std::greater{}, &ShaderFailure::Serial);
            return result;
        }

        [[nodiscard]] std::uint64_t FailureCount() const
        {
            std::scoped_lock lock(m_Mutex);
            return m_FailureCount;
        }

        [[nodiscard]] std::uint64_t SuccessCount() const
        {
            std::scoped_lock lock(m_Mutex);
            return m_SuccessCount;
        }

    private:
        mutable std::mutex m_Mutex;
        std::vector<ShaderFailure> m_Failures;
        std::uint64_t m_Serial = 0;
        std::uint64_t m_FailureCount = 0;
        std::uint64_t m_SuccessCount = 0;
        std::atomic<std::uint64_t> m_Revision{0};
    };
}
