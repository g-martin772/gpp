module;
#include <glm/gtc/quaternion.hpp>
module GPP.Graphics;

import std;
import glm;
import vulkan;
import GPP.Core;
import :Vulkan;
import :Assets.Gltf;
import :Assets.Image;

namespace GPP
{
    namespace
    {
        constexpr int kComponentTypeUnsignedByte = 5121;
        constexpr int kComponentTypeUnsignedShort = 5123;
        constexpr int kComponentTypeUnsignedInt = 5125;
        constexpr int kComponentTypeFloat = 5126;
        constexpr int kTypeVec2 = 2;
        constexpr int kTypeVec3 = 3;

        struct AccessorView
        {
            const unsigned char* Data = nullptr;
            std::size_t Stride = 0;
            std::size_t Count = 0;
            int ComponentType = 0;
            int Type = 0;
        };

        AccessorView GetAccessorView(const tinygltf::Model& model, const int accessorIndex)
        {
            const auto& accessor = model.accessors.at(static_cast<std::size_t>(accessorIndex));
            const auto& view = model.bufferViews.at(static_cast<std::size_t>(accessor.bufferView));
            const auto& buffer = model.buffers.at(static_cast<std::size_t>(view.buffer));
            const auto stride = static_cast<std::size_t>(accessor.ByteStride(view));
            return AccessorView{
                buffer.data.data() + view.byteOffset + accessor.byteOffset,
                stride, accessor.count, accessor.componentType, accessor.type
            };
        }

        glm::vec3 ReadVec3(const AccessorView& view, const std::size_t index)
        {
            const auto* floats = reinterpret_cast<const float*>(view.Data + view.Stride * index);
            return {floats[0], floats[1], floats[2]};
        }

        glm::vec2 ReadVec2(const AccessorView& view, const std::size_t index)
        {
            const auto* floats = reinterpret_cast<const float*>(view.Data + view.Stride * index);
            return {floats[0], floats[1]};
        }

        std::uint32_t ReadIndex(const AccessorView& view, const std::size_t index)
        {
            const auto* ptr = view.Data + view.Stride * index;
            switch (view.ComponentType)
            {
            case kComponentTypeUnsignedByte:
                return *ptr;
            case kComponentTypeUnsignedShort:
                return *reinterpret_cast<const std::uint16_t*>(ptr);
            case kComponentTypeUnsignedInt:
                return *reinterpret_cast<const std::uint32_t*>(ptr);
            default:
                return 0;
            }
        }

        bool IsFloatVec(const AccessorView& view, const int expectedType)
        {
            return view.ComponentType == kComponentTypeFloat && view.Type == expectedType;
        }

        glm::mat4 LocalNodeTransform(const tinygltf::Node& node)
        {
            if (node.matrix.size() == 16)
            {
                glm::mat4 m;
                for (int c = 0; c < 4; ++c)
                {
                    for (int r = 0; r < 4; ++r)
                    {
                        m[c][r] = static_cast<float>(
                            node.matrix[static_cast<std::size_t>(c) * 4 + static_cast<std::size_t>(r)]);
                    }
                }
                return m;
            }

            glm::vec3 translation{0.0f};
            if (node.translation.size() == 3)
            {
                translation = {static_cast<float>(node.translation[0]), static_cast<float>(node.translation[1]),
                              static_cast<float>(node.translation[2])};
            }
            glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
            if (node.rotation.size() == 4)
            {
                rotation = glm::quat(static_cast<float>(node.rotation[3]), static_cast<float>(node.rotation[0]),
                                    static_cast<float>(node.rotation[1]), static_cast<float>(node.rotation[2]));
            }
            glm::vec3 scale{1.0f};
            if (node.scale.size() == 3)
            {
                scale = {static_cast<float>(node.scale[0]), static_cast<float>(node.scale[1]),
                        static_cast<float>(node.scale[2])};
            }
            return glm::translate(glm::mat4(1.0f), translation) * glm::mat4_cast(rotation) *
                   glm::scale(glm::mat4(1.0f), scale);
        }

        bool DecodeEmbeddedImage(tinygltf::Image* image, const int, std::string* err, std::string*,
                                 int, int, const unsigned char* bytes, const int size, void*)
        {
            int width = 0, height = 0, channels = 0;
            unsigned char* decoded = stbi_load_from_memory(bytes, size, &width, &height, &channels,
                                                           STBI_rgb_alpha);
            if (!decoded)
            {
                if (err)
                {
                    *err += std::string("Failed to decode embedded glTF image: ") +
                        (stbi_failure_reason() ? stbi_failure_reason() : "unknown error");
                }
                return false;
            }
            image->width = width;
            image->height = height;
            image->component = 4;
            image->bits = 8;
            image->pixel_type = kComponentTypeUnsignedByte;
            image->image.assign(decoded, decoded + (static_cast<std::size_t>(width) * height * 4));
            stbi_image_free(decoded);
            return true;
        }

        std::shared_ptr<VulkanImage> UploadTexture(const std::shared_ptr<VulkanDevice>& device,
                                                   VulkanCommandPool& uploadPool, vk::Queue queue,
                                                   const tinygltf::Image& image,
                                                   const std::shared_ptr<Logger>& logger)
        {
            if (image.image.empty() || image.width <= 0 || image.height <= 0)
            {
                return nullptr;
            }
            const auto extent = vk::Extent3D{
                static_cast<std::uint32_t>(image.width), static_cast<std::uint32_t>(image.height), 1
            };
            const auto byteSize = static_cast<vk::DeviceSize>(image.image.size());

            VulkanBuffer staging(device, MakeStagingBufferSpecification(byteSize), logger);
            staging.Upload(image.image.data(), byteSize);

            auto gpuImage = std::make_shared<VulkanImage>(
                device,
                VulkanImageSpecification{
                    .extent = extent,
                    .format = vk::Format::eR8G8B8A8Srgb,
                    .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .createSampler = true,
                    .debugName = "GltfBaseColorTexture"
                },
                logger);

            ImmediateSubmit(uploadPool, queue, [&](const vk::CommandBuffer cmd)
            {
                TransitionImageLayout(cmd, gpuImage->GetImage(), vk::Format::eR8G8B8A8Srgb,
                                      vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
                CopyBufferToImage(cmd, staging.GetBuffer(), gpuImage->GetImage(), extent);
                TransitionImageLayout(cmd, gpuImage->GetImage(), vk::Format::eR8G8B8A8Srgb,
                                      vk::ImageLayout::eTransferDstOptimal,
                                      vk::ImageLayout::eShaderReadOnlyOptimal);
            });
            return gpuImage;
        }

        void ProcessPrimitive(const tinygltf::Model& model, const tinygltf::Primitive& primitive,
                              const glm::mat4& worldTransform, const glm::mat3& normalMatrix,
                              const std::shared_ptr<VulkanDevice>& device,
                              const std::filesystem::path& resolvedPath, const std::shared_ptr<Logger>& logger,
                              GltfSceneData& scene, GltfMesh& outMesh)
        {
            const auto positionIt = primitive.attributes.find("POSITION");
            if (positionIt == primitive.attributes.end() || primitive.indices < 0)
            {
                if (logger)
                {
                    logger->Warn("glTF '{}': skipping primitive with no POSITION or indices.",
                                resolvedPath.string());
                }
                return;
            }

            const auto positionView = GetAccessorView(model, positionIt->second);
            if (!IsFloatVec(positionView, kTypeVec3))
            {
                if (logger)
                {
                    logger->Warn("glTF '{}': skipping primitive with a non-float POSITION accessor.",
                                resolvedPath.string());
                }
                return;
            }

            std::optional<AccessorView> normalView;
            if (const auto it = primitive.attributes.find("NORMAL"); it != primitive.attributes.end())
            {
                if (auto view = GetAccessorView(model, it->second); IsFloatVec(view, kTypeVec3))
                {
                    normalView = view;
                }
            }
            std::optional<AccessorView> texCoordView;
            if (const auto it = primitive.attributes.find("TEXCOORD_0"); it != primitive.attributes.end())
            {
                if (auto view = GetAccessorView(model, it->second); IsFloatVec(view, kTypeVec2))
                {
                    texCoordView = view;
                }
            }

            std::vector<GltfVertex> vertices(positionView.Count);
            for (std::size_t i = 0; i < positionView.Count; ++i)
            {
                const auto localPosition = ReadVec3(positionView, i);
                vertices[i].Position = glm::vec3(worldTransform * glm::vec4(localPosition, 1.0f));
                const auto localNormal = normalView ? ReadVec3(*normalView, i) : glm::vec3(0.0f, 1.0f, 0.0f);
                vertices[i].Normal = glm::normalize(normalMatrix * localNormal);
                if (texCoordView) vertices[i].TexCoord = ReadVec2(*texCoordView, i);
            }

            const auto indexView = GetAccessorView(model, primitive.indices);
            std::vector<std::uint32_t> indices(indexView.Count);
            for (std::size_t i = 0; i < indexView.Count; ++i)
            {
                indices[i] = ReadIndex(indexView, i);
            }

            GltfPrimitive outPrimitive;
            const auto vertexBytes = static_cast<vk::DeviceSize>(vertices.size() * sizeof(GltfVertex));
            outPrimitive.VertexBuffer.Create(device, MakeVertexBufferSpecification(vertexBytes, true));
            outPrimitive.VertexBuffer.Upload(vertices.data(), vertexBytes);

            const auto indexBytes = static_cast<vk::DeviceSize>(indices.size() * sizeof(std::uint32_t));
            outPrimitive.IndexBuffer.Create(device, MakeIndexBufferSpecification(indexBytes, true));
            outPrimitive.IndexBuffer.Upload(indices.data(), indexBytes);
            outPrimitive.IndexCount = static_cast<std::uint32_t>(indices.size());
            outPrimitive.MaterialIndex = primitive.material;

            const auto baseVertex = static_cast<std::uint32_t>(scene.Geometry.Vertices.size());
            scene.Geometry.Vertices.reserve(scene.Geometry.Vertices.size() + vertices.size());
            for (const auto& vertex : vertices)
            {
                scene.Geometry.Vertices.push_back(vertex.Position);
            }
            scene.Geometry.Indices.reserve(scene.Geometry.Indices.size() + indices.size());
            for (const auto index : indices)
            {
                scene.Geometry.Indices.push_back(baseVertex + index);
            }

            outMesh.Primitives.push_back(std::move(outPrimitive));
        }

        void WalkNode(const tinygltf::Model& model, int nodeIndex, const glm::mat4& parentTransform,
                     const std::shared_ptr<VulkanDevice>& device, const std::filesystem::path& resolvedPath,
                     const std::shared_ptr<Logger>& logger, GltfSceneData& scene)
        {
            if (nodeIndex < 0 || static_cast<std::size_t>(nodeIndex) >= model.nodes.size()) return;
            const auto& node = model.nodes[static_cast<std::size_t>(nodeIndex)];
            const glm::mat4 worldTransform = parentTransform * LocalNodeTransform(node);

            if (node.mesh >= 0 && static_cast<std::size_t>(node.mesh) < model.meshes.size())
            {
                const auto& mesh = model.meshes[static_cast<std::size_t>(node.mesh)];
                GltfMesh outMesh;
                outMesh.Name = mesh.name;
                outMesh.Primitives.reserve(mesh.primitives.size());
                const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(worldTransform)));
                for (const auto& primitive : mesh.primitives)
                {
                    ProcessPrimitive(model, primitive, worldTransform, normalMatrix, device, resolvedPath,
                                     logger, scene, outMesh);
                }
                if (!outMesh.Primitives.empty())
                {
                    scene.Meshes.push_back(std::move(outMesh));
                }
            }

            for (const auto child : node.children)
            {
                WalkNode(model, child, worldTransform, device, resolvedPath, logger, scene);
            }
        }
    }

    GltfSceneData LoadGltfScene(const std::shared_ptr<VulkanDevice>& device,
                                const std::shared_ptr<IFileSystem>& fileSystem,
                                const std::filesystem::path& path,
                                const std::shared_ptr<Logger>& logger)
    {
        if (!device || !fileSystem)
        {
            throw std::invalid_argument("LoadGltfScene requires a device and file system.");
        }

        const auto resolvedPath = fileSystem->ResolvePath(path);

        tinygltf::TinyGLTF loader;
        loader.SetImageLoader(DecodeEmbeddedImage, nullptr);

        tinygltf::Model model;
        std::string err, warn;
        const bool isBinary = resolvedPath.extension() == ".glb";
        const bool loaded = isBinary
            ? loader.LoadBinaryFromFile(&model, &err, &warn, resolvedPath.string())
            : loader.LoadASCIIFromFile(&model, &err, &warn, resolvedPath.string());

        if (!warn.empty() && logger)
        {
            logger->Warn("glTF '{}': {}", resolvedPath.string(), warn);
        }
        if (!loaded)
        {
            throw std::runtime_error(
                std::format("Failed to load glTF '{}': {}", resolvedPath.string(), err));
        }

        GltfSceneData scene;
        VulkanCommandPool uploadPool(device, logger, device->GetQueueIndices().Graphics);
        const auto queue = device->GetGraphicsQueue();

        scene.Materials.reserve(model.materials.size());
        for (const auto& material : model.materials)
        {
            GltfMaterial out;
            const auto& base = material.pbrMetallicRoughness.baseColorFactor;
            if (base.size() == 4)
            {
                out.BaseColorFactor = {
                    static_cast<float>(base[0]), static_cast<float>(base[1]),
                    static_cast<float>(base[2]), static_cast<float>(base[3])
                };
            }
            out.MetallicFactor = static_cast<float>(material.pbrMetallicRoughness.metallicFactor);
            out.RoughnessFactor = static_cast<float>(material.pbrMetallicRoughness.roughnessFactor);

            const auto textureIndex = material.pbrMetallicRoughness.baseColorTexture.index;
            if (textureIndex >= 0 && static_cast<std::size_t>(textureIndex) < model.textures.size())
            {
                const auto source = model.textures[static_cast<std::size_t>(textureIndex)].source;
                if (source >= 0 && static_cast<std::size_t>(source) < model.images.size())
                {
                    out.BaseColorTexture = UploadTexture(
                        device, uploadPool, queue, model.images[static_cast<std::size_t>(source)],
                        logger);
                }
            }
            scene.Materials.push_back(std::move(out));
        }

        scene.Meshes.reserve(model.meshes.size());
        if (!model.scenes.empty())
        {
            const auto sceneIndex = static_cast<std::size_t>(model.defaultScene >= 0 ? model.defaultScene : 0);
            const auto& rootNodes = model.scenes.at(sceneIndex).nodes;
            for (const auto rootNode : rootNodes)
            {
                WalkNode(model, rootNode, glm::mat4(1.0f), device, resolvedPath, logger, scene);
            }
        }
        else
        {
            // No scene graph at all (rare/malformed files) -- fall back to treating every node as
            // its own root so the mesh data isn't simply dropped.
            for (std::size_t i = 0; i < model.nodes.size(); ++i)
            {
                WalkNode(model, static_cast<int>(i), glm::mat4(1.0f), device, resolvedPath, logger, scene);
            }
        }

        if (logger)
        {
            logger->Info("Loaded glTF '{}': {} mesh(es), {} material(s)",
                        resolvedPath.string(), scene.Meshes.size(), scene.Materials.size());
        }

        return scene;
    }
}
