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
    const std::filesystem::path kRoot(GPP_SOURCE_DIR);

    ShaderCompileOptions Options()
    {
        ShaderCompileOptions options;
        options.enableCache = false;
        options.includeDirectories.push_back(kRoot / "shaders");
        return options;
    }

    CompiledShader Compile(const std::filesystem::path& file, const ShaderStage stage,
                           std::unordered_map<std::string, std::string> defines = {})
    {
        auto fileSystem = std::make_shared<FileSystem>();
        auto logger = std::make_shared<Logger>();
        ShaderCompiler compiler(fileSystem, logger);
        return compiler.Compile(ShaderSource{.path = kRoot / "tests/fixtures/pbr" / file, .stage = stage,
                                             .defines = std::move(defines)},
                                Options());
    }

    std::shared_ptr<VulkanDevice> Device()
    {
        return CreateHeadlessDevice(std::make_shared<Logger>());
    }

    struct ProbeCase
    {
        glm::vec4 BaseMetallic;
        glm::vec4 RoughIorSpecular;
        glm::vec4 SpecularColor;
        glm::vec4 Normal;
        glm::vec4 View;
        glm::vec4 Light;
        glm::vec4 Random;
    };
    static_assert(sizeof(ProbeCase) == 112);

    void RunCompute(const std::shared_ptr<VulkanDevice>& device, const CompiledShader& shader,
                    const std::vector<std::pair<const void*, std::size_t>>& inputs, void* output, const std::size_t outputBytes,
                    const void* pushConstants, const std::size_t pushBytes, const std::uint32_t groups)
    {
        auto logger = std::make_shared<Logger>();
        VulkanPipeline pipeline(device, shader);
        std::vector<VulkanBuffer> buffers;
        for (const auto& [data, bytes] : inputs)
        {
            buffers.emplace_back(device, MakeStorageBufferSpecification(bytes, true), logger);
            buffers.back().Upload(data, bytes);
        }
        buffers.emplace_back(device, MakeStorageBufferSpecification(outputBytes, true), logger);

        VulkanDescriptorAllocator allocator(device);
        const auto set = allocator.Allocate(pipeline.GetDescriptorSetLayouts()[0]);
        DescriptorSetWriter writer(device->GetDevice());
        for (std::uint32_t i = 0; i < buffers.size(); ++i)
        {
            const bool declared = std::ranges::any_of(shader.reflection.descriptorBindings,
                                                      [&](const ShaderDescriptorBinding& b) { return b.set == 0 && b.binding == i; });
            if (declared) writer.WriteStorageBuffer(set, i, buffers[i].GetBuffer());
        }
        writer.Update();

        VulkanCommandPool pool(device, logger, device->GetQueueIndices().Graphics);
        ImmediateSubmit(pool, device->GetGraphicsQueue(), [&](const vk::CommandBuffer cmd)
        {
            cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline.GetPipeline());
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeline.GetLayout(), 0, set, {});
            if (pushBytes > 0)
            {
                cmd.pushConstants(pipeline.GetLayout(), vk::ShaderStageFlagBits::eCompute, 0,
                                  static_cast<std::uint32_t>(pushBytes), pushConstants);
            }
            cmd.dispatch(groups, 1, 1);
            vk::MemoryBarrier2 barrier{vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderWrite,
                                       vk::PipelineStageFlagBits2::eHost, vk::AccessFlagBits2::eHostRead};
            vk::DependencyInfo dependency{};
            dependency.memoryBarrierCount = 1;
            dependency.pMemoryBarriers = &barrier;
            cmd.pipelineBarrier2(dependency);
        });
        buffers.back().Read(output, outputBytes);
    }

    glm::dvec3 ToD(const glm::vec4& v) { return {v.x, v.y, v.z}; }
}

TEST_CASE("PBR GLSL compiles in every configuration", "[pbr][shader]")
{
    const std::unordered_map<std::string, std::string> table{{"USE_TABLE", "1"}, {"DFG_WIDTH", "64"}, {"DFG_HEIGHT", "32"}};
    CHECK_FALSE(Compile("brdf_probe.comp", ShaderStage::Compute, table).spirv.empty());
    CHECK_FALSE(Compile("brdf_probe.comp", ShaderStage::Compute).spirv.empty());
    CHECK_FALSE(Compile("material_layout_probe.comp", ShaderStage::Compute).spirv.empty());

    const auto fragment = Compile("material_compile_probe.frag", ShaderStage::Fragment);
    REQUIRE_FALSE(fragment.spirv.empty());
    const auto textures = std::ranges::find_if(fragment.reflection.descriptorBindings,
                                               [](const ShaderDescriptorBinding& b) { return b.binding == 1; });
    REQUIRE(textures != fragment.reflection.descriptorBindings.end());
    CHECK(textures->descriptorCount == 64);
}

TEST_CASE("GLSL BSDF agrees with the CPU reference on the GPU", "[pbr][gpu]")
{
    const auto device = Device();
    if (!device)
    {
        WARN("No headless Vulkan device; skipping GPU BSDF comparison");
        return;
    }

    const auto table = Pbr::BuildDfgTable();
    std::vector<glm::vec4> texels;
    for (const auto& t : table.Texels()) texels.emplace_back(t);

    std::uint32_t state = 42u;
    const auto next = [&]
    {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) / static_cast<float>(1u << 24);
    };
    const auto direction = [&](const glm::vec3& n)
    {
        const float cosTheta = 0.05f + 0.95f * next();
        const float phi = 6.2831853f * next();
        const float sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);
        glm::dvec3 t, b;
        Pbr::Basis(glm::dvec3(n), t, b);
        return glm::vec3(t * static_cast<double>(sinTheta * std::cos(phi)) + b * static_cast<double>(sinTheta * std::sin(phi)) +
                         glm::dvec3(n) * static_cast<double>(cosTheta));
    };

    std::vector<ProbeCase> cases;
    for (int i = 0; i < 512; ++i)
    {
        ProbeCase c{};
        const glm::vec3 n = glm::normalize(glm::vec3(next() - 0.5f, next() - 0.5f, next() + 0.2f));
        c.BaseMetallic = {0.1f + 0.9f * next(), 0.1f + 0.9f * next(), 0.1f + 0.9f * next(), i % 3 == 0 ? 1.0f : (i % 3 == 1 ? 0.0f : next())};
        c.RoughIorSpecular = {0.1f + 0.9f * next(), 1.2f + 1.0f * next(), 0.5f + 0.5f * next(), 0.0f};
        c.SpecularColor = {0.6f + 0.4f * next(), 0.6f + 0.4f * next(), 0.6f + 0.4f * next(), 0.0f};
        c.Normal = glm::vec4(n, 0.0f);
        c.View = glm::vec4(direction(n), 0.0f);
        c.Light = glm::vec4(direction(n), 0.0f);
        c.Random = {next(), next(), next(), 0.0f};
        cases.push_back(c);
    }

    const std::vector<std::pair<const void*, std::size_t>> inputs{
        {cases.data(), cases.size() * sizeof(ProbeCase)}, {texels.data(), texels.size() * sizeof(glm::vec4)}};
    std::vector<glm::vec4> results(cases.size() * 5);
    const std::int32_t count = static_cast<std::int32_t>(cases.size());
    RunCompute(device,
               Compile("brdf_probe.comp", ShaderStage::Compute,
                       {{"USE_TABLE", "1"}, {"DFG_WIDTH", std::to_string(table.Width())},
                        {"DFG_HEIGHT", std::to_string(table.Height())}}),
               inputs, results.data(), results.size() * sizeof(glm::vec4), &count, sizeof(count),
               static_cast<std::uint32_t>((cases.size() + 63) / 64));

    int compared = 0;
    for (std::size_t i = 0; i < cases.size(); ++i)
    {
        const auto& c = cases[i];
        const Pbr::Surface surface{.BaseColor = ToD(c.BaseMetallic), .Metallic = c.BaseMetallic.w,
                                   .Roughness = c.RoughIorSpecular.x, .Ior = c.RoughIorSpecular.y,
                                   .SpecularColor = ToD(c.SpecularColor), .SpecularFactor = c.RoughIorSpecular.z};
        const glm::dvec3 n = glm::normalize(ToD(c.Normal));
        const glm::dvec3 v = glm::normalize(ToD(c.View));
        const glm::dvec3 l = glm::normalize(ToD(c.Light));
        const auto lobes = Pbr::MakeLobes(surface, n, v, table);

        const auto f = Pbr::Eval(lobes, n, v, l, table);
        const double pdf = Pbr::Pdf(lobes, n, v, l);
        const auto sample = Pbr::SampleBsdf(lobes, n, v, ToD(c.Random), table);

        const auto& gf = results[i * 5 + 0];
        INFO("case " << i);
        for (int k = 0; k < 3; ++k) CHECK(static_cast<double>(gf[k]) == Approx(f[k]).epsilon(5e-3).margin(2e-4));
        CHECK(static_cast<double>(gf.w) == Approx(pdf).epsilon(5e-3).margin(2e-4));

        const auto& gs = results[i * 5 + 1];
        const auto& gw = results[i * 5 + 2];
        CHECK((gw.w > 0.5f) == (sample.Pdf > 0.0));
        if (sample.Pdf > 0.0 && gw.w > 0.5f)
        {
            for (int k = 0; k < 3; ++k) CHECK(static_cast<double>(gs[k]) == Approx(sample.L[k]).margin(2e-3));
            CHECK(static_cast<double>(gs.w) == Approx(sample.Pdf).epsilon(5e-3).margin(2e-4));
            for (int k = 0; k < 3; ++k) CHECK(static_cast<double>(gw[k]) == Approx(sample.Weight[k]).epsilon(1e-2).margin(1e-3));
            ++compared;
        }
        CHECK(static_cast<double>(results[i * 5 + 3].x) == Approx(lobes.EWhiteView).margin(2e-3));
        CHECK(static_cast<double>(results[i * 5 + 3].z) == Approx(lobes.SpecularProbability).margin(2e-3));
    }
    CHECK(compared > 300);
}

TEST_CASE("Built-in GLSL DFG table is current and close to the fine table", "[pbr][shader]")
{
    const auto builtin = Pbr::BuildDfgTable(Pbr::kBuiltinDfgWidth, Pbr::kBuiltinDfgHeight);
    const auto path = kRoot / "shaders/pbr/dfg_table.glsl";
    if (std::getenv("GPP_REGENERATE_SHADER_TABLES"))
    {
        std::ofstream(path) << Pbr::FormatDfgTableGlsl(builtin);
    }

    std::ifstream stream(path);
    REQUIRE(stream.good());
    const std::string text{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    std::vector<double> numbers;
    for (std::size_t pos = text.find("vec4("); pos != std::string::npos; pos = text.find("vec4(", pos + 1))
    {
        std::size_t cursor = pos + 5;
        for (int k = 0; k < 4; ++k)
        {
            const char* begin = text.data() + cursor;
            double value = 0.0;
            const auto result = std::from_chars(begin, text.data() + text.size(), value);
            if (result.ec != std::errc{}) break;
            numbers.push_back(value);
            cursor = static_cast<std::size_t>(result.ptr - text.data());
            while (cursor < text.size() && (text[cursor] == ',' || text[cursor] == ' ')) ++cursor;
        }
    }
    REQUIRE(numbers.size() == builtin.Texels().size() * 4);
    for (std::size_t i = 0; i < builtin.Texels().size(); ++i)
    {
        const auto& t = builtin.Texels()[i];
        CHECK(numbers[i * 4 + 0] == Approx(t.x).margin(2e-5));
        CHECK(numbers[i * 4 + 1] == Approx(t.y).margin(2e-5));
        CHECK(numbers[i * 4 + 2] == Approx(t.z).margin(2e-5));
        CHECK(numbers[i * 4 + 3] == Approx(t.w).margin(2e-5));
    }

    const auto fine = Pbr::BuildDfgTable();
    double worst = 0.0;
    for (double roughness = 0.05; roughness <= 1.0; roughness += 0.05)
    {
        for (double nov = 0.02; nov <= 1.0; nov += 0.02)
        {
            const auto a = builtin.Lookup(nov, roughness);
            const auto b = fine.Lookup(nov, roughness);
            worst = std::max({worst, std::abs(a.x + a.y - b.x - b.y), std::abs(a.z + a.w - b.z - b.w)});
        }
    }
    INFO("worst built-in table deviation " << worst);
    CHECK(worst < 0.05);

    for (const double roughness : {0.15, 0.4, 0.8, 1.0})
    {
        for (const double nov : {0.1, 0.5, 1.0})
        {
            for (const double metallic : {0.0, 1.0})
            {
                const Pbr::Surface surface{.BaseColor = glm::dvec3(1.0), .Metallic = metallic, .Roughness = roughness};
                const auto albedo = Pbr::DirectionalAlbedo(surface, nov, builtin, 1u << 14);
                INFO("roughness=" << roughness << " nov=" << nov << " metallic=" << metallic);
                CHECK(albedo.x == Approx(1.0).epsilon(0.025));
            }
        }
    }
}

TEST_CASE("GLSL material struct matches GltfMaterialGpu byte for byte", "[pbr][gpu]")
{
    const auto device = Device();
    if (!device)
    {
        WARN("No headless Vulkan device; skipping material layout check");
        return;
    }

    std::vector<GltfMaterialGpu> materials(2);
    auto& m = materials[1];
    m.BaseColorFactor = {0.1f, 0.2f, 0.3f, 0.4f};
    m.EmissiveRadiance = {1.0f, 2.0f, 3.0f};
    m.NormalScale = 0.5f;
    m.Metallic = 0.6f;
    m.Roughness = 0.7f;
    m.OcclusionStrength = 0.8f;
    m.AlphaCutoff = 0.9f;
    m.Ior = 1.7f;
    m.SpecularFactor = 0.25f;
    m.Flags = kMaterialFlagDoubleSided | (2u << kMaterialAlphaShift);
    m.SpecularColorFactor = {0.3f, 0.4f, 0.5f};
    const auto slot = [](const int index)
    {
        GltfTextureSlotGpu s;
        s.Index = index;
        s.TexCoord = static_cast<std::uint32_t>(index % 2);
        s.Offset = {0.01f * index, 0.02f * index};
        return s;
    };
    m.BaseColor = slot(1);
    m.MetallicRoughness = slot(2);
    m.Normal = slot(3);
    m.Normal.Matrix = glm::mat2(glm::vec2(2.0f, 3.0f), glm::vec2(4.0f, 5.0f));
    m.Normal.TexCoord = 0;
    m.Normal.Offset = {0.5f, 0.25f};
    m.Occlusion = slot(4);
    m.Emissive = slot(5);
    m.Emissive.TexCoord = 1;
    m.Emissive.Offset = {0.1f, 0.2f};
    m.Specular = slot(6);
    m.SpecularColor = slot(7);

    std::vector<glm::vec4> results(14);
    RunCompute(device, Compile("material_layout_probe.comp", ShaderStage::Compute),
               {{materials.data(), materials.size() * sizeof(GltfMaterialGpu)}}, results.data(),
               results.size() * sizeof(glm::vec4), nullptr, 0, 1);

    CHECK(results[0] == m.BaseColorFactor);
    CHECK(results[1] == glm::vec4(m.EmissiveRadiance, m.NormalScale));
    CHECK(results[2] == glm::vec4(0.6f, 0.7f, 0.8f, 0.9f));
    CHECK(results[3] == glm::vec4(1.7f, 0.25f, 2.0f, 1.0f));
    CHECK(results[4] == glm::vec4(0.3f, 0.4f, 0.5f, 0.0f));
    const GltfTextureSlotGpu* slots[7] = {&m.BaseColor, &m.MetallicRoughness, &m.Normal, &m.Occlusion,
                                          &m.Emissive, &m.Specular, &m.SpecularColor};
    for (int i = 0; i < 7; ++i)
    {
        CHECK(results[5 + i] == glm::vec4(static_cast<float>(slots[i]->Index), static_cast<float>(slots[i]->TexCoord),
                                          slots[i]->Offset.x, slots[i]->Offset.y));
    }
    CHECK(results[12] == glm::vec4(2.0f, 3.0f, 4.0f, 5.0f));
    const glm::vec2 expectedNormalUv = m.Normal.Matrix * glm::vec2(1.0f, 0.0f) + m.Normal.Offset;
    CHECK(results[13].x == Approx(expectedNormalUv.x));
    CHECK(results[13].y == Approx(expectedNormalUv.y));
    CHECK(results[13].z == Approx(0.25f + 0.1f));
    CHECK(results[13].w == Approx(0.75f + 0.2f));
}
