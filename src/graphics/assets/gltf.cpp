module;
#include <glm/gtc/quaternion.hpp>
module GPP.Graphics;

import std;
import glm;
import vulkan;
import GPP.Core;
import :Vulkan;
import :Assets.Gltf;
import :Assets.Tangents;
import :Assets.Material;
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
        constexpr int kTypeVec4 = 4;

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

        glm::vec4 ReadVec4(const AccessorView& view, const std::size_t index)
        {
            const auto* floats = reinterpret_cast<const float*>(view.Data + view.Stride * index);
            return {floats[0], floats[1], floats[2], floats[3]};
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

        vk::SamplerAddressMode ToAddressMode(const int wrap)
        {
            switch (wrap)
            {
            case 33071: return vk::SamplerAddressMode::eClampToEdge;
            case 33648: return vk::SamplerAddressMode::eMirroredRepeat;
            default: return vk::SamplerAddressMode::eRepeat;
            }
        }

        std::shared_ptr<VulkanImage> UploadTexture(const std::shared_ptr<VulkanDevice>& device,
                                                   VulkanCommandPool& uploadPool, vk::Queue queue,
                                                   const tinygltf::Image& image, const vk::Format format,
                                                   const tinygltf::Sampler* sampler, std::string debugName,
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
            const std::uint32_t mipLevels =
                SupportsLinearBlit(device->GetPhysicalDevice(), format) ? MipLevelCount(extent) : 1u;

            VulkanBuffer staging(device, MakeStagingBufferSpecification(byteSize), logger);
            staging.Upload(image.image.data(), byteSize);

            auto gpuImage = std::make_shared<VulkanImage>(
                device,
                VulkanImageSpecification{
                    .extent = extent,
                    .format = format,
                    .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst |
                             vk::ImageUsageFlagBits::eTransferSrc,
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .mipLevels = mipLevels,
                    .createSampler = true,
                    .samplerFilter = sampler && sampler->magFilter == 9728 ? vk::Filter::eNearest
                                                                          : vk::Filter::eLinear,
                    .samplerAddressMode = sampler ? ToAddressMode(sampler->wrapS) : vk::SamplerAddressMode::eRepeat,
                    .debugName = std::move(debugName)
                },
                logger);

            ImmediateSubmit(uploadPool, queue, [&](const vk::CommandBuffer cmd)
            {
                TransitionImageLayout(cmd, gpuImage->GetImage(), format, vk::ImageLayout::eUndefined,
                                      vk::ImageLayout::eTransferDstOptimal, mipLevels);
                CopyBufferToImage(cmd, staging.GetBuffer(), gpuImage->GetImage(), extent);
                if (mipLevels > 1)
                {
                    GenerateMipmaps(cmd, gpuImage->GetImage(), extent, mipLevels);
                }
                else
                {
                    TransitionImageLayout(cmd, gpuImage->GetImage(), format, vk::ImageLayout::eTransferDstOptimal,
                                          vk::ImageLayout::eShaderReadOnlyOptimal);
                }
            });
            return gpuImage;
        }

        struct TextureKey
        {
            int Source = -1;
            int Sampler = -1;
            bool Srgb = false;
            auto operator<=>(const TextureKey&) const = default;
        };

        struct GltfUpload
        {
            const std::shared_ptr<VulkanDevice>& Device;
            VulkanCommandPool& Pool;
            vk::Queue Queue;
            const std::shared_ptr<Logger>& Log;
        };

        void UploadGeometry(const GltfUpload& upload, GltfPrimitive& primitive, const std::vector<GltfVertex>& vertices,
                            const std::vector<std::uint32_t>& indices, const std::vector<GltfVertexExtra>& extras)
        {
            vk::BufferUsageFlags traceable{};
            if (upload.Device->GetCapabilities().AccelerationStructure)
            {
                traceable = vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eStorageBuffer
                            | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR;
            }
            const auto vertexBytes = static_cast<vk::DeviceSize>(vertices.size() * sizeof(GltfVertex));
            const auto indexBytes = static_cast<vk::DeviceSize>(indices.size() * sizeof(std::uint32_t));
            const auto extraBytes = static_cast<vk::DeviceSize>(extras.size() * sizeof(GltfVertexExtra));
            auto vertexSpec = MakeVertexBufferSpecification(vertexBytes);
            vertexSpec.usage |= traceable;
            auto indexSpec = MakeIndexBufferSpecification(indexBytes);
            indexSpec.usage |= traceable;
            auto extraSpec = MakeVertexBufferSpecification(extraBytes);
            extraSpec.usage |= vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eStorageBuffer |
                               vk::BufferUsageFlagBits::eTransferSrc;
            primitive.VertexBuffer.Create(upload.Device, vertexSpec);
            primitive.IndexBuffer.Create(upload.Device, indexSpec);
            primitive.ExtraBuffer.Create(upload.Device, extraSpec);

            VulkanBuffer staging(upload.Device, MakeStagingBufferSpecification(vertexBytes + indexBytes + extraBytes),
                                 upload.Log);
            staging.Upload(vertices.data(), vertexBytes);
            staging.Upload(indices.data(), indexBytes, vertexBytes);
            staging.Upload(extras.data(), extraBytes, vertexBytes + indexBytes);
            ImmediateSubmit(upload.Pool, upload.Queue, [&](const vk::CommandBuffer cmd)
            {
                CopyBuffer(cmd, staging.GetBuffer(), primitive.VertexBuffer.GetBuffer(), vertexBytes);
                CopyBuffer(cmd, staging.GetBuffer(), primitive.IndexBuffer.GetBuffer(), indexBytes, vertexBytes);
                CopyBuffer(cmd, staging.GetBuffer(), primitive.ExtraBuffer.GetBuffer(), extraBytes,
                           vertexBytes + indexBytes);
            });
        }

        void ProcessPrimitive(const tinygltf::Model& model, const tinygltf::Primitive& primitive,
                              const glm::mat4& worldTransform, const glm::mat3& normalMatrix,
                              const GltfUpload& upload, const std::filesystem::path& resolvedPath,
                              GltfSceneData& scene, GltfMesh& outMesh)
        {
            const auto& logger = upload.Log;
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
            const auto optionalView = [&](const char* name, const int type) -> std::optional<AccessorView>
            {
                if (const auto it = primitive.attributes.find(name); it != primitive.attributes.end())
                {
                    if (auto view = GetAccessorView(model, it->second); IsFloatVec(view, type)) return view;
                }
                return std::nullopt;
            };
            const auto texCoordView = optionalView("TEXCOORD_0", kTypeVec2);
            const auto texCoord1View = optionalView("TEXCOORD_1", kTypeVec2);
            const auto tangentView = optionalView("TANGENT", kTypeVec4);

            const float handedness = glm::determinant(glm::mat3(worldTransform)) < 0.0f ? -1.0f : 1.0f;
            std::vector<GltfVertex> vertices(positionView.Count);
            std::vector<GltfVertexExtra> extras(positionView.Count);
            for (std::size_t i = 0; i < positionView.Count; ++i)
            {
                const auto localPosition = ReadVec3(positionView, i);
                vertices[i].Position = glm::vec3(worldTransform * glm::vec4(localPosition, 1.0f));
                const auto localNormal = normalView ? ReadVec3(*normalView, i) : glm::vec3(0.0f, 1.0f, 0.0f);
                vertices[i].Normal = glm::normalize(normalMatrix * localNormal);
                if (texCoordView) vertices[i].TexCoord = ReadVec2(*texCoordView, i);
                if (texCoord1View) extras[i].TexCoord1 = ReadVec2(*texCoord1View, i);
                if (tangentView)
                {
                    const auto local = ReadVec4(*tangentView, i);
                    const glm::vec3 n = vertices[i].Normal;
                    const glm::vec3 world = glm::mat3(worldTransform) * glm::vec3(local);
                    const glm::vec3 t = world - n * glm::dot(n, world);
                    extras[i].Tangent = glm::dot(t, t) > 1e-20f
                                            ? glm::vec4(glm::normalize(t), local.w * handedness)
                                            : ArbitraryTangent(n);
                }
            }

            const auto indexView = GetAccessorView(model, primitive.indices);
            std::vector<std::uint32_t> indices(indexView.Count);
            for (std::size_t i = 0; i < indexView.Count; ++i)
            {
                indices[i] = ReadIndex(indexView, i);
            }

            if (vertices.empty() || indices.empty()) return;

            if (!tangentView)
            {
                int normalSet = 0;
                if (primitive.material >= 0 && static_cast<std::size_t>(primitive.material) < scene.Materials.size())
                {
                    normalSet = scene.Materials[static_cast<std::size_t>(primitive.material)]
                                    .Texture(GltfTextureRole::Normal).TexCoord;
                }
                std::vector<glm::vec3> positions(vertices.size());
                std::vector<glm::vec3> normals(vertices.size());
                std::vector<glm::vec2> uvs(vertices.size());
                for (std::size_t i = 0; i < vertices.size(); ++i)
                {
                    positions[i] = vertices[i].Position;
                    normals[i] = vertices[i].Normal;
                    uvs[i] = normalSet == 1 && texCoord1View ? extras[i].TexCoord1 : vertices[i].TexCoord;
                }
                const bool hasUv = normalSet == 1 ? texCoord1View.has_value() : texCoordView.has_value();
                const auto tangents = GenerateTangents(positions, normals,
                                                       hasUv ? std::span<const glm::vec2>(uvs)
                                                             : std::span<const glm::vec2>{},
                                                       indices);
                for (std::size_t i = 0; i < vertices.size(); ++i) extras[i].Tangent = tangents[i];
            }

            GltfPrimitive outPrimitive;
            UploadGeometry(upload, outPrimitive, vertices, indices, extras);
            outPrimitive.VertexCount = static_cast<std::uint32_t>(vertices.size());
            outPrimitive.IndexCount = static_cast<std::uint32_t>(indices.size());
            outPrimitive.MaterialIndex = primitive.material;
            outPrimitive.HasAuthoredTangents = tangentView.has_value();
            outPrimitive.HasTexCoord1 = texCoord1View.has_value();

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
                     const GltfUpload& upload, const std::filesystem::path& resolvedPath, GltfSceneData& scene)
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
                    ProcessPrimitive(model, primitive, worldTransform, normalMatrix, upload, resolvedPath, scene,
                                     outMesh);
                }
                if (!outMesh.Primitives.empty())
                {
                    scene.Meshes.push_back(std::move(outMesh));
                }
            }

            for (const auto child : node.children)
            {
                WalkNode(model, child, worldTransform, upload, resolvedPath, scene);
            }
        }
    }

    namespace
    {
        double NumberOr(const tinygltf::Value& value, const double fallback)
        {
            return value.IsNumber() ? value.GetNumberAsDouble() : fallback;
        }

        template <std::size_t N>
        bool ReadNumbers(const tinygltf::Value& value, std::array<float, N>& out)
        {
            if (!value.IsArray() || value.ArrayLen() != N) return false;
            for (std::size_t i = 0; i < N; ++i) out[i] = static_cast<float>(NumberOr(value.Get(static_cast<int>(i)), 0.0));
            return true;
        }

        void ReadTransform(const tinygltf::ExtensionMap& extensions, GltfTextureBinding& binding)
        {
            const auto it = extensions.find("KHR_texture_transform");
            if (it == extensions.end()) return;
            const auto& value = it->second;
            std::array<float, 2> pair{};
            if (value.Has("offset") && ReadNumbers(value.Get("offset"), pair)) binding.Transform.Offset = {pair[0], pair[1]};
            if (value.Has("scale") && ReadNumbers(value.Get("scale"), pair)) binding.Transform.Scale = {pair[0], pair[1]};
            if (value.Has("rotation")) binding.Transform.Rotation = static_cast<float>(NumberOr(value.Get("rotation"), 0.0));
            if (value.Has("texCoord")) binding.TexCoord = static_cast<int>(NumberOr(value.Get("texCoord"), binding.TexCoord));
        }

        void Bind(GltfMaterial& material, GltfTextureIndices& indices, const GltfTextureRole role, const int index,
                  const int texCoord, const tinygltf::ExtensionMap& extensions)
        {
            auto& binding = material.Texture(role);
            indices[static_cast<std::size_t>(role)] = index;
            binding.TexCoord = texCoord;
            if (index >= 0) ReadTransform(extensions, binding);
        }

        void BindFromValue(GltfMaterial& material, GltfTextureIndices& indices, const GltfTextureRole role,
                           const tinygltf::Value& info)
        {
            if (!info.IsObject() || !info.Has("index")) return;
            const int index = static_cast<int>(NumberOr(info.Get("index"), -1.0));
            const int texCoord = info.Has("texCoord") ? static_cast<int>(NumberOr(info.Get("texCoord"), 0.0)) : 0;
            tinygltf::ExtensionMap extensions;
            if (info.Has("extensions") && info.Get("extensions").IsObject())
            {
                for (const auto& key : info.Get("extensions").Keys()) extensions[key] = info.Get("extensions").Get(key);
            }
            Bind(material, indices, role, index, texCoord, extensions);
        }
    }

    GltfMaterial ReadGltfMaterial(const tinygltf::Material& material, GltfTextureIndices& textureIndices)
    {
        GltfMaterial out;
        textureIndices.fill(-1);
        out.Name = material.name;

        const auto& pbr = material.pbrMetallicRoughness;
        if (pbr.baseColorFactor.size() == 4)
        {
            out.BaseColorFactor = {static_cast<float>(pbr.baseColorFactor[0]), static_cast<float>(pbr.baseColorFactor[1]),
                                   static_cast<float>(pbr.baseColorFactor[2]), static_cast<float>(pbr.baseColorFactor[3])};
        }
        out.MetallicFactor = static_cast<float>(pbr.metallicFactor);
        out.RoughnessFactor = static_cast<float>(pbr.roughnessFactor);
        if (material.emissiveFactor.size() == 3)
        {
            out.EmissiveFactor = {static_cast<float>(material.emissiveFactor[0]),
                                  static_cast<float>(material.emissiveFactor[1]),
                                  static_cast<float>(material.emissiveFactor[2])};
        }
        out.NormalScale = static_cast<float>(material.normalTexture.scale);
        out.OcclusionStrength = static_cast<float>(material.occlusionTexture.strength);
        out.AlphaCutoff = static_cast<float>(material.alphaCutoff);
        out.DoubleSided = material.doubleSided;
        out.AlphaMode = material.alphaMode == "MASK"    ? GltfAlphaMode::Mask
                        : material.alphaMode == "BLEND" ? GltfAlphaMode::Blend
                                                        : GltfAlphaMode::Opaque;

        Bind(out, textureIndices, GltfTextureRole::BaseColor, pbr.baseColorTexture.index, pbr.baseColorTexture.texCoord,
             pbr.baseColorTexture.extensions);
        Bind(out, textureIndices, GltfTextureRole::MetallicRoughness, pbr.metallicRoughnessTexture.index,
             pbr.metallicRoughnessTexture.texCoord, pbr.metallicRoughnessTexture.extensions);
        Bind(out, textureIndices, GltfTextureRole::Normal, material.normalTexture.index, material.normalTexture.texCoord,
             material.normalTexture.extensions);
        Bind(out, textureIndices, GltfTextureRole::Occlusion, material.occlusionTexture.index,
             material.occlusionTexture.texCoord, material.occlusionTexture.extensions);
        Bind(out, textureIndices, GltfTextureRole::Emissive, material.emissiveTexture.index,
             material.emissiveTexture.texCoord, material.emissiveTexture.extensions);

        const auto& extensions = material.extensions;
        if (const auto it = extensions.find("KHR_materials_emissive_strength"); it != extensions.end())
        {
            out.EmissiveStrength = static_cast<float>(NumberOr(it->second.Get("emissiveStrength"), 1.0));
        }
        if (const auto it = extensions.find("KHR_materials_ior"); it != extensions.end())
        {
            out.Ior = static_cast<float>(std::max(NumberOr(it->second.Get("ior"), 1.5), 1.0));
        }
        if (const auto it = extensions.find("KHR_materials_specular"); it != extensions.end())
        {
            const auto& specular = it->second;
            out.SpecularFactor = static_cast<float>(NumberOr(specular.Get("specularFactor"), 1.0));
            std::array<float, 3> color{};
            if (specular.Has("specularColorFactor") && ReadNumbers(specular.Get("specularColorFactor"), color))
            {
                out.SpecularColorFactor = {color[0], color[1], color[2]};
            }
            BindFromValue(out, textureIndices, GltfTextureRole::Specular, specular.Get("specularTexture"));
            BindFromValue(out, textureIndices, GltfTextureRole::SpecularColor, specular.Get("specularColorTexture"));
        }
        return out;
    }

    VulkanBuffer CreateMaterialBuffer(const std::shared_ptr<VulkanDevice>& device, const GltfMaterialLibrary& library,
                                      const std::shared_ptr<Logger>& logger)
    {
        const GltfMaterialGpu placeholder;
        const auto* data = library.Materials().empty() ? &placeholder : library.Materials().data();
        const auto count = std::max<std::size_t>(library.Materials().size(), 1);
        const auto bytes = static_cast<vk::DeviceSize>(count * sizeof(GltfMaterialGpu));
        VulkanBuffer buffer(device, MakeStorageBufferSpecification(bytes, true), logger);
        buffer.Upload(data, bytes);
        return buffer;
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

        std::map<TextureKey, std::shared_ptr<VulkanImage>> textureCache;
        const auto resolveTexture = [&](const GltfTextureRole role, const int textureIndex) -> std::shared_ptr<VulkanImage>
        {
            if (textureIndex < 0 || static_cast<std::size_t>(textureIndex) >= model.textures.size()) return nullptr;
            const auto& texture = model.textures[static_cast<std::size_t>(textureIndex)];
            if (texture.source < 0 || static_cast<std::size_t>(texture.source) >= model.images.size()) return nullptr;
            const TextureKey key{texture.source, texture.sampler, GltfTextureRoleIsSrgb(role)};
            if (const auto it = textureCache.find(key); it != textureCache.end()) return it->second;
            const tinygltf::Sampler* sampler =
                texture.sampler >= 0 && static_cast<std::size_t>(texture.sampler) < model.samplers.size()
                    ? &model.samplers[static_cast<std::size_t>(texture.sampler)]
                    : nullptr;
            const auto& image = model.images[static_cast<std::size_t>(texture.source)];
            auto gpu = UploadTexture(device, uploadPool, queue, image, GltfTextureFormat(role), sampler,
                                     image.name.empty() ? image.uri : image.name, logger);
            textureCache.emplace(key, gpu);
            if (gpu) scene.Textures.push_back(gpu);
            return gpu;
        };

        scene.Materials.reserve(model.materials.size());
        for (const auto& material : model.materials)
        {
            GltfTextureIndices indices;
            auto out = ReadGltfMaterial(material, indices);
            for (std::size_t role = 0; role < kGltfTextureRoleCount; ++role)
            {
                out.Textures[role].Image = resolveTexture(static_cast<GltfTextureRole>(role), indices[role]);
            }
            out.BaseColorTexture = out.Texture(GltfTextureRole::BaseColor).Image;
            scene.Materials.push_back(std::move(out));
        }

        scene.Meshes.reserve(model.meshes.size());
        const GltfUpload upload{device, uploadPool, queue, logger};
        if (!model.scenes.empty())
        {
            const auto sceneIndex = static_cast<std::size_t>(model.defaultScene >= 0 ? model.defaultScene : 0);
            const auto& rootNodes = model.scenes.at(sceneIndex).nodes;
            for (const auto rootNode : rootNodes)
            {
                WalkNode(model, rootNode, glm::mat4(1.0f), upload, resolvedPath, scene);
            }
        }
        else
        {
            // No scene graph at all (rare/malformed files) -- fall back to treating every node as
            // its own root so the mesh data isn't simply dropped.
            for (std::size_t i = 0; i < model.nodes.size(); ++i)
            {
                WalkNode(model, static_cast<int>(i), glm::mat4(1.0f), upload, resolvedPath, scene);
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
