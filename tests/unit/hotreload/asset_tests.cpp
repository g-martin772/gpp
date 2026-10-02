#include <catch2/catch_test_macros.hpp>

import GPP;
import std;

using namespace GPP;

namespace
{
    std::filesystem::path TestPluginPath()
    {
        FileSystem fs;
        return fs.GetBinaryDirectory() / "gpp_test_hot_reload_plugin.so";
    }

    std::filesystem::path MakeTempDirectory(const std::string& prefix)
    {
        const auto path = std::filesystem::temp_directory_path() /
            (prefix + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path);
        return path;
    }

    void SetLogEnv(const std::string& value)
    {
#if defined(_WIN32)
        _putenv_s("GPP_TEST_HOT_RELOAD_LOG", value.c_str());
#else
        if (value.empty())
        {
            unsetenv("GPP_TEST_HOT_RELOAD_LOG");
        }
        else
        {
            setenv("GPP_TEST_HOT_RELOAD_LOG", value.c_str(), 1);
        }
#endif
    }

    // RAII guard: points GPP_TEST_HOT_RELOAD_LOG (read by test_hot_reload_plugin.cpp) at a fresh temp
    // file for the lifetime of one test case, and cleans both the env var and the file up afterward.
    class HotReloadLogEnv
    {
    public:
        HotReloadLogEnv()
            : m_Path(std::filesystem::temp_directory_path() /
                     ("gpp_hotreload_log_" +
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".txt"))
        {
            SetLogEnv(m_Path.string());
        }

        ~HotReloadLogEnv()
        {
            SetLogEnv("");
            std::error_code error;
            std::filesystem::remove(m_Path, error);
        }

        HotReloadLogEnv(const HotReloadLogEnv&) = delete;
        HotReloadLogEnv& operator=(const HotReloadLogEnv&) = delete;

        [[nodiscard]] std::vector<std::string> ReadLines() const
        {
            std::ifstream file(m_Path);
            std::vector<std::string> lines;
            std::string line;
            while (std::getline(file, line))
            {
                if (!line.empty())
                {
                    lines.push_back(line);
                }
            }
            return lines;
        }

    private:
        std::filesystem::path m_Path;
    };
}

TEST_CASE("HotReloadAsset loads, stages, commits, and retires plugin instances in order",
          "[hotreload][asset]")
{
    HotReloadLogEnv logEnv;

    auto fileSystem = std::make_shared<FileSystem>();
    auto dispatcher = std::make_shared<EventDispatcher>();
    auto logger = std::make_shared<Logger>();
    ServiceCollection services;
    auto provider = services.Build();

    HotReloadAssetDescription description{.libraryPath = TestPluginPath(), .shadowCopy = false};
    HotReloadAsset asset("test-asset", description, fileSystem, dispatcher, logger);

    CHECK(asset.GetState() == HotReloadState::Unloaded);
    CHECK_FALSE(asset.GetInstance());

    const auto firstHandle = asset.Stage(provider);
    REQUIRE(firstHandle);
    CHECK(firstHandle.generation == 1);
    CHECK(asset.GetState() == HotReloadState::Loaded);
    // Staging does not install anything until Commit() is called.
    CHECK_FALSE(asset.GetInstance());

    {
        auto retired = asset.Commit();
        CHECK_FALSE(retired.HasInstance()); // nothing was active before the very first commit
    }
    CHECK(asset.GetInstance().instance == firstHandle.instance);
    CHECK(logEnv.ReadLines() == std::vector<std::string>{"create"});

    const auto secondHandle = asset.Stage(provider);
    REQUIRE(secondHandle);
    CHECK(secondHandle.generation == 2);
    CHECK(secondHandle.instance != firstHandle.instance);
    // Staging a replacement must not disturb the still-active first instance.
    CHECK(asset.GetInstance().instance == firstHandle.instance);
    CHECK(logEnv.ReadLines() == std::vector<std::string>{"create", "create"});

    {
        auto retired = asset.Commit();
        REQUIRE(retired.HasInstance());
        CHECK(retired.GetInstance() == firstHandle.instance);
        CHECK(asset.GetInstance().instance == secondHandle.instance);
        // The outgoing (first) instance is still alive here -- retiring it is deferred until
        // `retired` itself is destroyed, so a caller gets a chance to notify it first (e.g. call
        // OnDetach() on it, as GPP.Graphics's HotReloadLayerProxy does).
        CHECK(logEnv.ReadLines() == std::vector<std::string>{"create", "create"});
    }
    // `retired` just went out of scope: the first instance's destroy() ran exactly once, after both
    // create() calls and after the second instance was already installed.
    CHECK(logEnv.ReadLines() == std::vector<std::string>{"create", "create", "destroy"});
}

TEST_CASE("HotReloadAsset::Load is a convenience for an initial Stage+Commit", "[hotreload][asset]")
{
    HotReloadLogEnv logEnv;

    auto fileSystem = std::make_shared<FileSystem>();
    auto dispatcher = std::make_shared<EventDispatcher>();
    auto logger = std::make_shared<Logger>();
    ServiceCollection services;
    auto provider = services.Build();

    HotReloadAssetDescription description{.libraryPath = TestPluginPath(), .shadowCopy = false};
    HotReloadAsset asset("load-asset", description, fileSystem, dispatcher, logger);

    REQUIRE(asset.Load(provider));
    CHECK(asset.GetInstance());
    CHECK(asset.GetState() == HotReloadState::Loaded);
    CHECK(logEnv.ReadLines() == std::vector<std::string>{"create"});
}

TEST_CASE("HotReloadAsset destroys its current instance on destruction", "[hotreload][asset]")
{
    HotReloadLogEnv logEnv;

    auto fileSystem = std::make_shared<FileSystem>();
    auto dispatcher = std::make_shared<EventDispatcher>();
    auto logger = std::make_shared<Logger>();
    ServiceCollection services;
    auto provider = services.Build();

    {
        HotReloadAsset asset("scoped-asset",
                             HotReloadAssetDescription{.libraryPath = TestPluginPath(), .shadowCopy = false},
                             fileSystem, dispatcher, logger);
        REQUIRE(asset.Load(provider));
        CHECK(logEnv.ReadLines() == std::vector<std::string>{"create"});
    }
    CHECK(logEnv.ReadLines() == std::vector<std::string>{"create", "destroy"});
}

TEST_CASE("HotReloadAsset::Commit without a staged instance throws", "[hotreload][asset]")
{
    auto fileSystem = std::make_shared<FileSystem>();
    auto dispatcher = std::make_shared<EventDispatcher>();
    auto logger = std::make_shared<Logger>();

    HotReloadAssetDescription description{.libraryPath = TestPluginPath(), .shadowCopy = false};
    HotReloadAsset asset("no-stage-asset", description, fileSystem, dispatcher, logger);

    CHECK_THROWS_AS(asset.Commit(), std::logic_error);
}

TEST_CASE("HotReloadAsset reports a clear error for a missing library file", "[hotreload][asset]")
{
    auto fileSystem = std::make_shared<FileSystem>();
    auto dispatcher = std::make_shared<EventDispatcher>();
    auto logger = std::make_shared<Logger>();
    ServiceCollection services;
    auto provider = services.Build();

    HotReloadAssetDescription description{
        .libraryPath = std::filesystem::temp_directory_path() / "gpp_hotreload_missing.so",
        .shadowCopy = false
    };
    HotReloadAsset asset("missing-asset", description, fileSystem, dispatcher, logger);

    const auto handle = asset.Stage(provider);
    CHECK_FALSE(handle);
    CHECK(asset.GetState() == HotReloadState::Failed);
    CHECK(asset.LastError().find("does not exist") != std::string::npos);
}

TEST_CASE("HotReloadAsset reports a clear error for a missing exported symbol", "[hotreload][asset]")
{
    auto fileSystem = std::make_shared<FileSystem>();
    auto dispatcher = std::make_shared<EventDispatcher>();
    auto logger = std::make_shared<Logger>();
    ServiceCollection services;
    auto provider = services.Build();

    HotReloadAssetDescription description{
        .libraryPath = TestPluginPath(),
        .shadowCopy = false,
        .createSymbol = "ThisSymbolDoesNotExist"
    };
    HotReloadAsset asset("bad-symbol-asset", description, fileSystem, dispatcher, logger);

    const auto handle = asset.Stage(provider);
    CHECK_FALSE(handle);
    CHECK(asset.GetState() == HotReloadState::Failed);
    CHECK(asset.LastError().find("missing required export") != std::string::npos);
}

TEST_CASE("HotReloadAsset reports a clear error for an ABI version mismatch", "[hotreload][asset]")
{
    auto fileSystem = std::make_shared<FileSystem>();
    auto dispatcher = std::make_shared<EventDispatcher>();
    auto logger = std::make_shared<Logger>();
    ServiceCollection services;
    auto provider = services.Build();

    HotReloadAssetDescription description{
        .libraryPath = TestPluginPath(),
        .shadowCopy = false,
        .expectedAbiVersion = kHotReloadAbiVersion + 1
    };
    HotReloadAsset asset("abi-mismatch-asset", description, fileSystem, dispatcher, logger);

    const auto handle = asset.Stage(provider);
    CHECK_FALSE(handle);
    CHECK(asset.GetState() == HotReloadState::Failed);
    CHECK(asset.LastError().find("ABI version mismatch") != std::string::npos);
}

TEST_CASE("HotReloadAsset keeps the current instance running after a failed reload, and recovers",
          "[hotreload][asset]")
{
    HotReloadLogEnv logEnv;

    auto fileSystem = std::make_shared<FileSystem>();
    auto dispatcher = std::make_shared<EventDispatcher>();
    auto logger = std::make_shared<Logger>();
    ServiceCollection services;
    auto provider = services.Build();

    // A "controlled" copy of the real plugin binary whose contents this test corrupts and repairs
    // independently of the actual build artifact -- mirroring a linker leaving a partially-written
    // file mid-rebuild while the engine is running.
    const auto controlledDir = MakeTempDirectory("gpp_hotreload_controlled_");
    const auto controlledPath = controlledDir / "controlled_plugin.so";
    std::filesystem::copy_file(TestPluginPath(), controlledPath);

    HotReloadAssetDescription description{
        .libraryPath = controlledPath,
        .shadowDirectory = controlledDir / "shadow",
        .shadowCopy = true
    };
    HotReloadAsset asset("recoverable-asset", description, fileSystem, dispatcher, logger);

    REQUIRE(asset.Load(provider));
    const auto healthyInstance = asset.GetInstance();
    CHECK(logEnv.ReadLines() == std::vector<std::string>{"create"});

    // Corrupt the "build output": too short/invalid to be a loadable shared library.
    {
        std::ofstream corrupt(controlledPath, std::ios::binary | std::ios::trunc);
        corrupt << "not a real shared library";
    }

    const auto failedHandle = asset.Stage(provider);
    CHECK_FALSE(failedHandle);
    CHECK(asset.GetState() == HotReloadState::Failed);
    // The already-loaded, healthy instance is completely unaffected by the failed reload attempt --
    // it was dlopen'd from its own earlier shadow copy, never from `controlledPath` itself.
    CHECK(asset.GetInstance().instance == healthyInstance.instance);
    CHECK(logEnv.ReadLines() == std::vector<std::string>{"create"});

    // The next successful build repairs the file; the asset recovers on the following reload.
    std::filesystem::copy_file(TestPluginPath(), controlledPath,
                               std::filesystem::copy_options::overwrite_existing);
    const auto recoveredHandle = asset.Stage(provider);
    REQUIRE(recoveredHandle);
    CHECK(asset.GetState() == HotReloadState::Loaded);
    {
        auto retired = asset.Commit();
        CHECK(retired.GetInstance() == healthyInstance.instance);
    }
    CHECK(asset.GetInstance().instance == recoveredHandle.instance);
    CHECK(logEnv.ReadLines() == std::vector<std::string>{"create", "create", "destroy"});

    std::filesystem::remove_all(controlledDir);
}

TEST_CASE("HotReloadAsset shadow-copies the library before loading it", "[hotreload][asset]")
{
    auto fileSystem = std::make_shared<FileSystem>();
    auto dispatcher = std::make_shared<EventDispatcher>();
    auto logger = std::make_shared<Logger>();
    ServiceCollection services;
    auto provider = services.Build();

    const auto shadowDir = MakeTempDirectory("gpp_hotreload_shadow_");
    HotReloadAssetDescription description{
        .libraryPath = TestPluginPath(),
        .shadowDirectory = shadowDir,
        .shadowCopy = true
    };
    HotReloadAsset asset("shadow-asset", description, fileSystem, dispatcher, logger);

    REQUIRE(asset.Load(provider));
    CHECK(std::filesystem::exists(shadowDir / "shadow-asset_1.so"));
    // The original build output must remain untouched (never locked/renamed), so a build system can
    // keep writing to it.
    CHECK(std::filesystem::exists(TestPluginPath()));

    std::filesystem::remove_all(shadowDir);
}
