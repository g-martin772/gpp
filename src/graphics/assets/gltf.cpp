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
                    .format = vk::Format::eR8G8B8A8Unorm,
                    .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .createSampler = true,
                    .debugName = "GltfBaseColorTexture"
                },
                logger);

            ImmediateSubmit(uploadPool, queue, [&](const vk::CommandBuffer cmd)
            {
                TransitionImageLayout(cmd, gpuImage->GetImage(), vk::Format::eR8G8B8A8Unorm,
                                      vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
                CopyBufferToImage(cmd, staging.GetBuffer(), gpuImage->GetImage(), extent);
                TransitionImageLayout(cmd, gpuImage->GetImage(), vk::Format::eR8G8B8A8Unorm,
                                      vk::ImageLayout::eTransferDstOptimal,
                                      vk::ImageLayout::eShaderReadOnlyOptimal);
            });
            return gpuImage;
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
        for (const auto& mesh : model.meshes)
        {
            GltfMesh outMesh;
            outMesh.Name = mesh.name;
            outMesh.Primitives.reserve(mesh.primitives.size());

            for (const auto& primitive : mesh.primitives)
            {
                const auto positionIt = primitive.attributes.find("POSITION");
                if (positionIt == primitive.attributes.end() || primitive.indices < 0)
                {
                    if (logger)
                    {
                        logger->Warn("glTF '{}': skipping primitive with no POSITION or indices.",
                                    resolvedPath.string());
                    }
                    continue;
                }

                const auto positionView = GetAccessorView(model, positionIt->second);
                if (!IsFloatVec(positionView, kTypeVec3))
                {
                    if (logger)
                    {
                        logger->Warn("glTF '{}': skipping primitive with a non-float POSITION accessor.",
                                    resolvedPath.string());
                    }
                    continue;
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
                    vertices[i].Position = ReadVec3(positionView, i);
                    if (normalView) vertices[i].Normal = ReadVec3(*normalView, i);
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

                const auto indexBytes =
                    static_cast<vk::DeviceSize>(indices.size() * sizeof(std::uint32_t));
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

            scene.Meshes.push_back(std::move(outMesh));
        }

        if (logger)
        {
            logger->Info("Loaded glTF '{}': {} mesh(es), {} material(s)",
                        resolvedPath.string(), scene.Meshes.size(), scene.Materials.size());
        }

        return scene;
    }
}
