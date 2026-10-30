module GPP.Graphics;

import std;
import vulkan;
import GPP.Core;
import :Vulkan;
import :Assets.Image;
import :Assets.Texture;
import :Assets.Watch;

namespace GPP
{
    namespace
    {
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
        : m_Device(std::move(device)), m_FileSystem(std::move(fileSystem)), m_Logger(std::move(logger)),
          m_Retired(m_Device), m_Watch(m_FileSystem, m_Logger)
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
            m_Paths.Add(m_Watch.Watch(it->second.File), id);
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

    void TextureCache::StartUpload(const Key& id, const std::uint64_t generation, DecodedTexture&& texture,
                                   VulkanCommandPool& pool, const vk::Queue queue)
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

        auto staging = std::make_shared<VulkanBuffer>(m_Device, MakeStagingBufferSpecification(size), m_Logger);
        staging->Upload(packed.data(), size);
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
        auto submit = SubmitAsync(pool, queue, [&](const vk::CommandBuffer cmd)
        {
            TransitionImageLayout(cmd, image->GetImage(), texture.Format, vk::ImageLayout::eUndefined,
                                  vk::ImageLayout::eTransferDstOptimal, mips);
            cmd.copyBufferToImage(staging->GetBuffer(), image->GetImage(), vk::ImageLayout::eTransferDstOptimal, regions);
            TransitionImageLayout(cmd, image->GetImage(), texture.Format, vk::ImageLayout::eTransferDstOptimal,
                                  vk::ImageLayout::eShaderReadOnlyOptimal, mips);
        }, {staging});
        if (!submit.Valid()) throw std::runtime_error("upload submit failed");
        m_InFlight.push_back({id, generation, texture.Bytes(), std::move(image), std::move(submit)});
    }

    void TextureCache::Install(InFlight& upload)
    {
        const auto it = m_Entries.find(upload.Id);
        if (it == m_Entries.end() || it->second.Generation != upload.Generation) return;
        auto& entry = it->second;
        entry.Pending = false;
        --m_Pending;
        if (entry.Image) m_Retired.Retire(std::move(entry.Image));
        m_GpuBytes += upload.Bytes;
        m_GpuBytes -= entry.Bytes;
        entry.Bytes = upload.Bytes;
        entry.Image = std::move(upload.Image);
        if (std::exchange(entry.FromDisk, false))
        {
            m_Logger->Notifications()->Post(NotificationKind::Info, "Texture reloaded", upload.Id.first, 2.0f,
                                            "texture:" + upload.Id.first);
        }
    }

    void TextureCache::Update(VulkanCommandPool& uploadPool, const vk::Queue queue)
    {
        if (m_Retired.Pending() > 0) m_Retired.Collect();

        for (auto& upload : m_InFlight)
        {
            if (upload.Submit.Done()) Install(upload);
        }
        std::erase_if(m_InFlight, [](const InFlight& upload) { return upload.Submit.Done(); });

        {
            std::scoped_lock lock(m_Mutex);
            for (auto& result : m_Done) m_Ready.push_back(std::move(result));
            m_Done.clear();
        }

        std::uint64_t spent = 0;
        while (!m_Ready.empty())
        {
            auto& result = m_Ready.front();
            const auto it = m_Entries.find(result.Id);
            if (it == m_Entries.end() || it->second.Generation != result.Generation)
            {
                m_Ready.pop_front();
                continue;
            }
            auto& entry = it->second;
            if (!result.Texture)
            {
                entry.Pending = false;
                --m_Pending;
                m_Logger->Warn("Texture '{}' failed to load: {}{}", result.Id.first, result.Texture.error(),
                               entry.Image ? ", keeping the previous image" : "");
                if (std::exchange(entry.FromDisk, false))
                {
                    m_Logger->Notifications()->Post(NotificationKind::Error, "Texture reload failed",
                                                    result.Id.first + "\n" + result.Texture.error(), 0.0f,
                                                    "texture:" + result.Id.first);
                }
                m_Ready.pop_front();
                continue;
            }
            const auto bytes = result.Texture->Bytes();
            if (!WithinUploadBudget(spent, bytes)) break;
            spent += bytes;
            try
            {
                StartUpload(result.Id, result.Generation, std::move(*result.Texture), uploadPool, queue);
            }
            catch (const std::exception& e)
            {
                entry.Pending = false;
                --m_Pending;
                m_Logger->Error("Texture '{}' upload failed: {}", result.Id.first, e.what());
            }
            m_Ready.pop_front();
        }

        for (const auto& path : m_Watch.TakeChanged())
        {
            for (const auto& key : m_Paths.Find(path)) m_Stale.insert(key);
        }
        for (auto it = m_Stale.begin(); it != m_Stale.end();)
        {
            auto& entry = m_Entries.at(*it);
            if (entry.Pending)
            {
                ++it;
                continue;
            }
            entry.FromDisk = true;
            Enqueue(*it, entry);
            it = m_Stale.erase(it);
        }
    }
}
