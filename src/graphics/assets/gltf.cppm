module;
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>
export module GPP.Graphics:Assets.Gltf;

import std;
import glm;
import vulkan;
import GPP.Core;
import :Vulkan;

export namespace tinygltf
{
    using tinygltf::TinyGLTF;
    using tinygltf::Model;
    using tinygltf::Scene;
    using tinygltf::Node;
    using tinygltf::Mesh;
    using tinygltf::Primitive;
    using tinygltf::Accessor;
    using tinygltf::BufferView;
    using tinygltf::Buffer;
    using tinygltf::Material;
    using tinygltf::Texture;
    using tinygltf::Image;
    using tinygltf::Sampler;
    using tinygltf::Camera;
    using tinygltf::Animation;
    using tinygltf::Skin;
    using tinygltf::Value;
    using tinygltf::ExtensionMap;
    using tinygltf::TextureInfo;
    using tinygltf::NormalTextureInfo;
    using tinygltf::OcclusionTextureInfo;
    using tinygltf::PbrMetallicRoughness;
}

namespace GPP
{
    export enum class GltfTextureRole : std::uint8_t
    {
        BaseColor, MetallicRoughness, Normal, Occlusion, Emissive, Specular, SpecularColor
    };

    export inline constexpr std::size_t kGltfTextureRoleCount = 7;

    export [[nodiscard]] constexpr bool GltfTextureRoleIsSrgb(const GltfTextureRole role) noexcept
    {
        return role == GltfTextureRole::BaseColor || role == GltfTextureRole::Emissive ||
               role == GltfTextureRole::SpecularColor;
    }

    export [[nodiscard]] constexpr vk::Format GltfTextureFormat(const GltfTextureRole role) noexcept
    {
        return GltfTextureRoleIsSrgb(role) ? vk::Format::eR8G8B8A8Srgb : vk::Format::eR8G8B8A8Unorm;
    }

    export enum class GltfAlphaMode : std::uint8_t { Opaque, Mask, Blend };

    // KHR_texture_transform: uv' = Offset + R(Rotation) * (Scale * uv), rotation counter-clockwise in uv space.
    export struct GltfTextureTransform
    {
        glm::vec2 Offset{0.0f};
        float Rotation = 0.0f;
        glm::vec2 Scale{1.0f};

        [[nodiscard]] glm::mat2 Matrix() const
        {
            const float c = std::cos(Rotation);
            const float s = std::sin(Rotation);
            return {glm::vec2(c * Scale.x, s * Scale.x), glm::vec2(-s * Scale.y, c * Scale.y)};
        }
    };

    // Position, normal and first UV set, tightly packed (32 bytes). Existing consumers rely on this layout.
    export struct GltfVertex
    {
        glm::vec3 Position{0.0f};
        glm::vec3 Normal{0.0f, 1.0f, 0.0f};
        glm::vec2 TexCoord{0.0f};
    };

    // Parallel per-vertex stream (32 bytes) in GltfPrimitive::ExtraBuffer. Tangent.w is the bitangent sign:
    // bitangent = cross(Normal, Tangent.xyz) * Tangent.w points along +Y of the tangent-space normal map.
    export struct GltfVertexExtra
    {
        glm::vec4 Tangent{1.0f, 0.0f, 0.0f, 1.0f};
        glm::vec2 TexCoord1{0.0f};
        glm::vec2 Pad{0.0f};
    };

    export struct GltfPrimitive
    {
        VulkanBuffer VertexBuffer;
        VulkanBuffer IndexBuffer;
        VulkanBuffer ExtraBuffer;
        std::uint32_t VertexCount = 0;
        std::uint32_t IndexCount = 0;
        int MaterialIndex = -1;
        bool HasAuthoredTangents = false;
        bool HasTexCoord1 = false;
    };

    export struct GltfMesh
    {
        std::string Name;
        std::vector<GltfPrimitive> Primitives;
    };

    export struct GltfTextureBinding
    {
        std::shared_ptr<VulkanImage> Image;
        int TexCoord = 0;
        GltfTextureTransform Transform;

        explicit operator bool() const noexcept { return Image != nullptr; }
    };

    export struct GltfMaterial
    {
        std::string Name;
        glm::vec4 BaseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
        float MetallicFactor = 1.0f;
        float RoughnessFactor = 1.0f;
        glm::vec3 EmissiveFactor{0.0f};
        float EmissiveStrength = 1.0f;
        float NormalScale = 1.0f;
        float OcclusionStrength = 1.0f;
        float Ior = 1.5f;
        float SpecularFactor = 1.0f;
        glm::vec3 SpecularColorFactor{1.0f};
        GltfAlphaMode AlphaMode = GltfAlphaMode::Opaque;
        float AlphaCutoff = 0.5f;
        bool DoubleSided = false;
        std::array<GltfTextureBinding, kGltfTextureRoleCount> Textures;
        // Same image as Textures[BaseColor].Image; kept for consumers written before the full material set.
        std::shared_ptr<VulkanImage> BaseColorTexture;

        [[nodiscard]] GltfTextureBinding& Texture(const GltfTextureRole role) noexcept
        {
            return Textures[static_cast<std::size_t>(role)];
        }

        [[nodiscard]] const GltfTextureBinding& Texture(const GltfTextureRole role) const noexcept
        {
            return Textures[static_cast<std::size_t>(role)];
        }
    };

    // glTF texture indices per role (-1 = none), as read from the document.
    export using GltfTextureIndices = std::array<int, kGltfTextureRoleCount>;

    // Reads factors, flags, texture coordinate sets and KHR_texture_transform; images stay unresolved.
    export GltfMaterial ReadGltfMaterial(const tinygltf::Material& material, GltfTextureIndices& textureIndices);

    export struct GltfExtensionReport
    {
        std::vector<std::string> Unsupported;         // used, ignored
        std::vector<std::string> Approximated;        // used, mapped onto metallic-roughness
        std::vector<std::string> RequiredUnsupported; // required and not handled: the model may be incomplete
    };

    export [[nodiscard]] GltfExtensionReport AuditGltfExtensions(const std::vector<std::string>& used,
                                                                  const std::vector<std::string>& required);

    export struct GltfGeometry
    {
        std::vector<glm::vec3> Vertices;
        std::vector<std::uint32_t> Indices;
    };

    export struct GltfSceneData
    {
        std::vector<GltfMesh> Meshes;
        std::vector<GltfMaterial> Materials;
        // Unique GPU images of the scene; role-specific formats, full mip chains.
        std::vector<std::shared_ptr<VulkanImage>> Textures;
        GltfGeometry Geometry;
    };

    export GltfSceneData LoadGltfScene(const std::shared_ptr<VulkanDevice>& device,
                                       const std::shared_ptr<IFileSystem>& fileSystem,
                                       const std::filesystem::path& path,
                                       const std::shared_ptr<Logger>& logger = {});
}
