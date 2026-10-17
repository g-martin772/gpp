module GPP.Graphics;

import std;
import vulkan;
import :Vulkan.GpuProfiler;

namespace GPP
{
    GpuProfiler::GpuProfiler(std::shared_ptr<VulkanDevice> device, const std::uint32_t slotCount,
                             const std::uint32_t segmentCapacity)
        : m_Device(std::move(device)),
          m_Ledger(slotCount, segmentCapacity, m_Device ? m_Device->GetCapabilities().TimestampValidBits : 0,
                   m_Device ? m_Device->GetCapabilities().TimestampPeriodNs : 0.0)
    {
        if (!m_Ledger.Enabled()) return;
        vk::QueryPoolCreateInfo info{};
        info.queryType = vk::QueryType::eTimestamp;
        info.queryCount = segmentCapacity * 2;
        for (std::uint32_t i = 0; i < m_Ledger.SlotCount(); ++i)
            m_Pools.push_back(m_Device->GetDevice().createQueryPool(info));
    }

    GpuProfiler::~GpuProfiler()
    {
        for (const auto pool : m_Pools) m_Device->GetDevice().destroyQueryPool(pool);
    }

    void GpuProfiler::BeginFrame(const vk::CommandBuffer cmd, const std::uint32_t slot)
    {
        if (!Enabled()) return;
        m_Slot = slot % m_Ledger.SlotCount();
        m_Scope.clear();
        m_ScopeOpen = false;
        m_Segment.reset();
        m_Ledger.BeginFrame(m_Slot);
        cmd.resetQueryPool(m_Pools[m_Slot], 0, m_Ledger.QueriesCapacity());
    }

    void GpuProfiler::WriteBegin(const vk::CommandBuffer cmd)
    {
        m_Segment = m_Ledger.OpenSegment(m_Slot, m_Scope);
        if (m_Segment) cmd.writeTimestamp(vk::PipelineStageFlagBits::eAllCommands, m_Pools[m_Slot], *m_Segment);
    }

    void GpuProfiler::WriteEnd(const vk::CommandBuffer cmd)
    {
        if (!m_Segment) return;
        cmd.writeTimestamp(vk::PipelineStageFlagBits::eAllCommands, m_Pools[m_Slot], *m_Segment + 1);
        m_Ledger.CloseSegment(m_Slot, *m_Segment);
        m_Segment.reset();
    }

    void GpuProfiler::BeginScope(const vk::CommandBuffer cmd, const std::string_view name)
    {
        if (!Enabled()) return;
        if (m_ScopeOpen) EndScope(cmd);
        m_Scope = name;
        m_ScopeOpen = true;
        WriteBegin(cmd);
    }

    void GpuProfiler::EndScope(const vk::CommandBuffer cmd)
    {
        if (!Enabled() || !m_ScopeOpen) return;
        WriteEnd(cmd);
        m_ScopeOpen = false;
    }

    void GpuProfiler::Suspend(const vk::CommandBuffer cmd)
    {
        if (Enabled() && m_ScopeOpen) WriteEnd(cmd);
    }

    void GpuProfiler::Resume(const vk::CommandBuffer cmd)
    {
        if (Enabled() && m_ScopeOpen && !m_Segment) WriteBegin(cmd);
    }

    bool GpuProfiler::Collect(const std::uint32_t slot)
    {
        if (!Enabled()) return false;
        const auto index = slot % m_Ledger.SlotCount();
        const auto used = m_Ledger.QueriesUsed(index);
        if (used == 0) return false;
        std::vector<GpuTimestampSample> samples(used);
        const auto result = m_Device->GetDevice().getQueryPoolResults(
            m_Pools[index], 0, used, samples.size() * sizeof(GpuTimestampSample), samples.data(),
            sizeof(GpuTimestampSample), vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWithAvailability);
        if (result != vk::Result::eSuccess && result != vk::Result::eNotReady) return false;
        return m_Ledger.Resolve(index, samples);
    }
}
