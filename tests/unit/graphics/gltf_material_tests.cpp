#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

import GPP;
import glm;
import vulkan;
import std;

using namespace GPP;
using Catch::Approx;

namespace
{
    tinygltf::Value Obj(std::map<std::string, tinygltf::Value> fields)
    {
        return tinygltf::Value(std::move(fields));
    }

    tinygltf::Value Vec(std::initializer_list<double> values)
    {
        tinygltf::Value::Array array;
        for (const double v : values) array.emplace_back(v);
        return tinygltf::Value(std::move(array));
    }

    std::shared_ptr<VulkanImage> FakeImage()
    {
        return std::make_shared<VulkanImage>();
    }

    glm::vec3 Bitangent(const glm::vec3& n, const glm::vec4& t)
    {
        return glm::cross(n, glm::vec3(t)) * t.w;
    }
}

TEST_CASE("Texture roles map to the right Vulkan formats", "[graphics][gltf]")
{
    CHECK(GltfTextureFormat(GltfTextureRole::BaseColor) == vk::Format::eR8G8B8A8Srgb);
    CHECK(GltfTextureFormat(GltfTextureRole::Emissive) == vk::Format::eR8G8B8A8Srgb);
    CHECK(GltfTextureFormat(GltfTextureRole::SpecularColor) == vk::Format::eR8G8B8A8Srgb);
    CHECK(GltfTextureFormat(GltfTextureRole::MetallicRoughness) == vk::Format::eR8G8B8A8Unorm);
    CHECK(GltfTextureFormat(GltfTextureRole::Normal) == vk::Format::eR8G8B8A8Unorm);
    CHECK(GltfTextureFormat(GltfTextureRole::Occlusion) == vk::Format::eR8G8B8A8Unorm);
    CHECK(GltfTextureFormat(GltfTextureRole::Specular) == vk::Format::eR8G8B8A8Unorm);
}

TEST_CASE("Full mip chains cover the largest dimension", "[graphics][gltf][mips]")
{
    CHECK(MipLevelCount({1, 1, 1}) == 1);
    CHECK(MipLevelCount({2, 1, 1}) == 2);
    CHECK(MipLevelCount({5, 3, 1}) == 3);
    CHECK(MipLevelCount({1024, 1024, 1}) == 11);
    CHECK(MipLevelCount({1000, 300, 1}) == 10);
    CHECK(MipLevelCount({4096, 2048, 1}) == 13);
}

TEST_CASE("Vertex streams keep their layouts", "[graphics][gltf]")
{
    CHECK(sizeof(GltfVertex) == 32);
    CHECK(sizeof(GltfVertexExtra) == 32);
    CHECK(sizeof(GltfTextureSlotGpu) == 32);
    CHECK(sizeof(GltfMaterialGpu) == 304);
}

TEST_CASE("Tangents on a quad follow increasing u and the +Y of the normal map", "[graphics][gltf][tangents]")
{
    const std::vector<glm::vec3> positions{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
    const std::vector<glm::vec3> normals(4, glm::vec3(0, 0, 1));
    const std::vector<glm::vec2> uvs{{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    const std::vector<std::uint32_t> indices{0, 1, 2, 0, 2, 3};

    const auto tangents = GenerateTangents(positions, normals, uvs, indices);
    REQUIRE(tangents.size() == 4);
    for (const auto& t : tangents)
    {
        CHECK(t.x == Approx(1.0f));
        CHECK(t.y == Approx(0.0f).margin(1e-6));
        CHECK(t.z == Approx(0.0f).margin(1e-6));
        CHECK(t.w == 1.0f);
        const auto b = Bitangent(normals[0], t);
        CHECK(b.y == Approx(1.0f));
    }
}

TEST_CASE("Tangents handle mirrored UVs and rotated quads", "[graphics][gltf][tangents]")
{
    const std::vector<glm::vec3> positions{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
    const std::vector<glm::vec3> normals(4, glm::vec3(0, 0, 1));
    const std::vector<std::uint32_t> indices{0, 1, 2, 0, 2, 3};

    const std::vector<glm::vec2> mirrored{{1, 1}, {0, 1}, {0, 0}, {1, 0}};
    const auto flipped = GenerateTangents(positions, normals, mirrored, indices);
    for (const auto& t : flipped)
    {
        CHECK(t.x == Approx(-1.0f));
        CHECK(t.w == -1.0f);
    }

    const std::vector<glm::vec2> mirroredV{{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    const auto handed = GenerateTangents(positions, normals, mirroredV, indices);
    for (const auto& t : handed)
    {
        CHECK(t.x == Approx(1.0f));
        CHECK(t.w == -1.0f);
    }

    const std::vector<glm::vec2> rotated{{0, 0}, {0, 1}, {1, 1}, {1, 0}};
    const auto turned = GenerateTangents(positions, normals, rotated, indices);
    for (const auto& t : turned)
    {
        CHECK(t.y == Approx(1.0f).margin(1e-5));
        CHECK(glm::length(glm::vec3(t)) == Approx(1.0f));
    }
}

TEST_CASE("Tangents on a UV sphere are orthonormal and follow longitude", "[graphics][gltf][tangents]")
{
    constexpr int rings = 24;
    constexpr int segments = 32;
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> uvs;
    for (int r = 0; r <= rings; ++r)
    {
        const float v = static_cast<float>(r) / rings;
        const float theta = v * std::numbers::pi_v<float>;
        for (int s = 0; s <= segments; ++s)
        {
            const float u = static_cast<float>(s) / segments;
            const float phi = u * 2.0f * std::numbers::pi_v<float>;
            const glm::vec3 p(std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi));
            positions.push_back(p);
            normals.push_back(p);
            uvs.emplace_back(u, v);
        }
    }
    std::vector<std::uint32_t> indices;
    for (int r = 0; r < rings; ++r)
    {
        for (int s = 0; s < segments; ++s)
        {
            const auto a = static_cast<std::uint32_t>(r * (segments + 1) + s);
            const auto b = a + segments + 1;
            indices.insert(indices.end(), {a, a + 1, b, a + 1, b + 1, b});
        }
    }

    const auto tangents = GenerateTangents(positions, normals, uvs, indices);
    int checked = 0;
    for (std::size_t i = 0; i < positions.size(); ++i)
    {
        const auto& t = tangents[i];
        REQUIRE(std::isfinite(t.x + t.y + t.z + t.w));
        CHECK(glm::length(glm::vec3(t)) == Approx(1.0f).margin(1e-4));
        CHECK(glm::dot(glm::vec3(t), normals[i]) == Approx(0.0f).margin(1e-4));
        CHECK(std::abs(t.w) == 1.0f);
        if (positions[i].y > 0.95f || positions[i].y < -0.95f) continue;
        const glm::vec3 expected = glm::normalize(glm::vec3(-positions[i].z, 0.0f, positions[i].x));
        CHECK(glm::dot(glm::vec3(t), expected) > 0.98f);
        ++checked;
    }
    CHECK(checked > 400);
}

TEST_CASE("Tangents fall back to a perpendicular frame without usable UVs", "[graphics][gltf][tangents]")
{
    const std::vector<glm::vec3> positions{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    const std::vector<glm::vec3> normals(3, glm::normalize(glm::vec3(1, 1, 1)));
    const std::vector<std::uint32_t> indices{0, 1, 2};

    const auto none = GenerateTangents(positions, normals, {}, indices);
    const std::vector<glm::vec2> collapsed(3, glm::vec2(0.5f));
    const auto degenerate = GenerateTangents(positions, normals, collapsed, indices);
    for (const auto* set : {&none, &degenerate})
    {
        REQUIRE(set->size() == 3);
        for (std::size_t i = 0; i < 3; ++i)
        {
            const auto& t = (*set)[i];
            CHECK(glm::length(glm::vec3(t)) == Approx(1.0f));
            CHECK(glm::dot(glm::vec3(t), normals[i]) == Approx(0.0f).margin(1e-5));
            CHECK(t.w == 1.0f);
        }
    }
}

TEST_CASE("Material reader parses the P0 set and extensions", "[graphics][gltf][material]")
{
    tinygltf::Material source;
    source.name = "Hull";
    source.doubleSided = true;
    source.alphaMode = "MASK";
    source.alphaCutoff = 0.25;
    source.emissiveFactor = {1.0, 0.5, 0.25};
    source.pbrMetallicRoughness.baseColorFactor = {0.8, 0.7, 0.6, 0.5};
    source.pbrMetallicRoughness.metallicFactor = 0.9;
    source.pbrMetallicRoughness.roughnessFactor = 0.4;
    source.pbrMetallicRoughness.baseColorTexture.index = 3;
    source.pbrMetallicRoughness.baseColorTexture.texCoord = 1;
    source.pbrMetallicRoughness.metallicRoughnessTexture.index = 4;
    source.normalTexture.index = 5;
    source.normalTexture.scale = 0.3;
    source.occlusionTexture.index = 6;
    source.occlusionTexture.strength = 0.6;
    source.emissiveTexture.index = 7;
    source.emissiveTexture.extensions["KHR_texture_transform"] =
        Obj({{"offset", Vec({0.25, 0.5})}, {"rotation", tinygltf::Value(1.5)}, {"scale", Vec({2.0, 3.0})},
             {"texCoord", tinygltf::Value(1)}});
    source.extensions["KHR_materials_emissive_strength"] = Obj({{"emissiveStrength", tinygltf::Value(10.0)}});
    source.extensions["KHR_materials_ior"] = Obj({{"ior", tinygltf::Value(1.33)}});
    source.extensions["KHR_materials_specular"] =
        Obj({{"specularFactor", tinygltf::Value(0.7)},
             {"specularColorFactor", Vec({1.0, 0.9, 0.8})},
             {"specularTexture", Obj({{"index", tinygltf::Value(8)}})},
             {"specularColorTexture",
              Obj({{"index", tinygltf::Value(9)}, {"texCoord", tinygltf::Value(1)},
                   {"extensions", Obj({{"KHR_texture_transform", Obj({{"scale", Vec({4.0, 4.0})}})}})}})}});

    GltfTextureIndices indices;
    const auto material = ReadGltfMaterial(source, indices);

    CHECK(material.Name == "Hull");
    CHECK(material.DoubleSided);
    CHECK(material.AlphaMode == GltfAlphaMode::Mask);
    CHECK(material.AlphaCutoff == Approx(0.25f));
    CHECK(material.EmissiveFactor == glm::vec3(1.0f, 0.5f, 0.25f));
    CHECK(material.BaseColorFactor == glm::vec4(0.8f, 0.7f, 0.6f, 0.5f));
    CHECK(material.MetallicFactor == Approx(0.9f));
    CHECK(material.RoughnessFactor == Approx(0.4f));
    CHECK(material.NormalScale == Approx(0.3f));
    CHECK(material.OcclusionStrength == Approx(0.6f));
    CHECK(material.EmissiveStrength == Approx(10.0f));
    CHECK(material.Ior == Approx(1.33f));
    CHECK(material.SpecularFactor == Approx(0.7f));
    CHECK(material.SpecularColorFactor == glm::vec3(1.0f, 0.9f, 0.8f));

    CHECK(indices[static_cast<std::size_t>(GltfTextureRole::BaseColor)] == 3);
    CHECK(indices[static_cast<std::size_t>(GltfTextureRole::MetallicRoughness)] == 4);
    CHECK(indices[static_cast<std::size_t>(GltfTextureRole::Normal)] == 5);
    CHECK(indices[static_cast<std::size_t>(GltfTextureRole::Occlusion)] == 6);
    CHECK(indices[static_cast<std::size_t>(GltfTextureRole::Emissive)] == 7);
    CHECK(indices[static_cast<std::size_t>(GltfTextureRole::Specular)] == 8);
    CHECK(indices[static_cast<std::size_t>(GltfTextureRole::SpecularColor)] == 9);

    CHECK(material.Texture(GltfTextureRole::BaseColor).TexCoord == 1);
    const auto& emissive = material.Texture(GltfTextureRole::Emissive);
    CHECK(emissive.Transform.Offset == glm::vec2(0.25f, 0.5f));
    CHECK(emissive.Transform.Rotation == Approx(1.5f));
    CHECK(emissive.Transform.Scale == glm::vec2(2.0f, 3.0f));
    CHECK(emissive.TexCoord == 1);
    CHECK(material.Texture(GltfTextureRole::SpecularColor).TexCoord == 1);
    CHECK(material.Texture(GltfTextureRole::SpecularColor).Transform.Scale == glm::vec2(4.0f));
    CHECK(material.Texture(GltfTextureRole::Normal).Transform.Scale == glm::vec2(1.0f));
}

TEST_CASE("Material reader defaults match the glTF spec", "[graphics][gltf][material]")
{
    GltfTextureIndices indices;
    const auto material = ReadGltfMaterial(tinygltf::Material{}, indices);
    CHECK(material.BaseColorFactor == glm::vec4(1.0f));
    CHECK(material.MetallicFactor == 1.0f);
    CHECK(material.RoughnessFactor == 1.0f);
    CHECK(material.EmissiveFactor == glm::vec3(0.0f));
    CHECK(material.EmissiveStrength == 1.0f);
    CHECK(material.Ior == Approx(1.5f));
    CHECK(material.SpecularFactor == 1.0f);
    CHECK(material.SpecularColorFactor == glm::vec3(1.0f));
    CHECK(material.AlphaMode == GltfAlphaMode::Opaque);
    CHECK(material.AlphaCutoff == 0.5f);
    CHECK_FALSE(material.DoubleSided);
    for (const int index : indices) CHECK(index == -1);
    for (const auto& binding : material.Textures) CHECK_FALSE(binding);
}

TEST_CASE("Texture transform follows KHR_texture_transform", "[graphics][gltf][material]")
{
    GltfTextureTransform transform;
    transform.Rotation = std::numbers::pi_v<float> / 2.0f;
    const glm::vec2 turned = transform.Matrix() * glm::vec2(1.0f, 0.0f);
    CHECK(turned.x == Approx(0.0f).margin(1e-6));
    CHECK(turned.y == Approx(1.0f));

    transform = {.Offset = {0.5f, -0.25f}, .Rotation = 0.0f, .Scale = {2.0f, 4.0f}};
    const glm::vec2 uv = transform.Matrix() * glm::vec2(0.25f, 0.5f) + transform.Offset;
    CHECK(uv.x == Approx(1.0f));
    CHECK(uv.y == Approx(1.75f));

    transform = {.Offset = glm::vec2(0.0f), .Rotation = 0.7f, .Scale = {2.0f, 3.0f}};
    const glm::vec2 composed = transform.Matrix() * glm::vec2(0.3f, 0.4f);
    const glm::vec2 scaled(0.6f, 1.2f);
    const float c = std::cos(0.7f);
    const float s = std::sin(0.7f);
    CHECK(composed.x == Approx(c * scaled.x - s * scaled.y).epsilon(1e-5));
    CHECK(composed.y == Approx(s * scaled.x + c * scaled.y).epsilon(1e-5));
}

TEST_CASE("Materials pack into the std430 table with texture indices", "[graphics][gltf][material]")
{
    GltfMaterial material;
    material.BaseColorFactor = {0.1f, 0.2f, 0.3f, 0.4f};
    material.MetallicFactor = 0.25f;
    material.RoughnessFactor = 0.75f;
    material.EmissiveFactor = {1.0f, 0.5f, 0.0f};
    material.EmissiveStrength = 3.0f;
    material.Ior = 1.8f;
    material.SpecularFactor = 0.5f;
    material.SpecularColorFactor = {0.9f, 0.8f, 0.7f};
    material.AlphaMode = GltfAlphaMode::Blend;
    material.DoubleSided = true;
    material.NormalScale = 0.3f;
    material.OcclusionStrength = 0.6f;
    material.AlphaCutoff = 0.2f;
    const auto base = FakeImage();
    const auto normal = FakeImage();
    material.Texture(GltfTextureRole::BaseColor).Image = base;
    material.Texture(GltfTextureRole::Normal) = {normal, 1, {.Offset = {0.5f, 0.25f}, .Rotation = 0.0f, .Scale = {2.0f, 2.0f}}};

    std::map<const VulkanImage*, int> table{{base.get(), 4}, {normal.get(), 9}};
    const auto packed = PackGltfMaterial(material, [&](const std::shared_ptr<VulkanImage>& image) { return table[image.get()]; });

    CHECK(packed.BaseColorFactor == glm::vec4(0.1f, 0.2f, 0.3f, 0.4f));
    CHECK(packed.Metallic == 0.25f);
    CHECK(packed.Roughness == 0.75f);
    CHECK(packed.NormalScale == 0.3f);
    CHECK(packed.OcclusionStrength == 0.6f);
    CHECK(packed.AlphaCutoff == 0.2f);
    CHECK(packed.Ior == 1.8f);
    CHECK(packed.SpecularFactor == 0.5f);
    CHECK(packed.SpecularColorFactor == glm::vec3(0.9f, 0.8f, 0.7f));
    CHECK(packed.AlphaMode() == GltfAlphaMode::Blend);
    CHECK(packed.DoubleSided());

    const float scale = static_cast<float>(Radiometry::kHdrReferenceWhite / Radiometry::kLuminousEfficacyMax) * 3.0f;
    CHECK(packed.EmissiveRadiance.x == Approx(scale));
    CHECK(packed.EmissiveRadiance.y == Approx(scale * 0.5f));
    CHECK(packed.EmissiveRadiance.z == 0.0f);

    CHECK(packed.BaseColor.Index == 4);
    CHECK(packed.BaseColor.TexCoord == 0);
    CHECK(packed.Normal.Index == 9);
    CHECK(packed.Normal.TexCoord == 1);
    CHECK(packed.Normal.Offset == glm::vec2(0.5f, 0.25f));
    CHECK(packed.Normal.Matrix[0][0] == Approx(2.0f));
    CHECK(packed.Normal.Matrix[1][1] == Approx(2.0f));
    CHECK(packed.MetallicRoughness.Index == -1);
    CHECK(packed.Occlusion.Index == -1);
    CHECK(packed.Emissive.Index == -1);
    CHECK(packed.Specular.Index == -1);
    CHECK(packed.SpecularColor.Index == -1);
}

TEST_CASE("Material library shares textures, offsets scenes and reports overflow", "[graphics][gltf][material]")
{
    const auto a = FakeImage();
    const auto b = FakeImage();
    const auto c = FakeImage();

    GltfSceneData first;
    first.Materials.resize(2);
    first.Materials[0].Texture(GltfTextureRole::BaseColor).Image = a;
    first.Materials[1].Texture(GltfTextureRole::BaseColor).Image = b;
    first.Materials[1].Texture(GltfTextureRole::Emissive).Image = a;
    GltfSceneData second;
    second.Materials.resize(1);
    second.Materials[0].Texture(GltfTextureRole::BaseColor).Image = b;
    second.Materials[0].Texture(GltfTextureRole::Normal).Image = c;

    GltfMaterialLibrary library(2);
    CHECK(library.Add(first) == 0);
    CHECK(library.Add(second) == 2);
    CHECK(library.Add(first) == 0);
    CHECK(library.BaseMaterial(second) == 2);
    CHECK(library.Materials().size() == 3);
    CHECK(library.Images().size() == 2);
    CHECK(library.Materials()[1].BaseColor.Index == 1);
    CHECK(library.Materials()[1].Emissive.Index == 0);
    CHECK(library.Materials()[2].BaseColor.Index == 1);
    CHECK(library.Materials()[2].Normal.Index == -1);
    CHECK(library.Overflowed());

    GltfMaterialLibrary wide(kGltfIndexedTextureCapacity);
    wide.Add(first);
    wide.Add(second);
    CHECK_FALSE(wide.Overflowed());
    CHECK(wide.Images().size() == 3);
    CHECK(wide.Materials()[2].Normal.Index == 2);

    wide.Clear();
    CHECK(wide.Materials().empty());
    CHECK(wide.Images().empty());
}

TEST_CASE("Texture capacity follows descriptor indexing support", "[graphics][gltf][material]")
{
    GpuCapabilities without;
    GpuCapabilities with;
    with.DescriptorIndexing = true;
    CHECK(GltfTextureCapacity(without) == kGltfFallbackTextureCapacity);
    CHECK(GltfTextureCapacity(with) == kGltfIndexedTextureCapacity);
    CHECK(kGltfFallbackTextureCapacity == 32);
    CHECK(kGltfIndexedTextureCapacity > kGltfFallbackTextureCapacity);

    const auto fallbackDefines = PbrShaderDefines(without, 32);
    CHECK(fallbackDefines.at("PBR_TEXTURE_CAPACITY") == "32");
    CHECK_FALSE(fallbackDefines.contains("PBR_DESCRIPTOR_INDEXING"));
    const auto indexedDefines = PbrShaderDefines(with, 1024);
    CHECK(indexedDefines.at("PBR_TEXTURE_CAPACITY") == "1024");
    CHECK(indexedDefines.contains("PBR_DESCRIPTOR_INDEXING"));
}

TEST_CASE("Texture descriptor array is padded with the fallback image", "[graphics][gltf][material]")
{
    GltfSceneData scene;
    scene.Materials.resize(1);
    scene.Materials[0].Texture(GltfTextureRole::BaseColor).Image = FakeImage();
    GltfMaterialLibrary library(8);
    library.Add(scene);
    const VulkanImage fallback{};
    const auto infos = MakeTextureDescriptors(library, fallback);
    CHECK(infos.size() == 8);
    for (const auto& info : infos) CHECK(info.imageLayout == vk::ImageLayout::eShaderReadOnlyOptimal);
}

TEST_CASE("Extension audit separates supported, approximated and unsupported extensions", "[graphics][gltf][material]")
{
    const auto report = AuditGltfExtensions(
        {"KHR_materials_specular", "KHR_texture_transform", "KHR_materials_transmission", "KHR_materials_pbrSpecularGlossiness",
         "KHR_materials_clearcoat", "EXT_texture_webp"},
        {"KHR_mesh_quantization", "KHR_draco_mesh_compression", "KHR_materials_pbrSpecularGlossiness"});
    CHECK(report.Approximated == std::vector<std::string>{"KHR_materials_transmission", "KHR_materials_pbrSpecularGlossiness"});
    CHECK(report.Unsupported == std::vector<std::string>{"KHR_materials_clearcoat", "EXT_texture_webp"});
    CHECK(report.RequiredUnsupported == std::vector<std::string>{"KHR_draco_mesh_compression"});
    const auto none = AuditGltfExtensions({}, {});
    CHECK(none.Approximated.empty());
    CHECK(none.Unsupported.empty());
    CHECK(none.RequiredUnsupported.empty());
}

TEST_CASE("Spec-gloss materials map onto metallic-roughness", "[graphics][gltf][material]")
{
    tinygltf::Material source;
    source.extensions["KHR_materials_pbrSpecularGlossiness"] =
        Obj({{"diffuseFactor", Vec({0.2, 0.4, 0.6, 0.8})},
             {"diffuseTexture", Obj({{"index", tinygltf::Value(4)}, {"texCoord", tinygltf::Value(1)}})},
             {"glossinessFactor", tinygltf::Value(0.9)},
             {"specularFactor", Vec({0.0, 0.02, 0.5})}});
    GltfTextureIndices indices;
    const auto material = ReadGltfMaterial(source, indices);
    CHECK(material.BaseColorFactor == glm::vec4(0.2f, 0.4f, 0.6f, 0.8f));
    CHECK(material.MetallicFactor == 0.0f);
    CHECK(material.RoughnessFactor == Approx(0.1f));
    CHECK(material.SpecularColorFactor == glm::vec3(0.0f, 0.5f, 1.0f));
    CHECK(indices[static_cast<std::size_t>(GltfTextureRole::BaseColor)] == 4);
    CHECK(material.Texture(GltfTextureRole::BaseColor).TexCoord == 1);
}
