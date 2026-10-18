module;
#include <cstddef>
export module GPP.Graphics:Assets.Material;

import std;
import glm;
import vulkan;
import GPP.Core;
import :Vulkan;
import :Assets.Gltf;

export namespace GPP
{
    // Bounded texture array used without descriptor indexing (matches the traced mesh path's 32 slots).
    inline constexpr std::uint32_t kGltfFallbackTextureCapacity = 32;
    // Texture array size when GpuCapabilities::DescriptorIndexing is set (non-uniform indexing in ray queries).
    inline constexpr std::uint32_t kGltfIndexedTextureCapacity = 1024;

    [[nodiscard]] constexpr std::uint32_t GltfTextureCapacity(const GpuCapabilities& capabilities) noexcept
    {
        return capabilities.DescriptorIndexing ? kGltfIndexedTextureCapacity : kGltfFallbackTextureCapacity;
    }

    inline constexpr std::uint32_t kMaterialFlagDoubleSided = 1u << 0;
    inline constexpr std::uint32_t kMaterialAlphaShift = 1;
    inline constexpr std::uint32_t kMaterialAlphaMask = 3u << kMaterialAlphaShift;

    // std430, 32 bytes. Index < 0: no texture. uv' = Matrix * uv(TexCoord) + Offset.
    struct GltfTextureSlotGpu
    {
        std::int32_t Index{-1};
        std::uint32_t TexCoord{0};
        glm::vec2 Offset{0.0f};
        glm::mat2 Matrix{1.0f};
    };
    static_assert(sizeof(GltfTextureSlotGpu) == 32);

    // std430, 304 bytes; mirrored by GltfMaterial in pbr/material.glsl.
    // EmissiveRadiance = emissiveFactor * KHR_materials_emissive_strength * reference white / luminous efficacy
    // [W m^-2 sr^-1 for a texel value of 1].
    struct GltfMaterialGpu
    {
        glm::vec4 BaseColorFactor{1.0f};
        glm::vec3 EmissiveRadiance{0.0f};
        float NormalScale{1.0f};
        float Metallic{1.0f};
        float Roughness{1.0f};
        float OcclusionStrength{1.0f};
        float AlphaCutoff{0.5f};
        float Ior{1.5f};
        float SpecularFactor{1.0f};
        std::uint32_t Flags{0};
        float Pad0{0.0f};
        glm::vec3 SpecularColorFactor{1.0f};
        float Pad1{0.0f};
        GltfTextureSlotGpu BaseColor;
        GltfTextureSlotGpu MetallicRoughness;
        GltfTextureSlotGpu Normal;
        GltfTextureSlotGpu Occlusion;
        GltfTextureSlotGpu Emissive;
        GltfTextureSlotGpu Specular;
        GltfTextureSlotGpu SpecularColor;

        [[nodiscard]] GltfAlphaMode AlphaMode() const noexcept
        {
            return static_cast<GltfAlphaMode>((Flags & kMaterialAlphaMask) >> kMaterialAlphaShift);
        }

        [[nodiscard]] bool DoubleSided() const noexcept { return (Flags & kMaterialFlagDoubleSided) != 0; }
    };
    static_assert(sizeof(GltfMaterialGpu) == 304);
    static_assert(offsetof(GltfMaterialGpu, EmissiveRadiance) == 16);
    static_assert(offsetof(GltfMaterialGpu, Metallic) == 32);
    static_assert(offsetof(GltfMaterialGpu, SpecularColorFactor) == 64);
    static_assert(offsetof(GltfMaterialGpu, BaseColor) == 80);

    struct GltfPackOptions
    {
        double EmissiveReferenceWhite = Radiometry::kHdrReferenceWhite; // cd/m^2 of an emissive value of 1
        double LuminousEfficacy = Radiometry::kLuminousEfficacyMax;     // lm/W
    };

    using GltfTextureIndexFn = std::function<std::int32_t(const std::shared_ptr<VulkanImage>&)>;

    [[nodiscard]] inline GltfTextureSlotGpu PackTextureSlot(const GltfTextureBinding& binding,
                                                            const GltfTextureIndexFn& textureIndex)
    {
        GltfTextureSlotGpu slot;
        if (!binding || !textureIndex) return slot;
        slot.Index = textureIndex(binding.Image);
        if (slot.Index < 0) return slot;
        slot.TexCoord = static_cast<std::uint32_t>(std::clamp(binding.TexCoord, 0, 1));
        slot.Offset = binding.Transform.Offset;
        slot.Matrix = binding.Transform.Matrix();
        return slot;
    }

    [[nodiscard]] inline GltfMaterialGpu PackGltfMaterial(const GltfMaterial& material,
                                                          const GltfTextureIndexFn& textureIndex,
                                                          const GltfPackOptions& options = {})
    {
        GltfMaterialGpu out;
        out.BaseColorFactor = material.BaseColorFactor;
        out.EmissiveRadiance = material.EmissiveFactor * material.EmissiveStrength
                               * static_cast<float>(options.EmissiveReferenceWhite / options.LuminousEfficacy);
        out.NormalScale = material.NormalScale;
        out.Metallic = material.MetallicFactor;
        out.Roughness = material.RoughnessFactor;
        out.OcclusionStrength = material.OcclusionStrength;
        out.AlphaCutoff = material.AlphaCutoff;
        out.Ior = material.Ior;
        out.SpecularFactor = material.SpecularFactor;
        out.SpecularColorFactor = material.SpecularColorFactor;
        out.Flags = (material.DoubleSided ? kMaterialFlagDoubleSided : 0u)
                    | (static_cast<std::uint32_t>(material.AlphaMode) << kMaterialAlphaShift);
        out.BaseColor = PackTextureSlot(material.Texture(GltfTextureRole::BaseColor), textureIndex);
        out.MetallicRoughness = PackTextureSlot(material.Texture(GltfTextureRole::MetallicRoughness), textureIndex);
        out.Normal = PackTextureSlot(material.Texture(GltfTextureRole::Normal), textureIndex);
        out.Occlusion = PackTextureSlot(material.Texture(GltfTextureRole::Occlusion), textureIndex);
        out.Emissive = PackTextureSlot(material.Texture(GltfTextureRole::Emissive), textureIndex);
        out.Specular = PackTextureSlot(material.Texture(GltfTextureRole::Specular), textureIndex);
        out.SpecularColor = PackTextureSlot(material.Texture(GltfTextureRole::SpecularColor), textureIndex);
        return out;
    }

    // One material table and one texture table for any number of loaded scenes: the material index of a draw is
    // BaseMaterial(scene) + primitive.MaterialIndex, and a single descriptor set serves every draw.
    // Textures past the capacity get index -1 (no texture) and set Overflowed().
    class GltfMaterialLibrary
    {
    public:
        explicit GltfMaterialLibrary(const std::uint32_t textureCapacity = kGltfFallbackTextureCapacity,
                                     const GltfPackOptions options = {})
            : m_Capacity(textureCapacity), m_Options(options)
        {
        }

        std::uint32_t Add(const GltfSceneData& scene)
        {
            if (const auto it = m_SceneBase.find(&scene); it != m_SceneBase.end()) return it->second;
            const auto base = static_cast<std::uint32_t>(m_Materials.size());
            for (const auto& material : scene.Materials) AddMaterial(material);
            m_SceneBase.emplace(&scene, base);
            return base;
        }

        std::uint32_t AddMaterial(const GltfMaterial& material)
        {
            const auto index = static_cast<std::uint32_t>(m_Materials.size());
            m_Materials.push_back(PackGltfMaterial(
                material, [this](const std::shared_ptr<VulkanImage>& image) { return TextureIndex(image); }, m_Options));
            return index;
        }

        std::int32_t TextureIndex(const std::shared_ptr<VulkanImage>& image)
        {
            if (!image) return -1;
            if (const auto it = m_TextureIndex.find(image.get()); it != m_TextureIndex.end()) return it->second;
            if (m_Images.size() >= m_Capacity)
            {
                m_Overflowed = true;
                return -1;
            }
            const auto index = static_cast<std::int32_t>(m_Images.size());
            m_Images.push_back(image);
            m_TextureIndex.emplace(image.get(), index);
            return index;
        }

        [[nodiscard]] std::uint32_t BaseMaterial(const GltfSceneData& scene) const
        {
            const auto it = m_SceneBase.find(&scene);
            return it != m_SceneBase.end() ? it->second : 0;
        }

        [[nodiscard]] const std::vector<GltfMaterialGpu>& Materials() const noexcept { return m_Materials; }
        [[nodiscard]] const std::vector<std::shared_ptr<VulkanImage>>& Images() const noexcept { return m_Images; }
        [[nodiscard]] std::uint32_t TextureCapacity() const noexcept { return m_Capacity; }
        [[nodiscard]] bool Overflowed() const noexcept { return m_Overflowed; }

        void Clear()
        {
            m_Materials.clear();
            m_Images.clear();
            m_TextureIndex.clear();
            m_SceneBase.clear();
            m_Overflowed = false;
        }

    private:
        std::uint32_t m_Capacity;
        GltfPackOptions m_Options;
        std::vector<GltfMaterialGpu> m_Materials;
        std::vector<std::shared_ptr<VulkanImage>> m_Images;
        std::unordered_map<const VulkanImage*, std::int32_t> m_TextureIndex;
        std::unordered_map<const GltfSceneData*, std::uint32_t> m_SceneBase;
        bool m_Overflowed = false;
    };

    // Defines for ShaderSource::defines when including pbr/material.glsl.
    [[nodiscard]] inline std::unordered_map<std::string, std::string> PbrShaderDefines(
        const GpuCapabilities& capabilities, const std::uint32_t textureCapacity)
    {
        std::unordered_map<std::string, std::string> defines{{"PBR_TEXTURE_CAPACITY", std::to_string(textureCapacity)}};
        if (capabilities.DescriptorIndexing) defines.emplace("PBR_DESCRIPTOR_INDEXING", "1");
        return defines;
    }

    // Image infos for a combined-image-sampler array of exactly TextureCapacity() entries; unused entries use
    // the fallback so the whole descriptor array is always valid.
    [[nodiscard]] inline std::vector<vk::DescriptorImageInfo> MakeTextureDescriptors(const GltfMaterialLibrary& library,
                                                                                    const VulkanImage& fallback)
    {
        std::vector<vk::DescriptorImageInfo> infos(
            library.TextureCapacity(), vk::DescriptorImageInfo{fallback.GetSampler(), fallback.GetImageView(),
                                                                vk::ImageLayout::eShaderReadOnlyOptimal});
        for (std::size_t i = 0; i < library.Images().size(); ++i)
        {
            const auto& image = *library.Images()[i];
            infos[i] = vk::DescriptorImageInfo{image.GetSampler(), image.GetImageView(),
                                               vk::ImageLayout::eShaderReadOnlyOptimal};
        }
        return infos;
    }

    // Host-visible storage buffer holding Materials() (at least one entry, so it is never zero-sized).
    [[nodiscard]] VulkanBuffer CreateMaterialBuffer(const std::shared_ptr<VulkanDevice>& device,
                                                    const GltfMaterialLibrary& library,
                                                    const std::shared_ptr<Logger>& logger = {});
}
