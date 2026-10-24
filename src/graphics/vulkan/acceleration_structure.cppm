module;
#include <vk_mem_alloc.h>
export module GPP.Graphics:Vulkan.AccelerationStructure;

import std;
import glm;
import vulkan;
import GPP.Core;
import :Vulkan.Device;
import :Vulkan.Buffer;
import :Vulkan.Command;

namespace GPP
{
    export struct BlasTriangles
    {
        vk::DeviceAddress VertexAddress = 0;
        vk::DeviceSize VertexStride = 0;
        std::uint32_t VertexCount = 0;
        vk::Format VertexFormat = vk::Format::eR32G32B32Sfloat;
        vk::DeviceAddress IndexAddress = 0;
        std::uint32_t IndexCount = 0;
    };

    export struct TlasInstance
    {
        glm::mat4 Transform{1.0f};
        std::uint32_t CustomIndex = 0;
        std::uint8_t Mask = 0xFF;
        vk::GeometryInstanceFlagsKHR Flags = vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable;
        vk::DeviceAddress Blas = 0;
    };

    export [[nodiscard]] vk::TransformMatrixKHR ToTransformMatrix(const glm::mat4& transform);
    export [[nodiscard]] vk::AccelerationStructureInstanceKHR PackTlasInstance(const TlasInstance& instance);

    export class AccelerationStructure
    {
    public:
        AccelerationStructure() = default;
        ~AccelerationStructure();
        AccelerationStructure(const AccelerationStructure&) = delete;
        AccelerationStructure& operator=(const AccelerationStructure&) = delete;
        AccelerationStructure(AccelerationStructure&& other) noexcept;
        AccelerationStructure& operator=(AccelerationStructure&& other) noexcept;

        void Create(const std::shared_ptr<VulkanDevice>& device, vk::AccelerationStructureTypeKHR type,
                    vk::DeviceSize size);
        void Destroy();

        [[nodiscard]] vk::AccelerationStructureKHR GetHandle() const noexcept { return m_Handle; }
        [[nodiscard]] vk::DeviceAddress GetDeviceAddress() const noexcept { return m_Address; }
        [[nodiscard]] vk::DeviceSize GetSize() const noexcept { return m_Buffer.GetSize(); }
        [[nodiscard]] const std::shared_ptr<VulkanDevice>& GetDevice() const noexcept { return m_Device; }

    private:
        std::shared_ptr<VulkanDevice> m_Device;
        VulkanBuffer m_Buffer;
        vk::AccelerationStructureKHR m_Handle{};
        vk::DeviceAddress m_Address = 0;
    };

    export class Blas
    {
    public:
        Blas(const std::shared_ptr<VulkanDevice>& device, const VulkanCommandPool& pool, vk::Queue queue,
             const BlasTriangles& triangles);
        Blas(const std::shared_ptr<VulkanDevice>& device, const BlasTriangles& triangles);

        void RecordBuild(vk::CommandBuffer commandBuffer, vk::DeviceAddress scratch) const;
        void RecordCompact(vk::CommandBuffer commandBuffer, vk::DeviceSize compactedSize);
        void FinishCompact();

        [[nodiscard]] vk::DeviceSize ScratchSize() const noexcept { return m_ScratchSize; }
        [[nodiscard]] vk::DeviceSize GetSize() const noexcept { return m_Structure.GetSize(); }
        [[nodiscard]] vk::AccelerationStructureKHR GetHandle() const noexcept { return m_Structure.GetHandle(); }
        [[nodiscard]] vk::DeviceAddress GetDeviceAddress() const noexcept { return m_Structure.GetDeviceAddress(); }

    private:
        BlasTriangles m_Triangles;
        vk::DeviceSize m_ScratchSize = 0;
        AccelerationStructure m_Structure;
        AccelerationStructure m_Compact;
    };

    export void BuildBlasBatch(const std::shared_ptr<VulkanDevice>& device, const VulkanCommandPool& pool,
                               vk::Queue queue, std::span<Blas* const> blases);

    export class Tlas
    {
    public:
        explicit Tlas(std::shared_ptr<VulkanDevice> device);

        void SetInstances(std::span<const TlasInstance> instances);
        void RecordBuild(vk::CommandBuffer commandBuffer);
        [[nodiscard]] bool WillRefit() const noexcept { return m_Refit; }
        [[nodiscard]] std::uint32_t RefitCount() const noexcept { return m_Refits; }
        static constexpr std::uint32_t kMaxRefits = 1024;

        [[nodiscard]] vk::AccelerationStructureKHR GetHandle() const noexcept { return m_Structure.GetHandle(); }
        [[nodiscard]] std::uint32_t InstanceCount() const noexcept { return m_Count; }

    private:
        void Reserve(std::uint32_t capacity);
        [[nodiscard]] vk::AccelerationStructureGeometryKHR Geometry() const;

        struct InstanceKey
        {
            std::uint32_t CustomIndex = 0;
            std::uint32_t MaskAndFlags = 0;
            vk::DeviceAddress Blas = 0;
            bool operator==(const InstanceKey&) const = default;
        };

        std::shared_ptr<VulkanDevice> m_Device;
        VulkanBuffer m_Instances;
        VulkanBuffer m_Scratch;
        std::vector<vk::AccelerationStructureInstanceKHR> m_Packed;
        std::vector<InstanceKey> m_Keys;
        std::vector<InstanceKey> m_BuiltKeys;
        bool m_Built = false;
        bool m_Refit = false;
        std::uint32_t m_Refits = 0;
        vk::DeviceAddress m_ScratchAddress = 0;
        AccelerationStructure m_Structure;
        std::uint32_t m_Capacity = 0;
        std::uint32_t m_Count = 0;
    };
}
