module GPP.Graphics;

import std;
import vulkan;
import GPP.Core;
import :Vulkan;
import :Assets.Image;
import :Assets.Texture;

namespace GPP
{
    namespace
    {
        constexpr auto kPollInterval = std::chrono::milliseconds(500);
        constexpr std::size_t kSrgbEncodeSteps = 1 << 14;

        struct SrgbTables
        {
            std::array<float, 256> Decode{};
            std::array<std::uint8_t, kSrgbEncodeSteps + 1> Encode{};

            SrgbTables()
            {
                for (std::size_t i = 0; i < Decode.size(); ++i)
                {
                    const float c = static_cast<float>(i) / 255.0f;
                    Decode[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
                }
                for (std::size_t i = 0; i < Encode.size(); ++i)
                {
                    const float l = static_cast<float>(i) / static_cast<float>(kSrgbEncodeSteps);
                    const float c = l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
                    Encode[i] = static_cast<std::uint8_t>(std::clamp(std::lround(c * 255.0f), 0L, 255L));
                }
            }
        };

        const SrgbTables& Srgb()
        {
            static const SrgbTables tables;
            return tables;
        }

        std::uint16_t ToHalf(const float value)
        {
            if (std::isnan(value)) return 0;
            const auto bits = std::bit_cast<std::uint32_t>(value);
            const auto sign = static_cast<std::uint16_t>((bits >> 16) & 0x8000u);
            const int exponent = static_cast<int>((bits >> 23) & 0xffu) - 127 + 15;
            const std::uint32_t mantissa = bits & 0x7fffffu;
            if (exponent >= 31) return sign | 0x7bffu;
            if (exponent <= 0)
            {
                if (exponent < -10) return sign;
                const std::uint32_t full = mantissa | 0x800000u;
                const int shift = 14 - exponent;
                auto half = static_cast<std::uint32_t>(full >> shift);
                half += (full >> (shift - 1)) & 1u;
                return static_cast<std::uint16_t>(sign | half);
            }
            auto half = static_cast<std::uint32_t>((exponent << 10) | (mantissa >> 13));
            half += (mantissa >> 12) & 1u;
            if (half >= 0x7c00u) half = 0x7bffu;
            return static_cast<std::uint16_t>(sign | half);
        }

        // 2x2 box; an odd last row or column is folded into its neighbour's average.
        template <class T, class Load, class Store>
        std::vector<T> Downsample(const std::vector<T>& src, const std::uint32_t w, const std::uint32_t h,
                                  const std::uint32_t channels, const std::uint32_t dw, const std::uint32_t dh,
                                  Load load, Store store)
        {
            std::vector<T> out(static_cast<std::size_t>(dw) * dh * channels);
            for (std::uint32_t y = 0; y < dh; ++y)
            {
                const std::uint32_t y0 = 2 * y;
                const std::uint32_t y1 = y == dh - 1 ? h : y0 + 2;
                for (std::uint32_t x = 0; x < dw; ++x)
                {
                    const std::uint32_t x0 = 2 * x;
                    const std::uint32_t x1 = x == dw - 1 ? w : x0 + 2;
                    const float inv = 1.0f / static_cast<float>((x1 - x0) * (y1 - y0));
                    for (std::uint32_t c = 0; c < channels; ++c)
                    {
                        float sum = 0.0f;
                        for (std::uint32_t sy = y0; sy < y1; ++sy)
                        {
                            for (std::uint32_t sx = x0; sx < x1; ++sx)
                                sum += load(c, src[(static_cast<std::size_t>(sy) * w + sx) * channels + c]);
                        }
                        out[(static_cast<std::size_t>(y) * dw + x) * channels + c] = store(c, sum * inv);
                    }
                }
            }
            return out;
        }

        template <class T>
        std::vector<std::byte> ToBytes(const std::vector<T>& values)
        {
            std::vector<std::byte> bytes(values.size() * sizeof(T));
            std::memcpy(bytes.data(), values.data(), bytes.size());
            return bytes;
        }

        std::vector<std::byte> HalfBytes(const std::vector<float>& values)
        {
            std::vector<std::uint16_t> halves(values.size());
            std::ranges::transform(values, halves.begin(), ToHalf);
            return ToBytes(halves);
        }

        struct Chain
        {
            std::uint32_t Skip = 0;
            std::uint32_t Count = 0;
        };

        Chain PlanChain(const std::uint32_t w, const std::uint32_t h, const std::uint32_t maxSize)
        {
            Chain chain;
            chain.Count = static_cast<std::uint32_t>(std::bit_width(std::max(w, h)));
            while (maxSize > 0 && chain.Skip + 1 < chain.Count
                   && std::max(std::max(w >> chain.Skip, 1u), std::max(h >> chain.Skip, 1u)) > maxSize)
                ++chain.Skip;
            return chain;
        }

        template <class T, class Load, class Store, class Pack>
        void BuildLevels(DecodedTexture& texture, const std::uint32_t maxSize, std::vector<T> level,
                         const std::uint32_t channels, Load load, Store store, Pack pack)
        {
            std::uint32_t w = texture.SourceWidth, h = texture.SourceHeight;
            const auto count = PlanChain(w, h, 0).Count;
            const auto skip = PlanChain(w, h, maxSize).Skip;
            texture.Width = std::max(w >> skip, 1u);
            texture.Height = std::max(h >> skip, 1u);
            for (std::uint32_t l = 0; l < count; ++l)
            {
                if (l >= skip) texture.Levels.push_back(pack(level));
                if (l + 1 == count) break;
                const std::uint32_t dw = std::max(w / 2, 1u), dh = std::max(h / 2, 1u);
                level = Downsample(level, w, h, channels, dw, dh, load, store);
                w = dw;
                h = dh;
            }
        }

        struct StbiFree
        {
            void operator()(void* p) const { stbi_image_free(p); }
        };

        std::string StbiError()
        {
            const char* reason = stbi_failure_reason();
            return reason ? reason : "unknown error";
        }

        std::expected<DecodedTexture, std::string> Decode(const std::filesystem::path& file, const TextureRole role,
                                                          const std::uint32_t maxSize)
        {
            std::error_code ec;
            if (!std::filesystem::is_regular_file(file, ec)) return std::unexpected("file not found");

            DecodedTexture texture;
            texture.Role = role;
            texture.Format = TextureFormat(role);
            const std::string name = file.string();
            int w = 0, h = 0, n = 0;

            const auto finish = [&](const int width, const int height)
            {
                texture.SourceWidth = static_cast<std::uint32_t>(width);
                texture.SourceHeight = static_cast<std::uint32_t>(height);
            };
            const auto count = [&] { return static_cast<std::size_t>(w) * static_cast<std::size_t>(h); };

            switch (role)
            {
            case TextureRole::Color:
            case TextureRole::Data:
            {
                const std::unique_ptr<stbi_uc, StbiFree> pixels(stbi_load(name.c_str(), &w, &h, &n, 4));
                if (!pixels) return std::unexpected(StbiError());
                finish(w, h);
                std::vector<std::uint8_t> level(pixels.get(), pixels.get() + count() * 4);
                if (role == TextureRole::Color)
                {
                    const auto& srgb = Srgb();
                    BuildLevels(texture, maxSize, std::move(level), 4,
                                [&](const std::uint32_t c, const std::uint8_t v)
                                { return c < 3 ? srgb.Decode[v] : static_cast<float>(v) / 255.0f; },
                                [&](const std::uint32_t c, const float v) -> std::uint8_t
                                {
                                    if (c == 3) return static_cast<std::uint8_t>(std::clamp(std::lround(v * 255.0f), 0L, 255L));
                                    const auto i = static_cast<std::size_t>(std::clamp(v, 0.0f, 1.0f)
                                                                            * static_cast<float>(kSrgbEncodeSteps) + 0.5f);
                                    return srgb.Encode[i];
                                },
                                [](const std::vector<std::uint8_t>& v) { return ToBytes(v); });
                }
                else
                {
                    BuildLevels(texture, maxSize, std::move(level), 4,
                                [](std::uint32_t, const std::uint8_t v) { return static_cast<float>(v); },
                                [](std::uint32_t, const float v) { return static_cast<std::uint8_t>(v + 0.5f); },
                                [](const std::vector<std::uint8_t>& v) { return ToBytes(v); });
                }
                break;
            }
            case TextureRole::Height:
            {
                const std::unique_ptr<stbi_us, StbiFree> pixels(stbi_load_16(name.c_str(), &w, &h, &n, 1));
                if (!pixels) return std::unexpected(StbiError());
                finish(w, h);
                BuildLevels(texture, maxSize, std::vector<std::uint16_t>(pixels.get(), pixels.get() + count()), 1,
                            [](std::uint32_t, const std::uint16_t v) { return static_cast<float>(v); },
                            [](std::uint32_t, const float v) { return static_cast<std::uint16_t>(v + 0.5f); },
                            [](const std::vector<std::uint16_t>& v) { return ToBytes(v); });
                break;
            }
            case TextureRole::Hdr:
            {
                const std::unique_ptr<float, StbiFree> pixels(stbi_loadf(name.c_str(), &w, &h, &n, 4));
                if (!pixels) return std::unexpected(StbiError());
                finish(w, h);
                BuildLevels(texture, maxSize, std::vector<float>(pixels.get(), pixels.get() + count() * 4), 4,
                            [](std::uint32_t, const float v) { return v; },
                            [](std::uint32_t, const float v) { return v; },
                            [](const std::vector<float>& v) { return HalfBytes(v); });
                break;
            }
            }
            return texture;
        }

        std::filesystem::file_time_type LastWrite(const std::filesystem::path& file)
        {
            std::error_code ec;
            const auto time = std::filesystem::last_write_time(file, ec);
            return ec ? std::filesystem::file_time_type::min() : time;
        }

        constexpr std::uint64_t Align4(const std::uint64_t value) { return (value + 3) & ~std::uint64_t{3}; }
    }

    vk::Format TextureFormat(const TextureRole role) noexcept
    {
        switch (role)
        {
        case TextureRole::Color: return vk::Format::eR8G8B8A8Srgb;
        case TextureRole::Data: return vk::Format::eR8G8B8A8Unorm;
        case TextureRole::Height: return vk::Format::eR16Unorm;
        case TextureRole::Hdr: return vk::Format::eR16G16B16A16Sfloat;
        }
        return vk::Format::eUndefined;
    }

    std::uint64_t DecodedTexture::Bytes() const noexcept
    {
        std::uint64_t total = 0;
        for (const auto& level : Levels) total += level.size();
        return total;
    }

    std::expected<DecodedTexture, std::string> DecodeTexture(const std::filesystem::path& file, const TextureRole role,
                                                             const std::uint32_t maxSize)
    {
        try
        {
            return Decode(file, role, maxSize);
        }
        catch (const std::exception& e)
        {
            return std::unexpected(e.what());
        }
    }

    TextureCache::TextureCache(std::shared_ptr<VulkanDevice> device, std::shared_ptr<IFileSystem> fileSystem,
                               std::shared_ptr<Logger> logger)
        : m_Device(std::move(device)), m_FileSystem(std::move(fileSystem)), m_Logger(std::move(logger))
    {
        m_DeviceLimit = m_Device->GetPhysicalDevice().getProperties().limits.maxImageDimension2D;
        m_Worker = std::jthread([this](const std::stop_token& stop) { WorkerLoop(stop); });
    }

    TextureCache::~TextureCache()
    {
        m_Worker.request_stop();
        m_Wake.notify_all();
    }

    std::uint32_t TextureCache::EffectiveMaxSize() const
    {
        return m_MaxSize > 0 ? std::min(m_MaxSize, m_DeviceLimit) : m_DeviceLimit;
    }

    void TextureCache::WorkerLoop(const std::stop_token& stop)
    {
        while (true)
        {
            Job job;
            {
                std::unique_lock lock(m_Mutex);
                if (!m_Wake.wait(lock, stop, [this] { return !m_Jobs.empty(); })) return;
                job = std::move(m_Jobs.front());
                m_Jobs.pop_front();
            }
            auto texture = DecodeTexture(job.File, job.Id.second, job.MaxSize);
            std::scoped_lock lock(m_Mutex);
            m_Done.push_back({std::move(job.Id), job.Generation, std::move(texture)});
        }
    }

    void TextureCache::Enqueue(const Key& id, Entry& entry)
    {
        if (!entry.Pending) ++m_Pending;
        entry.Pending = true;
        entry.Mtime = LastWrite(entry.File);
        ++entry.Generation;
        {
            std::scoped_lock lock(m_Mutex);
            m_Jobs.push_back({id, entry.File, entry.Generation, EffectiveMaxSize()});
        }
        m_Wake.notify_one();
    }

    const VulkanImage* TextureCache::Get(const std::string& path, const TextureRole role)
    {
        if (path.empty()) return nullptr;
        const Key id{path, role};
        auto it = m_Entries.find(id);
        if (it == m_Entries.end())
        {
            it = m_Entries.emplace(id, Entry{.File = m_FileSystem->ResolvePath(path), .Role = role}).first;
            Enqueue(id, it->second);
        }
        return it->second.Image.get();
    }

    void TextureCache::SetMaxSize(const std::uint32_t maxSize)
    {
        if (maxSize == m_MaxSize) return;
        m_MaxSize = maxSize;
        for (auto& [id, entry] : m_Entries) Enqueue(id, entry);
    }

    void TextureCache::Upload(Entry& entry, const Key& id, DecodedTexture&& texture, VulkanCommandPool& pool,
                              const vk::Queue queue)
    {
        std::vector<vk::BufferImageCopy> regions;
        std::uint64_t size = 0;
        std::uint32_t w = texture.Width, h = texture.Height;
        for (std::uint32_t level = 0; level < texture.Levels.size(); ++level)
        {
            regions.push_back(vk::BufferImageCopy{
                size, 0, 0, {vk::ImageAspectFlagBits::eColor, level, 0, 1}, {}, {w, h, 1}});
            size = Align4(size + texture.Levels[level].size());
            w = std::max(w / 2, 1u);
            h = std::max(h / 2, 1u);
        }
        std::vector<std::byte> packed(size);
        for (std::size_t level = 0; level < texture.Levels.size(); ++level)
            std::ranges::copy(texture.Levels[level], packed.begin() + static_cast<std::ptrdiff_t>(regions[level].bufferOffset));

        VulkanBuffer staging(m_Device, MakeStagingBufferSpecification(size), m_Logger);
        staging.Upload(packed.data(), size);
        const auto mips = static_cast<std::uint32_t>(texture.Levels.size());
        auto image = std::make_unique<VulkanImage>(
            m_Device,
            VulkanImageSpecification{
                .extent = {texture.Width, texture.Height, 1},
                .format = texture.Format,
                .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .mipLevels = mips,
                .createSampler = true,
                .samplerFilter = vk::Filter::eLinear,
                .samplerAddressMode = vk::SamplerAddressMode::eRepeat,
                .samplerAddressModeV = vk::SamplerAddressMode::eClampToEdge,
                .debugName = id.first
            },
            m_Logger);
        ImmediateSubmit(pool, queue, [&](const vk::CommandBuffer cmd)
        {
            TransitionImageLayout(cmd, image->GetImage(), texture.Format, vk::ImageLayout::eUndefined,
                                  vk::ImageLayout::eTransferDstOptimal, mips);
            cmd.copyBufferToImage(staging.GetBuffer(), image->GetImage(), vk::ImageLayout::eTransferDstOptimal, regions);
            TransitionImageLayout(cmd, image->GetImage(), texture.Format, vk::ImageLayout::eTransferDstOptimal,
                                  vk::ImageLayout::eShaderReadOnlyOptimal, mips);
        });

        if (entry.Image) m_Retired.push_back({std::move(entry.Image), RetireFrames});
        m_GpuBytes += texture.Bytes();
        m_GpuBytes -= entry.Bytes;
        entry.Bytes = texture.Bytes();
        entry.Image = std::move(image);
    }

    void TextureCache::Update(VulkanCommandPool& uploadPool, const vk::Queue queue)
    {
        for (auto& retired : m_Retired) --retired.Frames;
        std::erase_if(m_Retired, [](const Retired& retired) { return retired.Frames <= 0; });

        std::deque<Result> done;
        {
            std::scoped_lock lock(m_Mutex);
            done.swap(m_Done);
        }
        for (auto& result : done)
        {
            const auto it = m_Entries.find(result.Id);
            if (it == m_Entries.end() || it->second.Generation != result.Generation) continue;
            auto& entry = it->second;
            entry.Pending = false;
            --m_Pending;
            if (!result.Texture)
            {
                m_Logger->Warn("Texture '{}' failed to load: {}{}", result.Id.first, result.Texture.error(),
                               entry.Image ? ", keeping the previous image" : "");
                continue;
            }
            try
            {
                Upload(entry, result.Id, std::move(*result.Texture), uploadPool, queue);
            }
            catch (const std::exception& e)
            {
                m_Logger->Error("Texture '{}' upload failed: {}", result.Id.first, e.what());
            }
        }

        const auto now = std::chrono::steady_clock::now();
        if (now - m_LastPoll < kPollInterval) return;
        m_LastPoll = now;
        for (auto& [id, entry] : m_Entries)
        {
            if (!entry.Pending && LastWrite(entry.File) != entry.Mtime) Enqueue(id, entry);
        }
    }
}
