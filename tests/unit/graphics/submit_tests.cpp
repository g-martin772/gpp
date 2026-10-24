#include <catch2/catch_test_macros.hpp>

import GPP;
import vulkan;
import std;

using namespace GPP;

TEST_CASE("RecycleList hands items back in reverse order and respects its capacity", "[graphics][submit]")
{
    RecycleList<int> list(2);
    CHECK_FALSE(list.Take());
    CHECK(list.Give(1));
    CHECK(list.Give(2));
    CHECK_FALSE(list.Give(3));
    CHECK(list.Size() == 2);
    CHECK(list.Take() == 2);
    CHECK(list.Take() == 1);
    CHECK_FALSE(list.Take());
}

TEST_CASE("RecycleList refuses items after Close and returns what was cached", "[graphics][submit]")
{
    RecycleList<int> list;
    CHECK(list.Give(7));
    CHECK(list.Give(8));
    const auto drained = list.Close();
    CHECK(drained == std::vector<int>{7, 8});
    CHECK(list.Closed());
    CHECK_FALSE(list.Give(9));
    CHECK_FALSE(list.Take());
}

TEST_CASE("RecycleList is safe under concurrent Give and Take", "[graphics][submit]")
{
    RecycleList<int> list(64);
    std::atomic<int> given{0}, taken{0};
    {
        std::vector<std::jthread> workers;
        for (int t = 0; t < 4; ++t)
        {
            workers.emplace_back([&]
            {
                for (int i = 0; i < 1000; ++i)
                {
                    if (list.Give(i)) ++given;
                    if (list.Take()) ++taken;
                }
            });
        }
    }
    CHECK(static_cast<std::size_t>(given - taken) == list.Size());
}

TEST_CASE("Upload budget admits the first texture always and then stops at the budget", "[graphics][submit]")
{
    constexpr std::uint64_t budget = 100;
    CHECK(WithinUploadBudget(0, 1000, budget));
    CHECK(WithinUploadBudget(40, 60, budget));
    CHECK_FALSE(WithinUploadBudget(40, 61, budget));
    CHECK(kTextureUploadBudgetBytes == (32ull << 20));

    const std::vector<std::uint64_t> sizes{30, 30, 30, 30, 30};
    std::uint64_t spent = 0;
    std::size_t admitted = 0;
    for (const auto size : sizes)
    {
        if (!WithinUploadBudget(spent, size, budget)) break;
        spent += size;
        ++admitted;
    }
    CHECK(admitted == 3);
}

TEST_CASE("An empty SubmitHandle is invalid and reports done", "[graphics][submit]")
{
    const SubmitHandle handle;
    CHECK_FALSE(handle.Valid());
    CHECK(handle.Done());
    handle.Wait();
}

namespace
{
    struct Rig
    {
        std::shared_ptr<Logger> Log = std::make_shared<Logger>();
        std::shared_ptr<VulkanDevice> Device = CreateHeadlessDevice(Log);
    };
}

TEST_CASE("SubmitAsync runs the work, keeps resources alive until done and recycles fence and buffer",
          "[graphics][submit][gpu]")
{
    Rig rig;
    if (!rig.Device)
    {
        WARN("No headless Vulkan device; skipping");
        return;
    }
    constexpr std::uint32_t pattern = 0xA5A5A5A5u;
    constexpr vk::DeviceSize bytes = 256;
    VulkanCommandPool pool(rig.Device, rig.Log, rig.Device->GetQueueIndices().Graphics);
    const auto queue = rig.Device->GetBackgroundQueue();

    VulkanBuffer readback(rig.Device, MakeReadbackBufferSpecification(bytes), rig.Log);
    auto alive = std::make_shared<int>(1);
    std::weak_ptr<int> watch = alive;

    auto handle = SubmitAsync(pool, queue, [&](const vk::CommandBuffer cmd)
    {
        FillBuffer(cmd, readback.GetBuffer(), pattern, bytes);
    }, {std::move(alive)});
    REQUIRE(handle.Valid());
    handle.Wait();
    CHECK(handle.Done());
    CHECK(watch.expired());

    std::vector<std::uint32_t> out(bytes / sizeof(std::uint32_t));
    readback.Read(out.data(), bytes);
    CHECK(std::ranges::all_of(out, [](const auto v) { return v == pattern; }));
}

TEST_CASE("SubmitAsync handles can be polled to completion and copies share the state", "[graphics][submit][gpu]")
{
    Rig rig;
    if (!rig.Device)
    {
        WARN("No headless Vulkan device; skipping");
        return;
    }
    VulkanCommandPool pool(rig.Device, rig.Log, rig.Device->GetQueueIndices().Graphics);
    VulkanBuffer target(rig.Device, MakeReadbackBufferSpecification(64), rig.Log);

    auto handle = SubmitAsync(pool, rig.Device->GetBackgroundQueue(), [&](const vk::CommandBuffer cmd)
    {
        FillBuffer(cmd, target.GetBuffer(), 1u, 64);
    });
    const auto copy = handle;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!handle.Done() && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    CHECK(handle.Done());
    CHECK(copy.Done());
}

TEST_CASE("Repeated ImmediateSubmit and SubmitAsync reuse the pool without leaking or failing", "[graphics][submit][gpu]")
{
    Rig rig;
    if (!rig.Device)
    {
        WARN("No headless Vulkan device; skipping");
        return;
    }
    VulkanCommandPool pool(rig.Device, rig.Log, rig.Device->GetQueueIndices().Graphics);
    VulkanBuffer target(rig.Device, MakeReadbackBufferSpecification(64), rig.Log);
    const auto queue = rig.Device->GetGraphicsQueue();

    for (std::uint32_t i = 0; i < 200; ++i)
    {
        ImmediateSubmit(pool, queue, [&](const vk::CommandBuffer cmd) { FillBuffer(cmd, target.GetBuffer(), i, 64); });
        std::uint32_t first = 0;
        target.Read(&first, sizeof(first));
        REQUIRE(first == i);
    }
    std::vector<SubmitHandle> handles;
    for (int i = 0; i < 8; ++i)
    {
        handles.push_back(SubmitAsync(pool, queue, [&](const vk::CommandBuffer cmd) { FillBuffer(cmd, target.GetBuffer(), 7u, 64); }));
    }
    for (const auto& handle : handles) handle.Wait();
    handles.clear();
    ImmediateSubmit(pool, queue, [&](const vk::CommandBuffer cmd) { FillBuffer(cmd, target.GetBuffer(), 9u, 64); });
    std::uint32_t value = 0;
    target.Read(&value, sizeof(value));
    CHECK(value == 9u);
}

TEST_CASE("Dropping the last handle waits for the GPU and a failing record leaves the pool usable", "[graphics][submit][gpu]")
{
    Rig rig;
    if (!rig.Device)
    {
        WARN("No headless Vulkan device; skipping");
        return;
    }
    VulkanCommandPool pool(rig.Device, rig.Log, rig.Device->GetQueueIndices().Graphics);
    VulkanBuffer target(rig.Device, MakeReadbackBufferSpecification(64), rig.Log);
    const auto queue = rig.Device->GetGraphicsQueue();

    {
        auto handle = SubmitAsync(pool, queue, [&](const vk::CommandBuffer cmd) { FillBuffer(cmd, target.GetBuffer(), 5u, 64); });
        REQUIRE(handle.Valid());
    }
    std::uint32_t value = 0;
    target.Read(&value, sizeof(value));
    CHECK(value == 5u);

    CHECK_THROWS(SubmitAsync(pool, queue, [](const vk::CommandBuffer) { throw std::runtime_error("record failed"); }));
    ImmediateSubmit(pool, queue, [&](const vk::CommandBuffer cmd) { FillBuffer(cmd, target.GetBuffer(), 6u, 64); });
    target.Read(&value, sizeof(value));
    CHECK(value == 6u);
}

TEST_CASE("A batch of BLAS builds shares one scratch buffer and yields valid addresses", "[graphics][submit][acceleration][gpu]")
{
    Rig rig;
    if (!rig.Device || !rig.Device->GetCapabilities().AccelerationStructure)
    {
        WARN("No acceleration structure support; skipping");
        return;
    }
    const std::array<float, 9> positions{0, 0, 0, 1, 0, 0, 0, 1, 0};
    const std::array<std::uint32_t, 3> indices{0, 1, 2};
    const auto make = [&](const void* data, const vk::DeviceSize bytes, const vk::BufferUsageFlags extra)
    {
        auto spec = MakeStorageBufferSpecification(bytes, true);
        spec.usage |= vk::BufferUsageFlagBits::eShaderDeviceAddress | extra;
        auto buffer = std::make_unique<VulkanBuffer>(rig.Device, spec, rig.Log);
        buffer->Upload(data, bytes);
        return buffer;
    };
    constexpr auto input = vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR;
    const auto vertices = make(positions.data(), sizeof(positions), input);
    const auto index = make(indices.data(), sizeof(indices), input);
    const BlasTriangles triangles{.VertexAddress = vertices->GetDeviceAddress(), .VertexStride = 12, .VertexCount = 3,
                                  .IndexAddress = index->GetDeviceAddress(), .IndexCount = 3};

    VulkanCommandPool pool(rig.Device, rig.Log, rig.Device->GetQueueIndices().Graphics);
    std::vector<std::unique_ptr<Blas>> blases;
    std::vector<Blas*> pointers;
    for (int i = 0; i < 5; ++i)
    {
        blases.push_back(std::make_unique<Blas>(rig.Device, triangles));
        pointers.push_back(blases.back().get());
    }
    BuildBlasBatch(rig.Device, pool, rig.Device->GetBackgroundQueue(), pointers);
    for (const auto& blas : blases)
    {
        CHECK(blas->GetHandle());
        CHECK(blas->GetDeviceAddress() != 0);
    }
}

TEST_CASE("TextureCache installs textures asynchronously, a few per Update, and never hands out an unfinished image",
          "[graphics][submit][texture][gpu]")
{
    Rig rig;
    if (!rig.Device)
    {
        WARN("No headless Vulkan device; skipping");
        return;
    }
    const auto dir = std::filesystem::temp_directory_path() / "gpp_texture_cache_test";
    std::filesystem::create_directories(dir);
    constexpr int count = 6;
    std::vector<std::uint8_t> rgba(512 * 512 * 4, 200);
    for (int i = 0; i < count; ++i)
        REQUIRE(stbi_write_png((dir / std::format("t{}.png", i)).string().c_str(), 512, 512, 4, rgba.data(), 512 * 4) != 0);

    {
        auto fileSystem = std::make_shared<FileSystem>();
        TextureCache cache(rig.Device, fileSystem, rig.Log);
        VulkanCommandPool pool(rig.Device, rig.Log, rig.Device->GetQueueIndices().Graphics);
        const auto queue = rig.Device->GetBackgroundQueue();
        for (int i = 0; i < count; ++i)
            CHECK(cache.Get((dir / std::format("t{}.png", i)).string(), TextureRole::Color) == nullptr);
        CHECK(cache.PendingCount() == count);

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (cache.PendingCount() > 0 && std::chrono::steady_clock::now() < deadline)
        {
            cache.Update(pool, queue);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        REQUIRE(cache.PendingCount() == 0);
        for (int i = 0; i < count; ++i)
        {
            const auto* image = cache.Get((dir / std::format("t{}.png", i)).string(), TextureRole::Color);
            REQUIRE(image != nullptr);
            CHECK(image->GetSpecification().extent.width == 512);
        }
        CHECK(cache.GpuBytes() > 0);
    }
    std::filesystem::remove_all(dir);
}
