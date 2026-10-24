#include <catch2/catch_test_macros.hpp>

import GPP;
import glm;
import vulkan;
import std;

using namespace GPP;

TEST_CASE("TLAS transforms are the top three rows of the model matrix", "[graphics][acceleration]")
{
    glm::mat4 model(1.0f);
    model[0] = {1.0f, 2.0f, 3.0f, 0.0f};
    model[1] = {4.0f, 5.0f, 6.0f, 0.0f};
    model[2] = {7.0f, 8.0f, 9.0f, 0.0f};
    model[3] = {10.0f, 11.0f, 12.0f, 1.0f};

    const auto packed = ToTransformMatrix(model);
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            CHECK(packed.matrix[row][column] == model[column][row]);
        }
    }
    CHECK(packed.matrix[0][3] == 10.0f);
    CHECK(packed.matrix[2][0] == 3.0f);

    const glm::vec4 point(0.5f, -2.0f, 3.0f, 1.0f);
    const glm::vec4 expected = model * point;
    for (int row = 0; row < 3; ++row)
    {
        float value = 0.0f;
        for (int column = 0; column < 4; ++column) value += packed.matrix[row][column] * point[column];
        CHECK(value == expected[row]);
    }
}

TEST_CASE("TLAS instance records carry index, mask, flags and the BLAS address", "[graphics][acceleration]")
{
    TlasInstance instance;
    instance.Transform = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 3.0f));
    instance.CustomIndex = 0x1234567u;
    instance.Mask = 0x0Fu;
    instance.Blas = 0xDEADBEEF00ull;

    const auto packed = PackTlasInstance(instance);
    CHECK(sizeof(packed) == 64);
    CHECK(packed.instanceCustomIndex == 0x234567u);
    CHECK(packed.mask == 0x0Fu);
    CHECK(packed.instanceShaderBindingTableRecordOffset == 0u);
    CHECK(packed.flags == static_cast<VkGeometryInstanceFlagsKHR>(vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable));
    CHECK(packed.accelerationStructureReference == 0xDEADBEEF00ull);
    CHECK(packed.transform.matrix[1][3] == 2.0f);
}

TEST_CASE("TLAS refits when only transforms change and rebuilds when the instance set changes", "[graphics][acceleration][gpu]")
{
    const auto log = std::make_shared<Logger>();
    const auto device = CreateHeadlessDevice(log);
    if (!device || !device->GetCapabilities().AccelerationStructure)
    {
        WARN("No acceleration structure support; skipping");
        return;
    }
    const std::array<float, 9> positions{0, 0, 0, 1, 0, 0, 0, 1, 0};
    const std::array<std::uint32_t, 3> indices{0, 1, 2};
    const auto make = [&](const void* data, const vk::DeviceSize bytes)
    {
        auto spec = MakeStorageBufferSpecification(bytes, true);
        spec.usage |= vk::BufferUsageFlagBits::eShaderDeviceAddress
                      | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR;
        auto buffer = std::make_unique<VulkanBuffer>(device, spec, log);
        buffer->Upload(data, bytes);
        return buffer;
    };
    const auto vertices = make(positions.data(), sizeof(positions));
    const auto index = make(indices.data(), sizeof(indices));
    VulkanCommandPool pool(device, log, device->GetQueueIndices().Graphics);
    const auto queue = device->GetGraphicsQueue();
    Blas blas(device, pool, queue,
              BlasTriangles{.VertexAddress = vertices->GetDeviceAddress(), .VertexStride = 12, .VertexCount = 3,
                            .IndexAddress = index->GetDeviceAddress(), .IndexCount = 3});
    REQUIRE(blas.GetDeviceAddress() != 0);

    const auto instance = [&](const float x, const std::uint32_t custom = 0, const std::uint8_t mask = 0xFF)
    {
        TlasInstance out;
        out.Transform = glm::translate(glm::mat4(1.0f), glm::vec3(x, 0.0f, 0.0f));
        out.CustomIndex = custom;
        out.Mask = mask;
        out.Blas = blas.GetDeviceAddress();
        return out;
    };
    const auto build = [&](Tlas& tlas)
    {
        ImmediateSubmit(pool, queue, [&](const vk::CommandBuffer cmd) { tlas.RecordBuild(cmd); });
    };

    Tlas tlas(device);
    const std::array first{instance(0.0f), instance(2.0f, 1)};
    tlas.SetInstances(first);
    CHECK_FALSE(tlas.WillRefit());
    build(tlas);

    const std::array moved{instance(5.0f), instance(7.0f, 1)};
    tlas.SetInstances(moved);
    CHECK(tlas.WillRefit());
    build(tlas);
    CHECK(tlas.RefitCount() == 1);

    tlas.SetInstances(std::array{instance(5.0f), instance(7.0f, 1, 0x0F)});
    CHECK_FALSE(tlas.WillRefit());
    build(tlas);
    CHECK(tlas.RefitCount() == 0);

    tlas.SetInstances(std::array{instance(5.0f), instance(7.0f, 1, 0x0F), instance(9.0f, 2, 0x0F)});
    CHECK_FALSE(tlas.WillRefit());
    build(tlas);

    tlas.SetInstances(std::array{instance(6.0f), instance(8.0f, 1, 0x0F), instance(10.0f, 2, 0x0F)});
    CHECK(tlas.WillRefit());
    build(tlas);
    CHECK(tlas.RefitCount() == 1);
}
