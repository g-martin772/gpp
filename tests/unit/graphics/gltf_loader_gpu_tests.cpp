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
    std::string Base64(const std::vector<std::uint8_t>& bytes)
    {
        constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        for (std::size_t i = 0; i < bytes.size(); i += 3)
        {
            const std::uint32_t a = bytes[i];
            const std::uint32_t b = i + 1 < bytes.size() ? bytes[i + 1] : 0;
            const std::uint32_t c = i + 2 < bytes.size() ? bytes[i + 2] : 0;
            const std::uint32_t triple = (a << 16) | (b << 8) | c;
            out += alphabet[(triple >> 18) & 63];
            out += alphabet[(triple >> 12) & 63];
            out += i + 1 < bytes.size() ? alphabet[(triple >> 6) & 63] : '=';
            out += i + 2 < bytes.size() ? alphabet[triple & 63] : '=';
        }
        return out;
    }

    std::string PpmDataUri(const int width, const int height, const std::uint8_t r, const std::uint8_t g, const std::uint8_t b)
    {
        std::vector<std::uint8_t> bytes;
        const auto header = std::format("P6\n{} {}\n255\n", width, height);
        bytes.assign(header.begin(), header.end());
        for (int i = 0; i < width * height; ++i)
        {
            bytes.push_back(r);
            bytes.push_back(g);
            bytes.push_back(b);
        }
        return "data:image/png;base64," + Base64(bytes);
    }

    template <typename T>
    void Append(std::vector<std::uint8_t>& out, const T& value)
    {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);
        out.insert(out.end(), bytes, bytes + sizeof(T));
    }

    std::filesystem::path WriteQuadGltf()
    {
        std::vector<std::uint8_t> buffer;
        const glm::vec3 positions[4] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
        const glm::vec3 normals[4] = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
        const glm::vec2 uvs[4] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
        const glm::vec4 tangents[4] = {{0, 1, 0, -1}, {0, 1, 0, -1}, {0, 1, 0, -1}, {0, 1, 0, -1}};
        const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
        for (const auto& v : positions) Append(buffer, v);
        for (const auto& v : normals) Append(buffer, v);
        for (const auto& v : uvs) Append(buffer, v);
        for (const auto& v : tangents) Append(buffer, v);
        for (const auto v : indices) Append(buffer, v);

        const auto json = std::format(R"({{
 "asset": {{"version": "2.0"}},
 "extensionsUsed": ["KHR_materials_emissive_strength", "KHR_texture_transform"],
 "scene": 0, "scenes": [{{"nodes": [0]}}],
 "nodes": [{{"mesh": 0}}],
 "meshes": [{{"primitives": [
   {{"attributes": {{"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}}, "indices": 4, "material": 0}},
   {{"attributes": {{"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2, "TANGENT": 3}}, "indices": 4, "material": 0}}]}}],
 "materials": [{{
   "name": "Test", "doubleSided": true,
   "pbrMetallicRoughness": {{"baseColorTexture": {{"index": 0}}, "metallicRoughnessTexture": {{"index": 1}}}},
   "normalTexture": {{"index": 2, "scale": 0.5}},
   "emissiveTexture": {{"index": 3, "extensions": {{"KHR_texture_transform": {{"scale": [2, 2]}}}}}},
   "emissiveFactor": [1, 1, 1],
   "extensions": {{"KHR_materials_emissive_strength": {{"emissiveStrength": 4}}}}}},
  {{"name": "Shared", "pbrMetallicRoughness": {{"baseColorTexture": {{"index": 4}}}}}}],
 "textures": [{{"source": 0, "sampler": 0}}, {{"source": 1}}, {{"source": 2}}, {{"source": 3}}, {{"source": 0, "sampler": 0}}],
 "samplers": [{{"magFilter": 9729, "minFilter": 9987, "wrapS": 33071, "wrapT": 33071}}],
 "images": [{{"uri": "{}"}}, {{"uri": "{}"}}, {{"uri": "{}"}}, {{"uri": "{}"}}],
 "buffers": [{{"byteLength": {}, "uri": "data:application/octet-stream;base64,{}"}}],
 "bufferViews": [
   {{"buffer": 0, "byteOffset": 0, "byteLength": 48}},
   {{"buffer": 0, "byteOffset": 48, "byteLength": 48}},
   {{"buffer": 0, "byteOffset": 96, "byteLength": 32}},
   {{"buffer": 0, "byteOffset": 128, "byteLength": 64}},
   {{"buffer": 0, "byteOffset": 192, "byteLength": 12}}],
 "accessors": [
   {{"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3", "min": [0,0,0], "max": [1,1,0]}},
   {{"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC3"}},
   {{"bufferView": 2, "componentType": 5126, "count": 4, "type": "VEC2"}},
   {{"bufferView": 3, "componentType": 5126, "count": 4, "type": "VEC4"}},
   {{"bufferView": 4, "componentType": 5123, "count": 6, "type": "SCALAR"}}]
}})",
                                      PpmDataUri(64, 32, 200, 100, 50), PpmDataUri(32, 32, 255, 128, 64),
                                      PpmDataUri(16, 16, 128, 128, 255), PpmDataUri(20, 12, 255, 255, 255),
                                      buffer.size(), Base64(buffer));
        const auto path = std::filesystem::temp_directory_path() / "gpp_quad_pbr_test.gltf";
        std::ofstream(path) << json;
        return path;
    }

    std::filesystem::path WriteDegradedGltf()
    {
        std::vector<std::uint8_t> buffer;
        const std::int16_t positions[4][3] = {{0, 0, 0}, {100, 0, 0}, {100, 100, 0}, {0, 100, 0}};
        const std::int8_t normals[4][3] = {{0, 0, 127}, {0, 0, 127}, {0, 0, 127}, {0, 0, 127}};
        const std::uint16_t uvs[4][2] = {{0, 65535}, {65535, 65535}, {65535, 0}, {0, 0}};
        for (const auto& v : positions) for (const auto c : v) Append(buffer, c);
        for (const auto& v : normals) for (const auto c : v) Append(buffer, c);
        for (const auto& v : uvs) for (const auto c : v) Append(buffer, c);

        std::string json = R"({
 "asset": {"version": "2.0"},
 "extensionsUsed": ["KHR_mesh_quantization", "KHR_materials_pbrSpecularGlossiness", "KHR_draco_mesh_compression", "KHR_materials_clearcoat"],
 "extensionsRequired": ["KHR_mesh_quantization", "KHR_draco_mesh_compression"],
 "scene": 0, "scenes": [{"nodes": [0]}],
 "nodes": [{"mesh": 0, "scale": [0.01, 0.01, 0.01]}],
 "meshes": [{"primitives": [
   {"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "mode": 5, "material": 0},
   {"attributes": {"POSITION": 3}, "material": 0},
   {"attributes": {"POSITION": 0}, "mode": 1, "material": 0}]}],
 "materials": [
  {"name": "SpecGloss", "doubleSided": true, "extensions": {"KHR_materials_pbrSpecularGlossiness": {
     "diffuseFactor": [0.5, 0.25, 1.0, 1.0], "diffuseTexture": {"index": 0}, "glossinessFactor": 0.25, "specularFactor": [0.04, 0.02, 0.0]}}},
  {"name": "Broken", "pbrMetallicRoughness": {"baseColorTexture": {"index": 1}}},
  {"name": "Missing", "pbrMetallicRoughness": {"baseColorTexture": {"index": 2}}}],
 "textures": [{"source": 0}, {"source": 1}, {"source": 2}],
 "images": [{"uri": "@GOOD@"}, {"uri": "data:image/png;base64,AAAAAAAA"}, {"uri": "does_not_exist.png"}],
 "buffers": [{"byteLength": @SIZE@, "uri": "data:application/octet-stream;base64,@DATA@"}],
 "bufferViews": [
   {"buffer": 0, "byteOffset": 0, "byteLength": 24},
   {"buffer": 0, "byteOffset": 24, "byteLength": 12},
   {"buffer": 0, "byteOffset": 36, "byteLength": 16}],
 "accessors": [
   {"bufferView": 0, "componentType": 5122, "count": 4, "type": "VEC3", "min": [0,0,0], "max": [100,100,0]},
   {"bufferView": 1, "componentType": 5120, "normalized": true, "count": 4, "type": "VEC3"},
   {"bufferView": 2, "componentType": 5123, "normalized": true, "count": 4, "type": "VEC2"},
   {"componentType": 5126, "count": 4, "type": "VEC3"}]
})";
        const auto replace = [&](const std::string& key, const std::string& value)
        {
            json.replace(json.find(key), key.size(), value);
        };
        replace("@GOOD@", PpmDataUri(8, 8, 255, 0, 0));
        replace("@SIZE@", std::to_string(buffer.size()));
        replace("@DATA@", Base64(buffer));
        const auto path = std::filesystem::temp_directory_path() / "gpp_degraded_gltf_test.gltf";
        std::ofstream(path) << json;
        return path;
    }

    std::vector<GltfVertex> ReadVertices(const std::shared_ptr<VulkanDevice>& device, const GltfPrimitive& primitive)
    {
        auto logger = std::make_shared<Logger>();
        const auto bytes = static_cast<vk::DeviceSize>(primitive.VertexCount * sizeof(GltfVertex));
        VulkanBuffer readback(device, MakeReadbackBufferSpecification(bytes), logger);
        VulkanCommandPool pool(device, logger, device->GetQueueIndices().Graphics);
        ImmediateSubmit(pool, device->GetGraphicsQueue(), [&](const vk::CommandBuffer cmd)
        {
            CopyBuffer(cmd, primitive.VertexBuffer.GetBuffer(), readback.GetBuffer(), bytes);
        });
        std::vector<GltfVertex> out(primitive.VertexCount);
        readback.Read(out.data(), bytes);
        return out;
    }

    std::vector<GltfVertexExtra> ReadExtras(const std::shared_ptr<VulkanDevice>& device, const GltfPrimitive& primitive)
    {
        auto logger = std::make_shared<Logger>();
        const auto bytes = static_cast<vk::DeviceSize>(primitive.VertexCount * sizeof(GltfVertexExtra));
        VulkanBuffer readback(device, MakeReadbackBufferSpecification(bytes), logger);
        VulkanCommandPool pool(device, logger, device->GetQueueIndices().Graphics);
        ImmediateSubmit(pool, device->GetGraphicsQueue(), [&](const vk::CommandBuffer cmd)
        {
            CopyBuffer(cmd, primitive.ExtraBuffer.GetBuffer(), readback.GetBuffer(), bytes);
        });
        std::vector<GltfVertexExtra> out(primitive.VertexCount);
        readback.Read(out.data(), bytes);
        return out;
    }
}

TEST_CASE("Loader uploads role formats, mip chains, tangents and the full material set", "[graphics][gltf][gpu]")
{
    const auto logger = std::make_shared<Logger>();
    const auto device = CreateHeadlessDevice(logger);
    if (!device)
    {
        WARN("No headless Vulkan device; skipping loader GPU test");
        return;
    }

    const auto path = WriteQuadGltf();
    const auto scene = LoadGltfScene(device, std::make_shared<FileSystem>(), path, logger);
    std::filesystem::remove(path);

    REQUIRE(scene.Materials.size() == 2);
    const auto& material = scene.Materials[0];
    CHECK(material.DoubleSided);
    CHECK(material.NormalScale == Approx(0.5f));
    CHECK(material.EmissiveStrength == Approx(4.0f));
    CHECK(material.Texture(GltfTextureRole::Emissive).Transform.Scale == glm::vec2(2.0f));

    const auto spec = [&](const GltfTextureRole role) -> const VulkanImageSpecification&
    {
        const auto& binding = material.Texture(role);
        REQUIRE(binding);
        return binding.Image->GetSpecification();
    };
    CHECK(spec(GltfTextureRole::BaseColor).format == vk::Format::eR8G8B8A8Srgb);
    CHECK(spec(GltfTextureRole::MetallicRoughness).format == vk::Format::eR8G8B8A8Unorm);
    CHECK(spec(GltfTextureRole::Normal).format == vk::Format::eR8G8B8A8Unorm);
    CHECK(spec(GltfTextureRole::Emissive).format == vk::Format::eR8G8B8A8Srgb);
    CHECK_FALSE(material.Texture(GltfTextureRole::Occlusion));
    CHECK(spec(GltfTextureRole::BaseColor).mipLevels == 7);
    CHECK(spec(GltfTextureRole::MetallicRoughness).mipLevels == 6);
    CHECK(spec(GltfTextureRole::Normal).mipLevels == 5);
    CHECK(spec(GltfTextureRole::Emissive).mipLevels == 5);
    CHECK(spec(GltfTextureRole::BaseColor).samplerAddressMode == vk::SamplerAddressMode::eClampToEdge);
    CHECK(material.BaseColorTexture == material.Texture(GltfTextureRole::BaseColor).Image);

    // same source image and sampler, same role: shared; the second material reuses it
    CHECK(scene.Materials[1].BaseColorTexture == material.BaseColorTexture);
    CHECK(scene.Textures.size() == 4);

    REQUIRE(scene.Meshes.size() == 1);
    REQUIRE(scene.Meshes[0].Primitives.size() == 2);
    const auto& generated = scene.Meshes[0].Primitives[0];
    const auto& authored = scene.Meshes[0].Primitives[1];
    CHECK_FALSE(generated.HasAuthoredTangents);
    CHECK(authored.HasAuthoredTangents);

    for (const auto& extra : ReadExtras(device, generated))
    {
        CHECK(extra.Tangent.x == Approx(1.0f));
        CHECK(extra.Tangent.y == Approx(0.0f).margin(1e-6));
        CHECK(extra.Tangent.w == 1.0f);
    }
    for (const auto& extra : ReadExtras(device, authored))
    {
        CHECK(extra.Tangent.x == Approx(0.0f).margin(1e-6));
        CHECK(extra.Tangent.y == Approx(1.0f));
        CHECK(extra.Tangent.w == -1.0f);
    }

    GltfMaterialLibrary library(GltfTextureCapacity(device->GetCapabilities()));
    library.Add(scene);
    CHECK(library.Images().size() == 4);
    CHECK(library.Materials()[0].Normal.Index >= 0);
    CHECK(library.Materials()[0].Emissive.Matrix[0][0] == Approx(2.0f));
    auto buffer = CreateMaterialBuffer(device, library, logger);
    CHECK(buffer.GetSize() == 2 * sizeof(GltfMaterialGpu));
}

TEST_CASE("Loader degrades on quantized data, strips, unsupported primitives, broken images and spec-gloss", "[graphics][gltf][gpu]")
{
    const auto logger = std::make_shared<Logger>();
    const auto device = CreateHeadlessDevice(logger);
    if (!device)
    {
        WARN("No headless Vulkan device; skipping loader GPU test");
        return;
    }

    const auto path = WriteDegradedGltf();
    const auto scene = LoadGltfScene(device, std::make_shared<FileSystem>(), path, logger);
    std::filesystem::remove(path);

    REQUIRE(scene.Meshes.size() == 1);
    REQUIRE(scene.Meshes[0].Primitives.size() == 1);
    const auto& strip = scene.Meshes[0].Primitives[0];
    CHECK(strip.VertexCount == 4);
    CHECK(strip.IndexCount == 6);

    const auto vertices = ReadVertices(device, strip);
    CHECK(vertices[1].Position.x == Approx(1.0f));
    CHECK(vertices[2].Position.y == Approx(1.0f));
    CHECK(vertices[0].Normal.z == Approx(1.0f));
    CHECK(vertices[1].TexCoord == glm::vec2(1.0f, 1.0f));
    CHECK(vertices[3].TexCoord == glm::vec2(0.0f, 0.0f));

    REQUIRE(scene.Materials.size() == 3);
    const auto& specGloss = scene.Materials[0];
    CHECK(specGloss.BaseColorFactor == glm::vec4(0.5f, 0.25f, 1.0f, 1.0f));
    CHECK(specGloss.MetallicFactor == 0.0f);
    CHECK(specGloss.RoughnessFactor == Approx(0.75f));
    CHECK(specGloss.SpecularColorFactor.x == Approx(1.0f));
    CHECK(specGloss.SpecularColorFactor.y == Approx(0.5f));
    CHECK(specGloss.SpecularColorFactor.z == Approx(0.0f));
    CHECK(specGloss.Texture(GltfTextureRole::BaseColor));
    CHECK_FALSE(scene.Materials[1].Texture(GltfTextureRole::BaseColor));
    CHECK_FALSE(scene.Materials[2].Texture(GltfTextureRole::BaseColor));
}

TEST_CASE("Every sample model loads with its materials and textures", "[graphics][gltf][gpu][models]")
{
    const auto logger = std::make_shared<Logger>();
    const auto device = CreateHeadlessDevice(logger);
    if (!device)
    {
        WARN("No headless Vulkan device; skipping model test");
        return;
    }
    const auto root = std::filesystem::path(GPP_SOURCE_DIR) / ".." / "MoleHole" / "assets" / "models";
    if (!std::filesystem::exists(root))
    {
        WARN("No sample models next to gpp; skipping");
        return;
    }
    std::size_t loaded = 0;
    for (const auto& entry : std::filesystem::directory_iterator(root))
    {
        const auto path = entry.path() / "scene.gltf";
        if (!std::filesystem::exists(path)) continue;
        INFO(path.string());
        tinygltf::Model model;
        std::string err, warn;
        tinygltf::TinyGLTF parser;
        parser.SetImageLoader([](tinygltf::Image*, int, std::string*, std::string*, int, int, const unsigned char*, int, void*)
                              { return true; }, nullptr);
        REQUIRE(parser.LoadASCIIFromFile(&model, &err, &warn, path.string()));
        const auto scene = LoadGltfScene(device, std::make_shared<FileSystem>(), path, logger);
        CHECK(scene.Materials.size() == model.materials.size());
        CHECK_FALSE(scene.Meshes.empty());
        CHECK_FALSE(scene.Geometry.Indices.empty());
        std::size_t sourceTextures = 0;
        for (const auto& material : model.materials)
        {
            const auto& pbr = material.pbrMetallicRoughness;
            const auto specGloss = material.extensions.find("KHR_materials_pbrSpecularGlossiness");
            const bool diffuse = specGloss != material.extensions.end() && specGloss->second.Has("diffuseTexture");
            if (pbr.baseColorTexture.index >= 0 || diffuse) ++sourceTextures;
        }
        std::size_t textured = 0;
        for (const auto& material : scene.Materials) textured += material.Texture(GltfTextureRole::BaseColor) ? 1 : 0;
        CHECK(textured == sourceTextures);
        ++loaded;
    }
    if (loaded == 0) WARN("No sample models found");
}

TEST_CASE("Report the sample ship's materials", "[.sample][gltf][gpu]")
{
    const auto logger = std::make_shared<Logger>();
    const auto device = CreateHeadlessDevice(logger);
    REQUIRE(device);
    const char* env = std::getenv("GPP_SAMPLE_GLTF");
    const std::filesystem::path path = env ? std::filesystem::path(env)
                                           : std::filesystem::path(GPP_SOURCE_DIR) / ".." / "MoleHole" / "assets" / "models" /
                                                 "star_trek_online__u.s.s._enterprise_kelvin" / "scene.gltf";
    REQUIRE(std::filesystem::exists(path));
    const auto scene = LoadGltfScene(device, std::make_shared<FileSystem>(), path, logger);

    static constexpr std::array<std::string_view, kGltfTextureRoleCount> roles{
        "baseColor", "metalRough", "normal", "occlusion", "emissive", "specular", "specColor"};
    std::println("materials={} unique textures={}", scene.Materials.size(), scene.Textures.size());
    for (std::size_t i = 0; i < scene.Materials.size(); ++i)
    {
        const auto& m = scene.Materials[i];
        std::println("material {} '{}': alpha={} doubleSided={} metallic={} roughness={} emissive=({},{},{})x{} ior={} specular={}",
                     i, m.Name, static_cast<int>(m.AlphaMode), m.DoubleSided, m.MetallicFactor, m.RoughnessFactor,
                     m.EmissiveFactor.x, m.EmissiveFactor.y, m.EmissiveFactor.z, m.EmissiveStrength, m.Ior, m.SpecularFactor);
        for (std::size_t r = 0; r < kGltfTextureRoleCount; ++r)
        {
            const auto& binding = m.Textures[r];
            if (!binding) continue;
            const auto& s = binding.Image->GetSpecification();
            std::println("    {:10} {}x{} mips={} format={} texCoord={}", roles[r], s.extent.width, s.extent.height, s.mipLevels,
                         vk::to_string(s.format), binding.TexCoord);
        }
    }
    std::size_t primitives = 0, authored = 0;
    std::uint64_t vertices = 0;
    for (const auto& mesh : scene.Meshes)
    {
        for (const auto& p : mesh.Primitives)
        {
            ++primitives;
            authored += p.HasAuthoredTangents ? 1 : 0;
            vertices += p.VertexCount;
        }
    }
    std::println("primitives={} (authored tangents {}, generated {}) vertices={}", primitives, authored, primitives - authored, vertices);

    GltfMaterialLibrary library(GltfTextureCapacity(device->GetCapabilities()));
    library.Add(scene);
    std::println("library: {} materials, {} textures, capacity {}, overflow={}", library.Materials().size(),
                 library.Images().size(), library.TextureCapacity(), library.Overflowed());
    SUCCEED();
}
