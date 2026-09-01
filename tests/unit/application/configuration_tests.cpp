#include <catch2/catch_test_macros.hpp>

import GPP;
import std;

using namespace GPP;

namespace
{
    std::filesystem::path MakeTempJsonPath(const std::string& prefix)
    {
        const auto ts = std::chrono::steady_clock::now().time_since_epoch().count();
        return std::filesystem::temp_directory_path() / (prefix + std::to_string(ts) + ".json");
    }

    void SetEnvVar(const std::string& key, const std::string& value)
    {
#if defined(_WIN32)
        _putenv_s(key.c_str(), value.c_str());
#else
        setenv(key.c_str(), value.c_str(), 1);
#endif
    }
}

TEST_CASE("Configuration supports JSON file mapping and typed parsing", "[application][configuration][file]")
{
    const auto filePath = MakeTempJsonPath("gpp_config_file_");
    std::ofstream out(filePath);
    out << R"({
  "Graphics": {
    "Window": {
      "Width": 1600,
      "Height": 900,
      "Fullscreen": true,
      "Title": "FromJson"
    },
    "Adapters": ["vk0", "vk1"]
  }
})";
    out.close();

    auto builder = App::CreateCliBuilder();
    builder.Configuration.AddJsonFile(filePath.string());
    builder.Services.Configure<WindowOptions>("Graphics:Window", [](const IConfigurationSection& section) {
        WindowOptions options;
        options.Width = section.GetValue<int>("Width", 0);
        options.Height = section.GetValue<int>("Height", 0);
        options.Fullscreen = section.GetValue<bool>("Fullscreen", false);
        options.Title = section.GetValue<std::string>("Title", "");
        return options;
    });

    auto app = builder.Build();
    const auto& config = app.GetConfiguration();

    CHECK(config.GetSection("Graphics:Window")->GetValue<int>("Width", 0) == 1600);
    CHECK(config.GetSection("Graphics:Window")->GetValue<int>("Height", 0) == 900);
    CHECK(config.GetSection("Graphics:Window")->GetValue<bool>("Fullscreen", false));
    CHECK(config.GetSection("Graphics:Adapters")->GetValue<std::string>("0", "") == "vk0");
    CHECK(config.GetSection("Graphics:Adapters")->GetValue<std::string>("1", "") == "vk1");

    const auto options = app.GetServiceProvider().GetRequiredService<WindowOptions>();
    CHECK(options->Width == 1600);
    CHECK(options->Height == 900);
    CHECK(options->Fullscreen);
    CHECK(options->Title == "FromJson");

    std::filesystem::remove(filePath);
}

TEST_CASE("Configuration supports prefixed environment variables and defaults", "[application][configuration][env]")
{
    SetEnvVar("GPP_TEST_Graphics__Window__Width", "1920");
    SetEnvVar("GPP_TEST_Graphics__Window__Height", "1080");
    SetEnvVar("GPP_TEST_Graphics__Window__Fullscreen", "on");
    SetEnvVar("GPP_TEST_Graphics__Window__Title", "FromEnv");
    SetEnvVar("OTHER_Graphics__Window__Width", "400");

    auto builder = App::CreateCliBuilder();
    builder.Configuration.AddEnvironmentVariables("GPP_TEST_");
    builder.Services.Configure<WindowOptions>("Graphics:Window", [](const IConfigurationSection& section) {
        WindowOptions options;
        options.Width = section.GetValue<int>("Width", -1);
        options.Height = section.GetValue<int>("Height", -1);
        options.Fullscreen = section.GetValue<bool>("Fullscreen", false);
        options.Title = section.GetValue<std::string>("Title", "");
        return options;
    });

    auto app = builder.Build();
    auto section = app.GetConfiguration().GetSection("Graphics:Window");
    CHECK(section->GetValue<int>("Width", 0) == 1920);
    CHECK(section->GetValue<int>("Height", 0) == 1080);
    CHECK(section->GetValue<bool>("Fullscreen", false));
    CHECK(section->GetValue<std::string>("Title", "") == "FromEnv");
    CHECK(app.GetConfiguration().GetSection("Missing:Path")->GetValue<int>("Width", 77) == 77);

    const auto options = app.GetServiceProvider().GetRequiredService<WindowOptions>();
    CHECK(options->Width == 1920);
    CHECK(options->Height == 1080);
    CHECK(options->Fullscreen);
    CHECK(options->Title == "FromEnv");
}

TEST_CASE("Configuration supports command line forms and precedence", "[application][configuration][argv]")
{
    SetEnvVar("GPP_TEST_Graphics__Window__Width", "1111");
    SetEnvVar("GPP_TEST_Graphics__Window__Height", "777");
    SetEnvVar("GPP_TEST_Graphics__Window__Fullscreen", "false");

    char* argv[] = {
        const_cast<char*>("scratch"),
        const_cast<char*>("--Graphics:Window:Width=1280"),
        const_cast<char*>("--Graphics:Window:Title"),
        const_cast<char*>("CLI Window"),
        const_cast<char*>("--Graphics:Window:Height=bad"),
        const_cast<char*>("--Graphics:Window:Fullscreen"),
        const_cast<char*>("--Flag")
    };
    const int argc = static_cast<int>(std::size(argv));

    auto builder = App::CreateCliBuilder();
    builder.Configuration
           .AddEnvironmentVariables("GPP_TEST_")
           .AddCommandLine(argc, argv);
    builder.Services.Configure<WindowOptions>("Graphics:Window", [](const IConfigurationSection& section) {
        WindowOptions options;
        options.Width = section.GetValue<int>("Width", 0);
        options.Height = section.GetValue<int>("Height", 720);
        options.Fullscreen = section.GetValue<bool>("Fullscreen", false);
        options.Title = section.GetValue<std::string>("Title", "");
        return options;
    });

    auto app = builder.Build();
    auto section = app.GetConfiguration().GetSection("Graphics:Window");

    CHECK(section->GetValue<int>("Width", 0) == 1280);
    CHECK(section->GetValue<std::string>("Title", "") == "CLI Window");
    CHECK(section->GetValue<int>("Height", 720) == 720);
    CHECK(section->GetValue<bool>("Fullscreen", false));
    CHECK(app.GetConfiguration().GetValue("Flag") == "true");

    const auto options = app.GetServiceProvider().GetRequiredService<WindowOptions>();
    CHECK(options->Width == 1280);
    CHECK(options->Height == 720);
    CHECK(options->Fullscreen);
    CHECK(options->Title == "CLI Window");
}
