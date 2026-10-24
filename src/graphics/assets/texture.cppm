export module GPP.Graphics:Assets.Texture;

import std;
import vulkan;
import GPP.Core;
import :Vulkan;

namespace GPP
{
    export enum class TextureRole
    {
        Color,  // R8G8B8A8_SRGB
        Data,   // R8G8B8A8_UNORM
        Height, // R16_UNORM
        Hdr     // R16G16B16A16_SFLOAT
    };

    export [[nodiscard]] vk::Format TextureFormat(TextureRole role) noexcept;

    // Mip chain built on the CPU; Levels[0] is Width x Height, the top levels above maxSize are dropped.
    export struct DecodedTexture
    {
        TextureRole Role = TextureRole::Color;
        vk::Format Format = vk::Format::eUndefined;
        std::uint32_t Width = 0;
        std::uint32_t Height = 0;
        std::uint32_t SourceWidth = 0;
        std::uint32_t SourceHeight = 0;
        std::vector<std::vector<std::byte>> Levels;

        [[nodiscard]] std::uint64_t Bytes() const noexcept;
    };

    // maxSize 0 = unlimited
    export [[nodiscard]] std::expected<DecodedTexture, std::string> DecodeTexture(
        const std::filesystem::path& file, TextureRole role, std::uint32_t maxSize);

    export inline constexpr std::uint64_t kTextureUploadBudgetBytes = 32ull << 20;
    export [[nodiscard]] constexpr bool WithinUploadBudget(const std::uint64_t spent, const std::uint64_t next,
                                                           const std::uint64_t budget = kTextureUploadBudgetBytes) noexcept
    {
        return spent == 0 || spent + next <= budget;
    }

    export class TextureCache
    {
    public:
        TextureCache(std::shared_ptr<VulkanDevice> device, std::shared_ptr<IFileSystem> fileSystem,
                     std::shared_ptr<Logger> logger);
        ~TextureCache();
        TextureCache(const TextureCache&) = delete;
        TextureCache& operator=(const TextureCache&) = delete;

        [[nodiscard]] const VulkanImage* Get(const std::string& path, TextureRole role);
        void Update(VulkanCommandPool& uploadPool, vk::Queue queue);
        void SetMaxSize(std::uint32_t maxSize);

        [[nodiscard]] std::size_t PendingCount() const noexcept { return m_Pending.load(); }
        [[nodiscard]] std::uint64_t GpuBytes() const noexcept { return m_GpuBytes.load(); }

    private:
        using Key = std::pair<std::string, TextureRole>;

        struct Entry
        {
            std::filesystem::path File;
            TextureRole Role = TextureRole::Color;
            std::unique_ptr<VulkanImage> Image;
            std::uint64_t Bytes = 0;
            std::uint64_t Generation = 0;
            std::filesystem::file_time_type Mtime{};
            bool Pending = false;
        };

        struct Job
        {
            Key Id;
            std::filesystem::path File;
            std::uint64_t Generation = 0;
            std::uint32_t MaxSize = 0;
        };

        struct Result
        {
            Key Id;
            std::uint64_t Generation = 0;
            std::expected<DecodedTexture, std::string> Texture;
        };

        struct Retired
        {
            std::unique_ptr<VulkanImage> Image;
            int Frames = 0;
        };

        struct InFlight
        {
            Key Id;
            std::uint64_t Generation = 0;
            std::uint64_t Bytes = 0;
            std::unique_ptr<VulkanImage> Image;
            SubmitHandle Submit;
        };

        void Enqueue(const Key& id, Entry& entry);
        void StartUpload(const Key& id, std::uint64_t generation, DecodedTexture&& texture, VulkanCommandPool& pool,
                         vk::Queue queue);
        void Install(InFlight& upload);
        [[nodiscard]] std::uint32_t EffectiveMaxSize() const;
        void WorkerLoop(const std::stop_token& stop);

        static constexpr int RetireFrames = 4;

        std::shared_ptr<VulkanDevice> m_Device;
        std::shared_ptr<IFileSystem> m_FileSystem;
        std::shared_ptr<Logger> m_Logger;
        std::uint32_t m_MaxSize = 0;
        std::uint32_t m_DeviceLimit = 0;
        std::map<Key, Entry> m_Entries;
        std::vector<Retired> m_Retired;
        std::deque<Result> m_Ready;
        std::vector<InFlight> m_InFlight;
        std::chrono::steady_clock::time_point m_LastPoll{};
        std::atomic<std::size_t> m_Pending{0};
        std::atomic<std::uint64_t> m_GpuBytes{0};

        std::mutex m_Mutex;
        std::condition_variable_any m_Wake;
        std::deque<Job> m_Jobs;
        std::deque<Result> m_Done;
        std::jthread m_Worker;
    };
}
