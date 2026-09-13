module;

#include <vulkan/vulkan.hpp>

module GPP.Graphics;

import std;
import GPP.Core;
import :Shader;
import :HotReload;
import :Vulkan.Pipeline;

namespace GPP
{
    HotReloadablePipeline::HotReloadablePipeline(
        std::shared_ptr<VulkanDevice> device, const vk::Format colorFormat,
        const vk::Format depthFormat, ShaderPipelineDescription description,
        std::shared_ptr<IFileSystem> fileSystem, std::shared_ptr<EventDispatcher> dispatcher,
        std::shared_ptr<Logger> logger)
        : m_Device(std::move(device)),
          m_ColorFormat(colorFormat),
          m_DepthFormat(depthFormat),
          m_Description(std::move(description)),
          m_Compiler(fileSystem, logger),
          m_Logger(std::move(logger)),
          m_Dispatcher(std::move(dispatcher)),
          m_Watcher(fileSystem, m_Dispatcher, m_Logger)
    {
        if (!m_Device || !m_Dispatcher)
        {
            throw std::invalid_argument("HotReloadablePipeline requires a device and dispatcher.");
        }
        m_VertexPath = fileSystem->ResolvePath(m_Description.vertex.path);
        m_FragmentPath = fileSystem->ResolvePath(m_Description.fragment.path);
        m_Watcher.Watch(m_VertexPath);
        m_Watcher.Watch(m_FragmentPath);
    }

    HotReloadablePipeline::~HotReloadablePipeline()
    {
        try
        {
            StopAsync().get();
        }
        catch (...)
        {
        }
    }

    Task<void> HotReloadablePipeline::StartAsync(std::stop_token stopToken)
    {
        {
            std::scoped_lock lock(m_Mutex);
            if (m_Running)
            {
                co_return;
            }
            if (stopToken.stop_requested())
            {
                co_return;
            }
            m_StopSource = std::stop_source{};
            m_Running = true;
        }

        try
        {
            if (!(co_await ReloadAsync()))
            {
                throw std::runtime_error(LastError());
            }
            m_FileSubscription = m_Dispatcher->Subscribe<ShaderFileChangedEvent>(
                [this](const ShaderFileChangedEvent& event) { HandleFileChanged(event); },
                EventDelivery::Async, EventTarget::Render);
            co_await m_Watcher.StartAsync(m_Description.pollingInterval,
                                          m_StopSource.get_token());
        }
        catch (...)
        {
            const auto exception = std::current_exception();
            std::scoped_lock lock(m_Mutex);
            m_LastError = "Shader pipeline initialization failed.";
            try
            {
                std::rethrow_exception(exception);
            }
            catch (const std::exception& error)
            {
                m_LastError = error.what();
            }
            m_Running = false;
            throw;
        }
        co_return;
    }

    Task<void> HotReloadablePipeline::StopAsync()
    {
        m_FileSubscription.Reset();
        {
            std::scoped_lock lock(m_Mutex);
            m_Running = false;
            m_StopSource.request_stop();
        }
        co_await m_Watcher.StopAsync();
        std::unique_lock lock(m_Mutex);
        m_ReloadCondition.wait(lock, [this] { return !m_Reloading; });
        co_return;
    }

    bool HotReloadablePipeline::Reload()
    {
        try
        {
            CompiledShader vertex = m_Compiler.Compile(
                m_Description.vertex, m_Description.compileOptions);
            CompiledShader fragment = m_Compiler.Compile(
                m_Description.fragment, m_Description.compileOptions);
            auto reflection = MergeShaderReflections(vertex.reflection, fragment.reflection);
            auto candidate = std::make_shared<VulkanPipeline>(
                m_Device, m_ColorFormat, m_DepthFormat,
                std::span<const std::uint32_t>(vertex.spirv),
                std::span<const std::uint32_t>(fragment.spirv), reflection);

            std::scoped_lock lock(m_Mutex);
            if (!m_Running)
            {
                return false;
            }
            if (m_CurrentPipeline)
            {
                m_RetiredPipelines.push_back(std::move(m_CurrentPipeline));
            }
            m_CurrentPipeline = std::move(candidate);
            m_Reflection = std::move(reflection);
            m_LastError.clear();
            return true;
        }
        catch (const std::exception& exception)
        {
            std::scoped_lock lock(m_Mutex);
            m_LastError = exception.what();
            if (m_Logger) m_Logger->Error("Shader reload failed: {}", m_LastError);
            return false;
        }
    }

    Task<bool> HotReloadablePipeline::ReloadAsync()
    {
        co_await ResumeOn(ThreadPool::Instance());
        try
        {
            co_return Reload();
        }
        catch (const std::exception& exception)
        {
            std::scoped_lock lock(m_Mutex);
            m_LastError = exception.what();
            if (m_Logger) m_Logger->Error("Shader reload failed: {}", m_LastError);
            co_return false;
        }
    }

    std::shared_ptr<VulkanPipeline> HotReloadablePipeline::GetPipeline() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_CurrentPipeline;
    }

    ShaderReflection HotReloadablePipeline::GetReflection() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_Reflection;
    }

    std::string HotReloadablePipeline::LastError() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_LastError;
    }

    bool HotReloadablePipeline::IsRunning() const noexcept
    {
        std::scoped_lock lock(m_Mutex);
        return m_Running;
    }

    void HotReloadablePipeline::QueueReload()
    {
        {
            std::scoped_lock lock(m_Mutex);
            if (!m_Running || m_Reloading)
            {
                return;
            }
            m_Reloading = true;
        }
        ThreadPool::Instance().Submit([this]
        {
            try
            {
                if (!Reload())
                {
                    std::scoped_lock lock(m_Mutex);
                    if (m_LastError.empty()) m_LastError = "Shader reload was cancelled.";
                }
            }
            catch (const std::exception& exception)
            {
                std::scoped_lock lock(m_Mutex);
                m_LastError = exception.what();
                if (m_Logger) m_Logger->Error("Shader reload failed: {}", m_LastError);
            }
            {
                std::scoped_lock lock(m_Mutex);
                m_Reloading = false;
            }
            m_ReloadCondition.notify_all();
        });
    }

    void HotReloadablePipeline::HandleFileChanged(const ShaderFileChangedEvent& event)
    {
        std::error_code error;
        const auto changed = std::filesystem::weakly_canonical(event.path, error);
        const auto vertex = std::filesystem::weakly_canonical(m_VertexPath, error);
        const auto fragment = std::filesystem::weakly_canonical(m_FragmentPath, error);
        if (event.path == m_VertexPath || event.path == m_FragmentPath ||
            changed == vertex || changed == fragment)
        {
            QueueReload();
        }
    }
}
