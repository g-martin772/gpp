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
        std::string DecodeUri(const std::string& uri)
        {
            std::string out;
            for (std::size_t i = 0; i < uri.size(); ++i)
            {
                if (uri[i] == '%' && i + 2 < uri.size() && std::isxdigit(static_cast<unsigned char>(uri[i + 1])) &&
                    std::isxdigit(static_cast<unsigned char>(uri[i + 2])))
                {
                    out.push_back(static_cast<char>(std::stoi(uri.substr(i + 1, 2), nullptr, 16)));
                    i += 2;
                }
                else
                {
                    out.push_back(uri[i]);
                }
            }
            return out;
        }

        std::vector<std::filesystem::path> ExternalSources(const tinygltf::Model& model,
                                                           const std::filesystem::path& directory)
        {
            std::vector<std::filesystem::path> sources;
            const auto add = [&](const std::string& uri)
            {
                if (!uri.empty() && !uri.starts_with("data:")) sources.push_back(directory / DecodeUri(uri));
            };
            for (const auto& buffer : model.buffers) add(buffer.uri);
            for (const auto& image : model.images) add(image.uri);
            return sources;
        }

        constexpr std::size_t kDecodeWorkers = 8;
        constexpr int kComponentTypeByte = 5120;
        constexpr int kComponentTypeUnsignedByte = 5121;
        constexpr int kComponentTypeShort = 5122;
        constexpr int kComponentTypeUnsignedShort = 5123;
        constexpr int kComponentTypeUnsignedInt = 5125;
        constexpr int kComponentTypeFloat = 5126;
        constexpr int kTypeScalar = 64 + 1;
        constexpr int kTypeVec2 = 2;
        constexpr int kTypeVec3 = 3;
        constexpr int kTypeVec4 = 4;
        constexpr int kModeTriangles = 4;
        constexpr int kModeTriangleStrip = 5;
        constexpr int kModeTriangleFan = 6;

        struct AccessorView
        {
            const unsigned char* Data = nullptr;
            std::size_t Stride = 0;
            std::size_t Count = 0;
            int ComponentType = 0;
            int Type = 0;
            bool Normalized = false;
        };

        std::size_t ComponentSize(const int componentType)
        {
            switch (componentType)
            {
            case kComponentTypeByte:
            case kComponentTypeUnsignedByte: return 1;
            case kComponentTypeShort:
            case kComponentTypeUnsignedShort: return 2;
            case kComponentTypeUnsignedInt:
            case kComponentTypeFloat: return 4;
            default: return 0;
            }
        }

        int ComponentCount(const int type)
        {
            switch (type)
            {
            case kTypeScalar: return 1;
            case kTypeVec2: return 2;
            case kTypeVec3: return 3;
            case kTypeVec4: return 4;
            case 32 + 2: return 4;
            case 32 + 3: return 9;
            case 32 + 4: return 16;
            default: return 0;
            }
        }

        AccessorView GetAccessorView(const tinygltf::Model& model, const int accessorIndex)
        {
            const auto& accessor = model.accessors.at(static_cast<std::size_t>(accessorIndex));
            if (accessor.sparse.isSparse) throw std::runtime_error("sparse accessors are not supported");
            if (accessor.bufferView < 0)
            {
                throw std::runtime_error("accessor without a buffer view (compressed or zero-filled data)");
            }
            const auto& view = model.bufferViews.at(static_cast<std::size_t>(accessor.bufferView));
            const auto& buffer = model.buffers.at(static_cast<std::size_t>(view.buffer));
            const auto componentSize = ComponentSize(accessor.componentType);
            const int components = ComponentCount(accessor.type);
            const auto stride = static_cast<std::size_t>(std::max(accessor.ByteStride(view), 0));
            if (componentSize == 0 || components <= 0 || stride == 0)
            {
                throw std::runtime_error("accessor with an unsupported component type or stride");
            }
            const auto elementSize = componentSize * static_cast<std::size_t>(components);
            const auto first = static_cast<std::size_t>(view.byteOffset) + static_cast<std::size_t>(accessor.byteOffset);
            if (accessor.count > 0 && first + stride * (accessor.count - 1) + elementSize > buffer.data.size())
            {
                throw std::runtime_error("accessor reads past the end of its buffer");
            }
            return AccessorView{
                buffer.data.data() + first, stride, accessor.count, accessor.componentType, accessor.type,
                accessor.normalized
            };
        }

        float ReadComponent(const AccessorView& view, const std::size_t index, const std::size_t component)
        {
            const auto* ptr = view.Data + view.Stride * index + component * ComponentSize(view.ComponentType);
            const auto scale = [&](const float raw, const float max)
            {
                return view.Normalized ? std::max(raw / max, -1.0f) : raw;
            };
            switch (view.ComponentType)
            {
            case kComponentTypeByte: return scale(static_cast<float>(*reinterpret_cast<const std::int8_t*>(ptr)), 127.0f);
            case kComponentTypeUnsignedByte: return scale(static_cast<float>(*ptr), 255.0f);
            case kComponentTypeShort:
            {
                std::int16_t value;
                std::memcpy(&value, ptr, sizeof(value));
                return scale(static_cast<float>(value), 32767.0f);
            }
            case kComponentTypeUnsignedShort:
            {
                std::uint16_t value;
                std::memcpy(&value, ptr, sizeof(value));
                return scale(static_cast<float>(value), 65535.0f);
            }
            case kComponentTypeUnsignedInt:
            {
                std::uint32_t value;
                std::memcpy(&value, ptr, sizeof(value));
                return static_cast<float>(value);
            }
            default:
            {
                float value;
                std::memcpy(&value, ptr, sizeof(value));
                return value;
            }
            }
        }

        glm::vec3 ReadVec3(const AccessorView& view, const std::size_t index)
        {
            return {ReadComponent(view, index, 0), ReadComponent(view, index, 1), ReadComponent(view, index, 2)};
        }

        glm::vec4 ReadVec4(const AccessorView& view, const std::size_t index)
        {
            return {ReadComponent(view, index, 0), ReadComponent(view, index, 1), ReadComponent(view, index, 2),
                    ReadComponent(view, index, 3)};
        }

        glm::vec2 ReadVec2(const AccessorView& view, const std::size_t index)
        {
            return {ReadComponent(view, index, 0), ReadComponent(view, index, 1)};
        }

        std::uint32_t ReadIndex(const AccessorView& view, const std::size_t index)
        {
            const auto* ptr = view.Data + view.Stride * index;
            switch (view.ComponentType)
            {
            case kComponentTypeUnsignedByte:
                return *ptr;
            case kComponentTypeUnsignedShort:
            {
                std::uint16_t value;
                std::memcpy(&value, ptr, sizeof(value));
                return value;
            }
            case kComponentTypeUnsignedInt:
            {
                std::uint32_t value;
                std::memcpy(&value, ptr, sizeof(value));
                return value;
            }
            default:
                return 0;
            }
        }

        bool IsVec(const AccessorView& view, const int expectedType)
        {
            return view.Type == expectedType && view.ComponentType != kComponentTypeUnsignedInt;
        }

        std::vector<std::uint32_t> ToTriangleList(std::vector<std::uint32_t> indices, const int mode)
        {
            if (mode == kModeTriangles) return indices;
            std::vector<std::uint32_t> out;
            if (indices.size() < 3) return out;
            out.reserve((indices.size() - 2) * 3);
            for (std::size_t i = 2; i < indices.size(); ++i)
            {
                if (mode == kModeTriangleStrip)
                {
                    const bool odd = (i & 1) != 0;
                    out.insert(out.end(), {indices[i - 2], odd ? indices[i] : indices[i - 1], odd ? indices[i - 1] : indices[i]});
                }
                else
                {
                    out.insert(out.end(), {indices[0], indices[i - 1], indices[i]});
                }
            }
            return out;
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

        struct EncodedImages
        {
            std::vector<std::vector<unsigned char>> Bytes;
        };

        bool CaptureEncodedImage(tinygltf::Image*, const int index, std::string*, std::string*, int, int,
                                 const unsigned char* bytes, const int size, void* user)
        {
            auto& store = *static_cast<EncodedImages*>(user);
            if (index < 0 || size <= 0) return true;
            if (store.Bytes.size() <= static_cast<std::size_t>(index)) store.Bytes.resize(static_cast<std::size_t>(index) + 1);
            store.Bytes[static_cast<std::size_t>(index)].assign(bytes, bytes + size);
            return true;
        }

        void DecodeImage(tinygltf::Image& image, const int index, const std::vector<unsigned char>& bytes, std::string& warn)
        {
            int width = 0, height = 0, channels = 0;
            unsigned char* decoded = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height,
                                                           &channels, STBI_rgb_alpha);
            if (!decoded)
            {
                warn += std::format("image {} ('{}') could not be decoded ({}), the material is untextured. ", index,
                                    image.uri, stbi_failure_reason() ? stbi_failure_reason() : "unknown error");
                image.width = 0;
                image.height = 0;
                image.image.clear();
                return;
            }
            image.width = width;
            image.height = height;
            image.component = 4;
            image.bits = 8;
            image.pixel_type = kComponentTypeUnsignedByte;
            image.image.assign(decoded, decoded + (static_cast<std::size_t>(width) * height * 4));
            stbi_image_free(decoded);
        }

        void HalveImage(tinygltf::Image& image)
        {
            const int width = std::max(image.width / 2, 1);
            const int height = std::max(image.height / 2, 1);
            std::vector<unsigned char> out(static_cast<std::size_t>(width) * height * 4);
            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    for (int c = 0; c < 4; ++c)
                    {
                        int sum = 0;
                        for (int dy = 0; dy < 2; ++dy)
                        {
                            for (int dx = 0; dx < 2; ++dx)
                            {
                                const int sx = std::min(x * 2 + dx, image.width - 1);
                                const int sy = std::min(y * 2 + dy, image.height - 1);
                                sum += image.image[(static_cast<std::size_t>(sy) * image.width + sx) * 4 + c];
                            }
                        }
                        out[(static_cast<std::size_t>(y) * width + x) * 4 + c] = static_cast<unsigned char>((sum + 2) / 4);
                    }
                }
            }
            image.width = width;
            image.height = height;
            image.image = std::move(out);
        }

        std::uint64_t MippedBytes(const tinygltf::Image& image)
        {
            return image.image.empty() ? 0 : static_cast<std::uint64_t>(image.image.size()) * 4 / 3;
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

        struct PreparedTexture
        {
            std::shared_ptr<VulkanImage> Image;
            std::unique_ptr<VulkanBuffer> Staging;
            vk::Extent3D Extent{};
            vk::Format Format{};
            std::uint32_t MipLevels = 1;
        };

        PreparedTexture PrepareTexture(const std::shared_ptr<VulkanDevice>& device, const tinygltf::Image& image,
                                       const vk::Format format, const tinygltf::Sampler* sampler, std::string debugName,
                                       const std::shared_ptr<Logger>& logger)
        {
            PreparedTexture prepared;
            if (image.image.empty() || image.width <= 0 || image.height <= 0) return prepared;
            prepared.Extent = vk::Extent3D{static_cast<std::uint32_t>(image.width), static_cast<std::uint32_t>(image.height), 1};
            prepared.Format = format;
            const auto byteSize = static_cast<vk::DeviceSize>(image.image.size());
            prepared.MipLevels = SupportsLinearBlit(device->GetPhysicalDevice(), format) ? MipLevelCount(prepared.Extent) : 1u;

            prepared.Staging = std::make_unique<VulkanBuffer>(device, MakeStagingBufferSpecification(byteSize), logger);
            prepared.Staging->Upload(image.image.data(), byteSize);

            prepared.Image = std::make_shared<VulkanImage>(
                device,
                VulkanImageSpecification{
                    .extent = prepared.Extent,
                    .format = format,
                    .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst |
                             vk::ImageUsageFlagBits::eTransferSrc,
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .mipLevels = prepared.MipLevels,
                    .createSampler = true,
                    .samplerFilter = sampler && sampler->magFilter == 9728 ? vk::Filter::eNearest
                                                                          : vk::Filter::eLinear,
                    .samplerAddressMode = sampler ? ToAddressMode(sampler->wrapS) : vk::SamplerAddressMode::eRepeat,
                    .debugName = std::move(debugName)
                },
                logger);
            return prepared;
        }

        void RecordTextureUpload(const vk::CommandBuffer cmd, const PreparedTexture& texture)
        {
            const auto image = texture.Image->GetImage();
            TransitionImageLayout(cmd, image, texture.Format, vk::ImageLayout::eUndefined,
                                  vk::ImageLayout::eTransferDstOptimal, texture.MipLevels);
            CopyBufferToImage(cmd, texture.Staging->GetBuffer(), image, texture.Extent);
            if (texture.MipLevels > 1)
            {
                GenerateMipmaps(cmd, image, texture.Extent, texture.MipLevels);
            }
            else
            {
                TransitionImageLayout(cmd, image, texture.Format, vk::ImageLayout::eTransferDstOptimal,
                                      vk::ImageLayout::eShaderReadOnlyOptimal);
            }
        }

        // Halves images the device cannot hold (dimension limit) and the largest ones while the scene's textures would
        // take more than a quarter of the largest device-local heap. Returns the resulting mipped byte total.
        std::uint64_t ConstrainTextures(tinygltf::Model& model, const std::shared_ptr<VulkanDevice>& device,
                                        const std::shared_ptr<Logger>& logger, const std::string& name)
        {
            const auto physical = device->GetPhysicalDevice();
            const auto maxDimension = static_cast<int>(physical.getProperties().limits.maxImageDimension2D);
            std::uint64_t heap = 0;
            const auto memory = physical.getMemoryProperties();
            for (std::uint32_t i = 0; i < memory.memoryHeapCount; ++i)
            {
                if (memory.memoryHeaps[i].flags & vk::MemoryHeapFlagBits::eDeviceLocal) heap = std::max<std::uint64_t>(heap, memory.memoryHeaps[i].size);
            }
            const std::uint64_t budget = std::max<std::uint64_t>(heap / 4, 256ull << 20);

            std::set<int> used;
            for (const auto& texture : model.textures)
            {
                if (texture.source >= 0 && static_cast<std::size_t>(texture.source) < model.images.size()) used.insert(texture.source);
            }
            std::map<int, std::pair<int, int>> original;
            const auto halve = [&](const int index)
            {
                auto& image = model.images[static_cast<std::size_t>(index)];
                original.try_emplace(index, std::pair{image.width, image.height});
                HalveImage(image);
            };
            std::uint64_t total = 0;
            for (const int index : used)
            {
                auto& image = model.images[static_cast<std::size_t>(index)];
                while (!image.image.empty() && (image.width > maxDimension || image.height > maxDimension)) halve(index);
                total += MippedBytes(image);
            }
            while (total > budget)
            {
                int largest = -1;
                std::uint64_t largestBytes = 0;
                for (const int index : used)
                {
                    const auto bytes = MippedBytes(model.images[static_cast<std::size_t>(index)]);
                    if (bytes > largestBytes) { largestBytes = bytes; largest = index; }
                }
                if (largest < 0 || model.images[static_cast<std::size_t>(largest)].width <= 1) break;
                total -= largestBytes;
                halve(largest);
                total += MippedBytes(model.images[static_cast<std::size_t>(largest)]);
            }
            if (logger)
            {
                for (const auto& [index, size] : original)
                {
                    const auto& image = model.images[static_cast<std::size_t>(index)];
                    logger->Warn("glTF '{}': texture {} reduced from {}x{} to {}x{} (device limit {}, texture budget {} MiB).", name,
                                 index, size.first, size.second, image.width, image.height, maxDimension, budget >> 20);
                }
            }
            return total;
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

        class GeometryBatch
        {
        public:
            explicit GeometryBatch(const GltfUpload& upload) : m_Upload(upload) {}

            void Add(GltfPrimitive& primitive, const std::vector<GltfVertex>& vertices,
                     const std::vector<std::uint32_t>& indices, const std::vector<GltfVertexExtra>& extras)
            {
                vk::BufferUsageFlags traceable{};
                if (m_Upload.Device->GetCapabilities().AccelerationStructure)
                {
                    traceable = vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eStorageBuffer
                                | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR;
                }
                Item item;
                item.VertexBytes = static_cast<vk::DeviceSize>(vertices.size() * sizeof(GltfVertex));
                item.IndexBytes = static_cast<vk::DeviceSize>(indices.size() * sizeof(std::uint32_t));
                item.ExtraBytes = static_cast<vk::DeviceSize>(extras.size() * sizeof(GltfVertexExtra));
                auto vertexSpec = MakeVertexBufferSpecification(item.VertexBytes);
                vertexSpec.usage |= traceable | vk::BufferUsageFlagBits::eTransferSrc;
                auto indexSpec = MakeIndexBufferSpecification(item.IndexBytes);
                indexSpec.usage |= traceable;
                auto extraSpec = MakeVertexBufferSpecification(item.ExtraBytes);
                extraSpec.usage |= vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eStorageBuffer |
                                   vk::BufferUsageFlagBits::eTransferSrc;
                primitive.VertexBuffer.Create(m_Upload.Device, vertexSpec);
                primitive.IndexBuffer.Create(m_Upload.Device, indexSpec);
                primitive.ExtraBuffer.Create(m_Upload.Device, extraSpec);
                item.Vertex = primitive.VertexBuffer.GetBuffer();
                item.Index = primitive.IndexBuffer.GetBuffer();
                item.Extra = primitive.ExtraBuffer.GetBuffer();

                const auto total = item.VertexBytes + item.IndexBytes + item.ExtraBytes;
                item.Staging = std::make_unique<VulkanBuffer>(m_Upload.Device, MakeStagingBufferSpecification(total),
                                                              m_Upload.Log);
                item.Staging->Upload(vertices.data(), item.VertexBytes);
                item.Staging->Upload(indices.data(), item.IndexBytes, item.VertexBytes);
                item.Staging->Upload(extras.data(), item.ExtraBytes, item.VertexBytes + item.IndexBytes);
                m_Bytes += total;
                m_Items.push_back(std::move(item));
                if (m_Bytes >= kFlushBytes) Flush();
            }

            void Flush()
            {
                if (m_Items.empty()) return;
                ImmediateSubmit(m_Upload.Pool, m_Upload.Queue, [&](const vk::CommandBuffer cmd)
                {
                    for (const auto& item : m_Items)
                    {
                        const auto staging = item.Staging->GetBuffer();
                        CopyBuffer(cmd, staging, item.Vertex, item.VertexBytes);
                        CopyBuffer(cmd, staging, item.Index, item.IndexBytes, item.VertexBytes);
                        CopyBuffer(cmd, staging, item.Extra, item.ExtraBytes, item.VertexBytes + item.IndexBytes);
                    }
                });
                m_Items.clear();
                m_Bytes = 0;
            }

        private:
            struct Item
            {
                std::unique_ptr<VulkanBuffer> Staging;
                vk::Buffer Vertex, Index, Extra;
                vk::DeviceSize VertexBytes = 0, IndexBytes = 0, ExtraBytes = 0;
            };

            static constexpr vk::DeviceSize kFlushBytes = 128ull << 20;
            const GltfUpload& m_Upload;
            std::vector<Item> m_Items;
            vk::DeviceSize m_Bytes = 0;
        };

        struct PrimitiveJob
        {
            const tinygltf::Primitive* Primitive = nullptr;
            glm::mat4 World{1.0f};
            glm::mat3 Normal{1.0f};
            std::size_t Mesh = 0;
        };

        struct MeshJob
        {
            std::string Name;
        };

        struct PreparedPrimitive
        {
            std::vector<GltfVertex> Vertices;
            std::vector<std::uint32_t> Indices;
            std::vector<GltfVertexExtra> Extras;
            int Material = -1;
            bool HasTangents = false;
            bool HasTexCoord1 = false;
        };

        void CollectNode(const tinygltf::Model& model, const int nodeIndex, const glm::mat4& parentTransform,
                         std::vector<MeshJob>& meshes, std::vector<PrimitiveJob>& primitives)
        {
            if (nodeIndex < 0 || static_cast<std::size_t>(nodeIndex) >= model.nodes.size()) return;
            const auto& node = model.nodes[static_cast<std::size_t>(nodeIndex)];
            const glm::mat4 worldTransform = parentTransform * LocalNodeTransform(node);

            if (node.mesh >= 0 && static_cast<std::size_t>(node.mesh) < model.meshes.size())
            {
                const auto& mesh = model.meshes[static_cast<std::size_t>(node.mesh)];
                const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(worldTransform)));
                meshes.push_back(MeshJob{mesh.name});
                for (const auto& primitive : mesh.primitives)
                {
                    primitives.push_back(PrimitiveJob{&primitive, worldTransform, normalMatrix, meshes.size() - 1});
                }
            }

            for (const auto child : node.children)
            {
                CollectNode(model, child, worldTransform, meshes, primitives);
            }
        }


        // CPU half of a primitive: accessors to world-space vertices, indices, tangents. Pure, runs on any thread
        std::optional<PreparedPrimitive> PreparePrimitive(const tinygltf::Model& model, const tinygltf::Primitive& primitive,
                                                          const glm::mat4& worldTransform, const glm::mat3& normalMatrix,
                                                          const std::vector<GltfMaterial>& materials,
                                                          const std::filesystem::path& resolvedPath,
                                                          const std::shared_ptr<Logger>& logger)
        {
            const auto positionIt = primitive.attributes.find("POSITION");
            if (positionIt == primitive.attributes.end())
            {
                if (logger) logger->Warn("glTF '{}': skipping primitive with no POSITION.", resolvedPath.string());
                return std::nullopt;
            }
            if (primitive.mode != kModeTriangles && primitive.mode != kModeTriangleStrip && primitive.mode != kModeTriangleFan)
            {
                if (logger)
                {
                    logger->Warn("glTF '{}': skipping primitive with unsupported mode {} (only triangles are drawn).",
                                 resolvedPath.string(), primitive.mode);
                }
                return std::nullopt;
            }

            const auto positionView = GetAccessorView(model, positionIt->second);
            if (!IsVec(positionView, kTypeVec3))
            {
                if (logger)
                {
                    logger->Warn("glTF '{}': skipping primitive with an unsupported POSITION accessor.",
                                resolvedPath.string());
                }
                return std::nullopt;
            }

            std::optional<AccessorView> normalView;
            if (const auto it = primitive.attributes.find("NORMAL"); it != primitive.attributes.end())
            {
                if (auto view = GetAccessorView(model, it->second); IsVec(view, kTypeVec3))
                {
                    normalView = view;
                }
            }
            const auto optionalView = [&](const char* name, const int type) -> std::optional<AccessorView>
            {
                if (const auto it = primitive.attributes.find(name); it != primitive.attributes.end())
                {
                    if (auto view = GetAccessorView(model, it->second); IsVec(view, type)) return view;
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

            std::vector<std::uint32_t> indices;
            if (primitive.indices >= 0)
            {
                const auto indexView = GetAccessorView(model, primitive.indices);
                if (indexView.Type != kTypeScalar || indexView.ComponentType == kComponentTypeFloat)
                {
                    if (logger) logger->Warn("glTF '{}': skipping primitive with unsupported indices.", resolvedPath.string());
                    return std::nullopt;
                }
                indices.resize(indexView.Count);
                for (std::size_t i = 0; i < indexView.Count; ++i)
                {
                    indices[i] = ReadIndex(indexView, i);
                }
            }
            else
            {
                indices.resize(positionView.Count);
                std::iota(indices.begin(), indices.end(), 0u);
            }
            indices = ToTriangleList(std::move(indices), primitive.mode);
            indices.resize(indices.size() / 3 * 3);
            if (std::ranges::any_of(indices, [&](const std::uint32_t index) { return index >= vertices.size(); }))
            {
                if (logger) logger->Warn("glTF '{}': skipping primitive with out-of-range indices.", resolvedPath.string());
                return std::nullopt;
            }

            if (vertices.empty() || indices.empty()) return std::nullopt;

            if (!tangentView)
            {
                int normalSet = 0;
                if (primitive.material >= 0 && static_cast<std::size_t>(primitive.material) < materials.size())
                {
                    normalSet = materials[static_cast<std::size_t>(primitive.material)]
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

            PreparedPrimitive prepared;
            prepared.Vertices = std::move(vertices);
            prepared.Indices = std::move(indices);
            prepared.Extras = std::move(extras);
            prepared.Material = primitive.material;
            prepared.HasTangents = tangentView.has_value();
            prepared.HasTexCoord1 = texCoord1View.has_value();
            return prepared;
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

    GltfExtensionReport AuditGltfExtensions(const std::vector<std::string>& used, const std::vector<std::string>& required)
    {
        constexpr std::array supported{"KHR_materials_emissive_strength", "KHR_materials_ior", "KHR_materials_specular",
                                       "KHR_texture_transform", "KHR_mesh_quantization"};
        constexpr std::array approximated{"KHR_materials_pbrSpecularGlossiness", "KHR_materials_transmission"};
        const auto has = [](const auto& list, const std::string& name)
        {
            return std::ranges::any_of(list, [&](const char* entry) { return name == entry; });
        };
        GltfExtensionReport report;
        for (const auto& name : used)
        {
            if (has(supported, name)) continue;
            (has(approximated, name) ? report.Approximated : report.Unsupported).push_back(name);
        }
        for (const auto& name : required)
        {
            if (!has(supported, name) && !has(approximated, name)) report.RequiredUnsupported.push_back(name);
        }
        return report;
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
        if (const auto it = extensions.find("KHR_materials_pbrSpecularGlossiness"); it != extensions.end())
        {
            // Factors and the diffuse texture only: dielectric, roughness = 1 - glossiness, specular scales F0
            const auto& specGloss = it->second;
            std::array<float, 4> diffuse{};
            if (specGloss.Has("diffuseFactor") && ReadNumbers(specGloss.Get("diffuseFactor"), diffuse))
            {
                out.BaseColorFactor = {diffuse[0], diffuse[1], diffuse[2], diffuse[3]};
            }
            out.MetallicFactor = 0.0f;
            out.RoughnessFactor = 1.0f - std::clamp(static_cast<float>(NumberOr(specGloss.Get("glossinessFactor"), 1.0)), 0.0f, 1.0f);
            std::array<float, 3> specular{1.0f, 1.0f, 1.0f};
            if (specGloss.Has("specularFactor")) ReadNumbers(specGloss.Get("specularFactor"), specular);
            constexpr float kDielectricF0 = 0.04f;
            out.SpecularFactor = 1.0f;
            out.SpecularColorFactor = {std::min(specular[0] / kDielectricF0, 1.0f), std::min(specular[1] / kDielectricF0, 1.0f),
                                       std::min(specular[2] / kDielectricF0, 1.0f)};
            if (!out.Texture(GltfTextureRole::BaseColor).Image && textureIndices[static_cast<std::size_t>(GltfTextureRole::BaseColor)] < 0)
            {
                BindFromValue(out, textureIndices, GltfTextureRole::BaseColor, specGloss.Get("diffuseTexture"));
            }
        }
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

        const auto started = std::chrono::steady_clock::now();
        const auto resolvedPath = fileSystem->ResolvePath(path);

        tinygltf::TinyGLTF loader;
        EncodedImages encoded;
        loader.SetImageLoader(CaptureEncodedImage, &encoded);

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

        const auto audit = AuditGltfExtensions(model.extensionsUsed, model.extensionsRequired);
        if (logger)
        {
            const auto join = [](const std::vector<std::string>& names)
            {
                std::string text;
                for (const auto& entry : names) text += (text.empty() ? "" : ", ") + entry;
                return text;
            };
            if (!audit.Approximated.empty())
            {
                logger->Warn("glTF '{}': extensions approximated: {}.", resolvedPath.string(), join(audit.Approximated));
            }
            if (!audit.Unsupported.empty())
            {
                logger->Warn("glTF '{}': extensions not supported and ignored: {}.", resolvedPath.string(), join(audit.Unsupported));
            }
            if (!audit.RequiredUnsupported.empty())
            {
                logger->Warn("glTF '{}': requires unsupported extensions ({}), the model may be incomplete.",
                             resolvedPath.string(), join(audit.RequiredUnsupported));
            }
        }
        {
            std::set<int> usedSet;
            for (const auto& texture : model.textures)
            {
                if (texture.source >= 0 && static_cast<std::size_t>(texture.source) < model.images.size()
                    && static_cast<std::size_t>(texture.source) < encoded.Bytes.size()
                    && !encoded.Bytes[static_cast<std::size_t>(texture.source)].empty())
                {
                    usedSet.insert(texture.source);
                }
            }
            const std::vector<int> used(usedSet.begin(), usedSet.end());
            std::vector<std::string> decodeWarnings(used.size());
            ParallelFor(used.size(), kDecodeWorkers, [&](const std::size_t i)
            {
                const auto index = static_cast<std::size_t>(used[i]);
                DecodeImage(model.images[index], used[i], encoded.Bytes[index], decodeWarnings[i]);
                encoded.Bytes[index] = {};
            });
            std::string decodeWarn;
            for (const auto& entry : decodeWarnings) decodeWarn += entry;
            if (!decodeWarn.empty() && logger) logger->Warn("glTF '{}': {}", resolvedPath.string(), decodeWarn);
        }
        const auto parsed = std::chrono::steady_clock::now();
        const auto textureBytes = ConstrainTextures(model, device, logger, resolvedPath.string());
        const auto constrained = std::chrono::steady_clock::now();

        GltfSceneData scene;
        scene.Sources = ExternalSources(model, resolvedPath.parent_path());
        VulkanCommandPool uploadPool(device, logger, device->GetQueueIndices().Graphics);
        const auto queue = device->GetGraphicsQueue();

        struct TextureJob
        {
            int Source = 0;
            const tinygltf::Sampler* Sampler = nullptr;
            vk::Format Format{};
            std::string Name;
        };
        std::vector<std::pair<GltfMaterial, GltfTextureIndices>> materialRecords;
        std::vector<std::array<int, kGltfTextureRoleCount>> materialJobs;
        std::map<TextureKey, int> jobOf;
        std::vector<TextureJob> jobs;
        for (const auto& material : model.materials)
        {
            GltfTextureIndices indices;
            auto out = ReadGltfMaterial(material, indices);
            std::array<int, kGltfTextureRoleCount> roleJobs;
            roleJobs.fill(-1);
            for (std::size_t role = 0; role < kGltfTextureRoleCount; ++role)
            {
                const int textureIndex = indices[role];
                if (textureIndex < 0 || static_cast<std::size_t>(textureIndex) >= model.textures.size()) continue;
                const auto& texture = model.textures[static_cast<std::size_t>(textureIndex)];
                if (texture.source < 0 || static_cast<std::size_t>(texture.source) >= model.images.size()) continue;
                const auto textureRole = static_cast<GltfTextureRole>(role);
                const TextureKey key{texture.source, texture.sampler, GltfTextureRoleIsSrgb(textureRole)};
                auto [it, inserted] = jobOf.try_emplace(key, static_cast<int>(jobs.size()));
                if (inserted)
                {
                    const auto& image = model.images[static_cast<std::size_t>(texture.source)];
                    jobs.push_back(TextureJob{
                        texture.source,
                        texture.sampler >= 0 && static_cast<std::size_t>(texture.sampler) < model.samplers.size()
                            ? &model.samplers[static_cast<std::size_t>(texture.sampler)]
                            : nullptr,
                        GltfTextureFormat(textureRole), image.name.empty() ? image.uri : image.name});
                }
                roleJobs[role] = it->second;
            }
            materialRecords.emplace_back(std::move(out), indices);
            materialJobs.push_back(roleJobs);
        }

        constexpr std::uint64_t kStagingChunkBytes = 192ull << 20;
        std::vector<std::shared_ptr<VulkanImage>> jobImages(jobs.size());
        for (std::size_t first = 0; first < jobs.size();)
        {
            std::size_t last = first;
            std::uint64_t bytes = 0;
            while (last < jobs.size())
            {
                const auto size = static_cast<std::uint64_t>(model.images[static_cast<std::size_t>(jobs[last].Source)].image.size());
                if (last > first && bytes + size > kStagingChunkBytes) break;
                bytes += size;
                ++last;
            }
            std::vector<PreparedTexture> prepared(last - first);
            ParallelFor(prepared.size(), kDecodeWorkers, [&](const std::size_t i)
            {
                const auto& job = jobs[first + i];
                prepared[i] = PrepareTexture(device, model.images[static_cast<std::size_t>(job.Source)], job.Format, job.Sampler,
                                             job.Name, logger);
            });
            if (std::ranges::any_of(prepared, [](const PreparedTexture& texture) { return texture.Image != nullptr; }))
            {
                ImmediateSubmit(uploadPool, queue, [&](const vk::CommandBuffer cmd)
                {
                    for (const auto& texture : prepared)
                    {
                        if (texture.Image) RecordTextureUpload(cmd, texture);
                    }
                });
            }
            for (std::size_t i = 0; i < prepared.size(); ++i)
            {
                jobImages[first + i] = prepared[i].Image;
                if (prepared[i].Image) scene.Textures.push_back(prepared[i].Image);
            }
            first = last;
        }
        for (auto& image : model.images) std::vector<unsigned char>().swap(image.image);

        scene.Materials.reserve(materialRecords.size());
        for (std::size_t m = 0; m < materialRecords.size(); ++m)
        {
            auto& out = materialRecords[m].first;
            for (std::size_t role = 0; role < kGltfTextureRoleCount; ++role)
            {
                const int job = materialJobs[m][role];
                out.Textures[role].Image = job >= 0 ? jobImages[static_cast<std::size_t>(job)] : nullptr;
            }
            out.BaseColorTexture = out.Texture(GltfTextureRole::BaseColor).Image;
            scene.Materials.push_back(std::move(out));
        }

        const auto texturesDone = std::chrono::steady_clock::now();
        scene.Meshes.reserve(model.meshes.size());
        const GltfUpload upload{device, uploadPool, queue, logger};
        std::vector<MeshJob> meshJobs;
        std::vector<PrimitiveJob> primitiveJobs;
        if (!model.scenes.empty())
        {
            const auto sceneIndex = static_cast<std::size_t>(model.defaultScene >= 0 ? model.defaultScene : 0);
            const auto& rootNodes = model.scenes.at(sceneIndex).nodes;
            for (const auto rootNode : rootNodes)
            {
                CollectNode(model, rootNode, glm::mat4(1.0f), meshJobs, primitiveJobs);
            }
        }
        else
        {
            // No scene graph at all (rare/malformed files) -- fall back to treating every node as
            // its own root so the mesh data isn't simply dropped.
            for (std::size_t i = 0; i < model.nodes.size(); ++i)
            {
                CollectNode(model, static_cast<int>(i), glm::mat4(1.0f), meshJobs, primitiveJobs);
            }
        }

        std::vector<std::optional<PreparedPrimitive>> prepared(primitiveJobs.size());
        ParallelFor(primitiveJobs.size(), kDecodeWorkers, [&](const std::size_t i)
        {
            const auto& job = primitiveJobs[i];
            try
            {
                prepared[i] = PreparePrimitive(model, *job.Primitive, job.World, job.Normal, scene.Materials, resolvedPath,
                                               logger);
            }
            catch (const std::out_of_range& error)
            {
                if (logger) logger->Warn("glTF '{}': skipping primitive with an invalid accessor ({}).", resolvedPath.string(),
                                         error.what());
            }
            catch (const std::runtime_error& error)
            {
                if (logger) logger->Warn("glTF '{}': skipping primitive: {}.", resolvedPath.string(), error.what());
            }
        });

        GeometryBatch batch(upload);
        std::size_t nextPrimitive = 0;
        for (std::size_t meshIndex = 0; meshIndex < meshJobs.size(); ++meshIndex)
        {
            GltfMesh outMesh;
            outMesh.Name = meshJobs[meshIndex].Name;
            for (; nextPrimitive < primitiveJobs.size() && primitiveJobs[nextPrimitive].Mesh == meshIndex; ++nextPrimitive)
            {
                auto& data = prepared[nextPrimitive];
                if (!data) continue;
                GltfPrimitive outPrimitive;
                batch.Add(outPrimitive, data->Vertices, data->Indices, data->Extras);
                outPrimitive.VertexCount = static_cast<std::uint32_t>(data->Vertices.size());
                outPrimitive.IndexCount = static_cast<std::uint32_t>(data->Indices.size());
                outPrimitive.MaterialIndex = data->Material;
                outPrimitive.HasAuthoredTangents = data->HasTangents;
                outPrimitive.HasTexCoord1 = data->HasTexCoord1;

                const auto baseVertex = static_cast<std::uint32_t>(scene.Geometry.Vertices.size());
                scene.Geometry.Vertices.reserve(scene.Geometry.Vertices.size() + data->Vertices.size());
                for (const auto& vertex : data->Vertices) scene.Geometry.Vertices.push_back(vertex.Position);
                scene.Geometry.Indices.reserve(scene.Geometry.Indices.size() + data->Indices.size());
                for (const auto index : data->Indices) scene.Geometry.Indices.push_back(baseVertex + index);

                outMesh.Primitives.push_back(std::move(outPrimitive));
                data.reset();
            }
            if (!outMesh.Primitives.empty()) scene.Meshes.push_back(std::move(outMesh));
        }
        batch.Flush();

        if (logger)
        {
            const auto ms = [](const auto a, const auto b)
            {
                return std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count();
            };
            const auto finished = std::chrono::steady_clock::now();
            logger->Debug("glTF '{}' timing: parse+decode {} ms, constrain {} ms, textures {} ms, geometry {} ms",
                          resolvedPath.string(), ms(started, parsed), ms(parsed, constrained), ms(constrained, texturesDone),
                          ms(texturesDone, finished));
            logger->Info("Loaded glTF '{}': {} mesh(es), {} material(s), {} triangle(s), {} texture(s) {} MiB, {} ms",
                         resolvedPath.string(), scene.Meshes.size(), scene.Materials.size(),
                         scene.Geometry.Indices.size() / 3, scene.Textures.size(), textureBytes >> 20,
                         std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());
        }

        return scene;
    }
}
