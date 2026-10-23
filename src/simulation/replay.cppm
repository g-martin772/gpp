module;
#include <yaml-cpp/yaml.h>
export module GPP.Simulation:Replay;

import std;
import :Scene;
import :Commands;
import :Runner;

namespace GPP
{
    export constexpr int kRecordingFormatVersion = 1;

    // Versioned YAML: initial scene, clock, tick rate, runner state (random stream and module state), command records and
    // hash checkpoints. Throws std::runtime_error on malformed input or an unsupported version.
    export [[nodiscard]] std::string RecordingToYaml(const SimulationRecording& recording);
    export [[nodiscard]] SimulationRecording RecordingFromYaml(const std::string& yaml);

    export bool SaveRecording(const SimulationRecording& recording, const std::filesystem::path& path,
                              std::string* error = nullptr);
    export [[nodiscard]] std::optional<SimulationRecording> LoadRecording(const std::filesystem::path& path,
                                                                          std::string* error = nullptr);

    export struct ReplayOptions
    {
        // Called with the fresh replay runner before the recording is loaded (e.g. to hand modules the runner's Random()).
        std::function<void(SimulationRunner&)> OnRunnerCreated;
    };

    export struct ReplayVerification
    {
        bool Ok{false};
        std::size_t CheckpointsChecked{0};
        // First checkpoint whose hash differs; the divergence happened in (LastGoodTick, DivergedTick].
        std::optional<std::uint64_t> DivergedTick;
        std::uint64_t LastGoodTick{0};
        std::uint64_t Expected{0};
        std::uint64_t Actual{0};
        std::uint64_t FinalHash{0};
        std::string Message;
    };

    // Replays headlessly through the deterministic stepper (no thread) and compares every checkpoint and the final hash.
    export [[nodiscard]] ReplayVerification VerifyReplay(const SimulationRecording& recording,
                                                         std::vector<std::shared_ptr<ISimulationModule>> modules,
                                                         const ReplayOptions& options = {});

    export [[nodiscard]] std::string FormatHash(std::uint64_t hash);
}
