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
    struct ShaderPipeline::CompiledPipeline
    {
        CompiledShader vertex;
        CompiledShader fragment;
        std::uint64_t generation = 0;
    };

    namespace
    {
        struct ShaderPipelineCompiledEvent
        {
            std::shared_ptr<ShaderPipeline::CompiledPipeline> compiled;
        };
    }

    ShaderPipeline::ShaderPipeline(
        std::shared_ptr<VulkanDevice> device,
        VulkanPipelineSpecification pipelineSpecification,
        ShaderPipelineDescription description,
        std::shared_ptr<IFileSystem> fileSystem,
        std::shared_ptr<EventDispatcher> dispatcher,
        std::shared_ptr<Logger> logger)
        : m_Device(std::move(device)),
          m_PipelineSpecification(pipelineSpecification),
          m_Description(std::move(description)),
          m_Compiler(fileSystem, logger),
          m_Logger(std::move(logger)),
          m_Dispatcher(std::move(dispatcher)),
          m_Watcher(fileSystem, m_Dispatcher, m_Logger)
    {
        if (!m_Device || !m_Dispatcher)
        {
            throw std::invalid_argument("ShaderPipeline requires a device and dispatcher.");
        }

        m_VertexPath = fileSystem->ResolvePath(m_Description.vertex.path);
        m_FragmentPath = fileSystem->ResolvePath(m_Description.fragment.path);
        if (m_Description.enableHotReload)
        {
            m_Watcher.Watch(m_VertexPath);
            m_Watcher.Watch(m_FragmentPath);
        }
    }

    ShaderPipeline::~ShaderPipeline()
    {
        try
        {
            StopAsync().get();
        }
        catch (...)
        {
        }
    }

    void ShaderPipeline::SetProgress(ShaderCompilationProgress progress)
    {
        ShaderCompilationProgress snapshot;
        {
            std::scoped_lock lock(m_Mutex);
            m_Progress = std::move(progress);
            snapshot = m_Progress;
        }
        if (m_Logger)
        {
            m_Logger->Debug("Shader pipeline [{}] {} ({}/{}) {}",
                            m_Description.vertex.path.string(),
                            snapshot.activeStage,
                            snapshot.completedStages,
                            snapshot.totalStages,
                            snapshot.message);
        }
    }

    std::shared_ptr<ShaderPipeline::CompiledPipeline> ShaderPipeline::Compile()
    {
        auto generation = [&]
        {
            std::scoped_lock lock(m_Mutex);
            return m_Progress.generation + 1;
        }();
        SetProgress({ShaderCompilationState::Compiling, 0, 2, generation, "vertex", "starting"});

        auto vertex = m_Compiler.Compile(m_Description.vertex, m_Description.compileOptions);
        SetProgress({ShaderCompilationState::Compiling, 1, 2, generation, "fragment", "starting"});
        auto fragment = m_Compiler.Compile(m_Description.fragment, m_Description.compileOptions);

        SetProgress({ShaderCompilationState::Compiling, 2, 2, generation, "reflection", "merging"});
        auto result = std::make_shared<CompiledPipeline>(
            CompiledPipeline{std::move(vertex), std::move(fragment), generation});
        return result;
    }

    Task<std::shared_ptr<ShaderPipeline::CompiledPipeline>> ShaderPipeline::CompileAsync()
    {
        co_await ResumeOn(ThreadPool::Instance());
        co_return Compile();
    }

    bool ShaderPipeline::Install(const std::shared_ptr<CompiledPipeline> compiled)
    {
        if (!compiled)
        {
            return false;
        }

        auto reflection = MergeShaderReflections(compiled->vertex.reflection, compiled->fragment.reflection);
        auto pipeline = std::make_shared<VulkanPipeline>(
            m_Device, m_PipelineSpecification, compiled->vertex, compiled->fragment);

        std::scoped_lock lock(m_Mutex);
        if (!m_Running)
        {
            return false;
        }
        if (m_CurrentPipeline)
        {
            m_RetiredPipelines.push_back(std::move(m_CurrentPipeline));
        }
        m_CurrentPipeline = std::move(pipeline);
        m_Reflection = reflection;
        m_Metadata = ShaderPipelineMetadata{
            .vertexSourceHash = compiled->vertex.sourceHash,
            .fragmentSourceHash = compiled->fragment.sourceHash,
            .descriptorCount = reflection.descriptorBindings.size(),
            .pushConstantCount = reflection.pushConstants.size(),
            .vertexInputCount = reflection.vertexInputs.size(),
            .reflection = reflection
        };
        m_LastError.clear();
        m_Progress = {
            ShaderCompilationState::Ready,
            2,
            2,
            compiled->generation,
            "pipeline",
            "ready"
        };

        if (m_Logger)
        {
            m_Logger->Debug(
                "Shader pipeline ready: descriptors={}, push constants={}, vertex inputs={}, "
                "vertex hash={:016x}, fragment hash={:016x}",
                m_Metadata.descriptorCount, m_Metadata.pushConstantCount,
                m_Metadata.vertexInputCount, m_Metadata.vertexSourceHash,
                m_Metadata.fragmentSourceHash);
            for (const auto& descriptor : reflection.descriptorBindings)
            {
                m_Logger->Debug("  descriptor set={} binding={} count={} type={} stages={} name={}",
                                descriptor.set, descriptor.binding, descriptor.descriptorCount,
                                static_cast<int>(descriptor.type),
                                static_cast<std::uint32_t>(descriptor.stages), descriptor.name);
            }
            for (const auto& range : reflection.pushConstants)
            {
                m_Logger->Debug("  push constant offset={} size={} stages={}",
                                range.offset, range.size,
                                static_cast<std::uint32_t>(range.stages));
            }
        }
        return true;
    }

    bool ShaderPipeline::StartOnRenderThread()
    {
        {
            std::scoped_lock lock(m_Mutex);
            if (m_Running)
            {
                return true;
            }
            m_StopSource = std::stop_source{};
            m_Running = true;
        }

        m_CompiledSubscription = m_Dispatcher->Subscribe<ShaderPipelineCompiledEvent>(
            [this](const ShaderPipelineCompiledEvent& event) { HandleCompiled(event.compiled); },
            EventDelivery::Async, EventTarget::Render);

        try
        {
            if (m_Description.enableHotReload)
            {
                m_FileSubscription = m_Dispatcher->Subscribe<ShaderFileChangedEvent>(
                    [this](const ShaderFileChangedEvent& event) { HandleFileChanged(event); },
                    EventDelivery::Async, EventTarget::Render);
                m_Watcher.StartAsync(m_Description.pollingInterval, m_StopSource.get_token()).get();
            }
            QueueReload();
            return true;
        }
        catch (...)
        {
            const auto exception = std::current_exception();
            std::string error = "Shader pipeline initialization failed.";
            try
            {
                std::rethrow_exception(exception);
            }
            catch (const std::exception& value)
            {
                error = value.what();
            }
            {
                std::scoped_lock lock(m_Mutex);
                m_LastError = error;
                m_Running = false;
                m_Progress.state = ShaderCompilationState::Failed;
                m_Progress.message = error;
            }
            if (m_Logger) m_Logger->Error("Shader pipeline initialization failed: {}", error);
            return false;
        }
    }

    Task<void> ShaderPipeline::StartAsync(std::stop_token stopToken)
    {
        {
            std::scoped_lock lock(m_Mutex);
            if (m_Running || stopToken.stop_requested())
            {
                co_return;
            }
            m_StopSource = std::stop_source{};
            m_Running = true;
        }

        m_CompiledSubscription = m_Dispatcher->Subscribe<ShaderPipelineCompiledEvent>(
            [this](const ShaderPipelineCompiledEvent& event) { HandleCompiled(event.compiled); },
            EventDelivery::Async, EventTarget::Render);
        try
        {
            auto compiled = co_await CompileAsync();
            m_Dispatcher->Publish(ShaderPipelineCompiledEvent{std::move(compiled)});
            if (m_Description.enableHotReload)
            {
                m_FileSubscription = m_Dispatcher->Subscribe<ShaderFileChangedEvent>(
                    [this](const ShaderFileChangedEvent& event) { HandleFileChanged(event); },
                    EventDelivery::Async, EventTarget::Render);
                co_await m_Watcher.StartAsync(m_Description.pollingInterval,
                                              m_StopSource.get_token());
            }
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
            m_Progress.state = ShaderCompilationState::Failed;
            m_Progress.message = m_LastError;
            m_Running = false;
            throw;
        }
        co_return;
    }

    Task<void> ShaderPipeline::StopAsync()
    {
        m_FileSubscription.Reset();
        m_CompiledSubscription.Reset();
        {
            std::scoped_lock lock(m_Mutex);
            m_Running = false;
            m_StopSource.request_stop();
        }
        co_await m_Watcher.StopAsync();
        std::unique_lock lock(m_Mutex);
        m_ReloadCondition.wait(lock, [this] { return !m_CompilationInFlight; });
        co_return;
    }

    bool ShaderPipeline::Reload()
    {
        try
        {
            // Compilation is always worker-side and installation is always delivered to the
            // render executor, regardless of which thread requests the reload.
            return ReloadAsync().get();
        }
        catch (const std::exception& exception)
        {
            std::scoped_lock lock(m_Mutex);
            m_LastError = exception.what();
            m_Progress.state = ShaderCompilationState::Failed;
            m_Progress.message = m_LastError;
            if (m_Logger) m_Logger->Error("Shader reload failed: {}", m_LastError);
            return false;
        }
    }

    Task<bool> ShaderPipeline::ReloadAsync()
    {
        auto compiled = co_await CompileAsync();
        m_Dispatcher->Publish(ShaderPipelineCompiledEvent{std::move(compiled)});
        co_return true;
    }

    void ShaderPipeline::HandleCompiled(const std::shared_ptr<CompiledPipeline>& compiled)
    {
        try
        {
            if (!Install(compiled))
            {
                std::scoped_lock lock(m_Mutex);
                m_LastError = "Shader reload was cancelled.";
            }
        }
        catch (const std::exception& exception)
        {
            std::scoped_lock lock(m_Mutex);
            m_LastError = exception.what();
            m_Progress.state = ShaderCompilationState::Failed;
            m_Progress.message = m_LastError;
            if (m_Logger) m_Logger->Error("Shader reload failed: {}", m_LastError);
        }
        {
            std::scoped_lock lock(m_Mutex);
            m_Reloading = false;
        }
        m_ReloadCondition.notify_all();
    }

    void ShaderPipeline::QueueReload()
    {
        {
            std::scoped_lock lock(m_Mutex);
            if (!m_Running || m_Reloading)
            {
                return;
            }
            m_Reloading = true;
            m_CompilationInFlight = true;
        }
        ThreadPool::Instance().Submit([this]
        {
            try
            {
                auto compiled = Compile();
                {
                    std::scoped_lock lock(m_Mutex);
                    m_CompilationInFlight = false;
                    if (!m_Running)
                    {
                        m_Reloading = false;
                        m_ReloadCondition.notify_all();
                        return;
                    }
                }
                m_Dispatcher->Publish(ShaderPipelineCompiledEvent{std::move(compiled)});
                m_ReloadCondition.notify_all();
            }
            catch (const std::exception& exception)
            {
                std::scoped_lock lock(m_Mutex);
                m_LastError = exception.what();
                m_Progress.state = ShaderCompilationState::Failed;
                m_Progress.message = m_LastError;
                m_Reloading = false;
                m_CompilationInFlight = false;
                if (m_Logger) m_Logger->Error("Shader reload failed: {}", m_LastError);
                m_ReloadCondition.notify_all();
            }
        });
    }

    void ShaderPipeline::HandleFileChanged(const ShaderFileChangedEvent& event)
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

    std::shared_ptr<VulkanPipeline> ShaderPipeline::GetPipeline() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_CurrentPipeline;
    }

    ShaderReflection ShaderPipeline::GetReflection() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_Reflection;
    }

    ShaderCompilationProgress ShaderPipeline::GetCompilationProgress() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_Progress;
    }

    ShaderPipelineMetadata ShaderPipeline::GetMetadata() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_Metadata;
    }

    std::string ShaderPipeline::LastError() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_LastError;
    }

    bool ShaderPipeline::IsRunning() const noexcept
    {
        std::scoped_lock lock(m_Mutex);
        return m_Running;
    }
}
