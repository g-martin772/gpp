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
}

namespace GPP
{
    export struct GltfVertex
    {
        glm::vec3 Position{0.0f};
        glm::vec3 Normal{0.0f, 1.0f, 0.0f};
        glm::vec2 TexCoord{0.0f};
    };

    export struct GltfPrimitive
    {
        VulkanBuffer VertexBuffer;
        VulkanBuffer IndexBuffer;
        std::uint32_t IndexCount = 0;
        int MaterialIndex = -1;
    };

    export struct GltfMesh
    {
        std::string Name;
        std::vector<GltfPrimitive> Primitives;
    };

    export struct GltfMaterial
    {
        glm::vec4 BaseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
        float MetallicFactor = 1.0f;
        float RoughnessFactor = 1.0f;
        std::shared_ptr<VulkanImage> BaseColorTexture;
    };

    export struct GltfGeometry
    {
        std::vector<glm::vec3> Vertices;
        std::vector<std::uint32_t> Indices;
    };

    export struct GltfSceneData
    {
        std::vector<GltfMesh> Meshes;
        std::vector<GltfMaterial> Materials;
        GltfGeometry Geometry;
    };

    export GltfSceneData LoadGltfScene(const std::shared_ptr<VulkanDevice>& device,
                                       const std::shared_ptr<IFileSystem>& fileSystem,
                                       const std::filesystem::path& path,
                                       const std::shared_ptr<Logger>& logger = {});
}
