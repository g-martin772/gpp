export module GPP.Graphics:ShaderAssets;

import std;
import GPP.Core;
import :RenderConfig;
import :Shader;

namespace GPP
{
    export struct ShaderAssetMetadata
    {
        std::filesystem::path path;
        ShaderStage stage = ShaderStage::Vertex;
        std::string entryPoint;
        std::uint64_t sourceHash = 0;
        ShaderReflection reflection;
    };

    export class ShaderAssetCatalog : public IHostedService
    {
    public:
        using Dependencies = std::tuple<IFileSystem, RenderOptions, Logger>;

        ShaderAssetCatalog(std::shared_ptr<IFileSystem> fileSystem,
                           std::shared_ptr<RenderOptions> options,
                           std::shared_ptr<Logger> logger);

        Task<void> StartAsync(std::stop_token stopToken) override;
        Task<void> StopAsync() override;

        [[nodiscard]] Task<CompiledShader> AwaitShader(std::filesystem::path path) const;
        [[nodiscard]] bool TryGetShader(const std::filesystem::path& path,
                                        const CompiledShader*& shader) const;
        [[nodiscard]] std::vector<ShaderAssetMetadata> GetAvailableShaders() const;
        [[nodiscard]] std::vector<std::filesystem::path> GetShaderPaths() const;
        [[nodiscard]] bool IsReady() const noexcept;

    private:
        void CompileDirectory(const std::filesystem::path& directory,
                              const ShaderCompileOptions& compileOptions);
        static std::optional<ShaderStage> GetStage(const std::filesystem::path& path);

        std::shared_ptr<IFileSystem> m_FileSystem;
        std::shared_ptr<RenderOptions> m_Options;
        std::shared_ptr<Logger> m_Logger;
        ShaderCompiler m_Compiler;
        mutable std::mutex m_Mutex;
        mutable std::condition_variable m_Condition;
        std::unordered_map<std::string, CompiledShader> m_Shaders;
        std::vector<std::filesystem::path> m_ShaderPaths;
        std::exception_ptr m_StartError;
        bool m_Ready = false;
        bool m_Stopping = false;
    };
}
