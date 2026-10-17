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
