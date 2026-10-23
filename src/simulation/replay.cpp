module;
#include <yaml-cpp/yaml.h>
module GPP.Simulation;

import :Replay;
import std;

namespace GPP
{
    namespace
    {
        std::uint64_t ParseHash(const YAML::Node& node)
        {
            const auto text = node.as<std::string>("");
            std::uint64_t value = 0;
            const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
            if (ec != std::errc{} || end != text.data() + text.size())
            {
                throw std::runtime_error("Recording: invalid hash '" + text + "'");
            }
            return value;
        }

        YAML::Node Require(const YAML::Node& root, const char* key)
        {
            const auto node = root[key];
            if (!node) throw std::runtime_error(std::string("Recording: missing '") + key + "'");
            return node;
        }
    }

    std::string FormatHash(const std::uint64_t hash)
    {
        return std::format("{:016x}", hash);
    }

    std::string RecordingToYaml(const SimulationRecording& recording)
    {
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "Format" << YAML::Value << "GppRecording";
        out << YAML::Key << "Version" << YAML::Value << kRecordingFormatVersion;
        out << YAML::Key << "TickRate" << YAML::Value << recording.TickRate;
        out << YAML::Key << "StartTick" << YAML::Value << recording.StartTick;
        out << YAML::Key << "EndTick" << YAML::Value << recording.EndTick;
        out << YAML::Key << "CheckpointInterval" << YAML::Value << recording.CheckpointInterval;
        out << YAML::Key << "RngState" << YAML::Value << recording.State.RngState;
        out << YAML::Key << "Modules" << YAML::Value << YAML::BeginSeq;
        for (const auto& module : recording.State.Modules) out << module;
        out << YAML::EndSeq;

        const auto& clock = recording.Initial.Clock();
        YAML::Node clockNode;
        clockNode["Time"] = clock.Time;
        clockNode["Elapsed"] = clock.Elapsed;
        clockNode["TimeCarry"] = clock.TimeCarry;
        clockNode["ElapsedCarry"] = clock.ElapsedCarry;
        out << YAML::Key << "Clock" << YAML::Value << clockNode;
        if (recording.FinalHash) out << YAML::Key << "FinalHash" << YAML::Value << FormatHash(*recording.FinalHash);

        out << YAML::Key << "Checkpoints" << YAML::Value << YAML::BeginSeq;
        for (const auto& checkpoint : recording.Checkpoints)
        {
            out << YAML::Flow << YAML::BeginMap << YAML::Key << "Tick" << YAML::Value << checkpoint.Tick
                << YAML::Key << "Hash" << YAML::Value << FormatHash(checkpoint.Hash) << YAML::EndMap;
        }
        out << YAML::EndSeq;

        out << YAML::Key << "Commands" << YAML::Value << YAML::BeginSeq;
        for (const auto& record : recording.Commands)
        {
            YAML::Node node = YAML::Load(CommandToYaml(record.Applied));
            YAML::Node entry;
            entry["Tick"] = record.Tick;
            entry["Command"] = node;
            out << entry;
        }
        out << YAML::EndSeq;
        out << YAML::Key << "Initial" << YAML::Value << YAML::Literal << recording.Initial.SerializeToYaml();
        out << YAML::EndMap;
        return out.c_str();
    }

    SimulationRecording RecordingFromYaml(const std::string& yaml)
    {
        YAML::Node root;
        try { root = YAML::Load(yaml); }
        catch (const YAML::Exception& e) { throw std::runtime_error(std::string("Recording: ") + e.what()); }
        if (!root.IsMap() || root["Format"].as<std::string>("") != "GppRecording")
        {
            throw std::runtime_error("Recording: not a GppRecording file");
        }
        const int version = root["Version"].as<int>(0);
        if (version != kRecordingFormatVersion)
        {
            throw std::runtime_error(std::format("Recording: unsupported version {} (this build reads {})", version,
                                                 kRecordingFormatVersion));
        }

        try
        {
            SimulationRecording recording;
            recording.TickRate = Require(root, "TickRate").as<double>();
            recording.StartTick = Require(root, "StartTick").as<std::uint64_t>();
            recording.EndTick = Require(root, "EndTick").as<std::uint64_t>();
            recording.CheckpointInterval = root["CheckpointInterval"].as<std::uint64_t>(0);
            recording.State.RngState = Require(root, "RngState").as<std::uint64_t>();
            for (const auto& module : root["Modules"]) recording.State.Modules.push_back(module.as<std::string>(""));
            if (root["FinalHash"]) recording.FinalHash = ParseHash(root["FinalHash"]);
            for (const auto& checkpoint : root["Checkpoints"])
            {
                recording.Checkpoints.push_back(
                    StateCheckpoint{checkpoint["Tick"].as<std::uint64_t>(), ParseHash(checkpoint["Hash"])});
            }
            for (const auto& entry : root["Commands"])
            {
                const auto command = CommandFromYaml(YAML::Dump(entry["Command"]));
                if (!command) throw std::runtime_error("unreadable command");
                recording.Commands.push_back(CommandRecord{entry["Tick"].as<std::uint64_t>(), *command, std::nullopt});
            }

            recording.Initial = Scene("Replay");
            recording.Initial.DeserializeFromYaml(Require(root, "Initial").as<std::string>());
            const auto clock = Require(root, "Clock");
            SimClock simClock;
            simClock.Time = clock["Time"].as<double>(0.0);
            simClock.Elapsed = clock["Elapsed"].as<double>(0.0);
            simClock.TimeCarry = clock["TimeCarry"].as<double>(0.0);
            simClock.ElapsedCarry = clock["ElapsedCarry"].as<double>(0.0);
            recording.Initial.SetClock(simClock);
            return recording;
        }
        catch (const YAML::Exception& e)
        {
            throw std::runtime_error(std::string("Recording: ") + e.what());
        }
    }

    bool SaveRecording(const SimulationRecording& recording, const std::filesystem::path& path, std::string* error)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (file) file << RecordingToYaml(recording);
        if (file && file.flush()) return true;
        if (error) *error = "Cannot write " + path.string();
        return false;
    }

    std::optional<SimulationRecording> LoadRecording(const std::filesystem::path& path, std::string* error)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            if (error) *error = "Cannot open " + path.string();
            return std::nullopt;
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        try
        {
            return RecordingFromYaml(buffer.str());
        }
        catch (const std::exception& e)
        {
            if (error) *error = e.what();
            return std::nullopt;
        }
    }

    ReplayVerification VerifyReplay(const SimulationRecording& recording,
                                    std::vector<std::shared_ptr<ISimulationModule>> modules, const ReplayOptions& options)
    {
        ReplayVerification result;
        if (recording.Checkpoints.empty() && !recording.FinalHash)
        {
            result.Message = "Recording has no hashes to verify";
            return result;
        }

        SimulationRunner runner(Scene("Replay"), std::move(modules));
        if (options.OnRunnerCreated) options.OnRunnerCreated(runner);
        runner.PrepareReplay(recording);
        runner.StepAndWait(recording.Ticks());
        const auto actual = runner.ReplayCheckpoints();
        result.FinalHash = runner.StateHash();

        result.LastGoodTick = recording.StartTick;
        for (const auto& expected : recording.Checkpoints)
        {
            const auto it = std::ranges::find(actual, expected.Tick, &StateCheckpoint::Tick);
            ++result.CheckpointsChecked;
            if (it == actual.end() || it->Hash != expected.Hash)
            {
                result.DivergedTick = expected.Tick;
                result.Expected = expected.Hash;
                result.Actual = it != actual.end() ? it->Hash : 0;
                result.Message = std::format("State diverged between tick {} and tick {} (expected {}, got {})",
                                             result.LastGoodTick, expected.Tick, FormatHash(result.Expected),
                                             it != actual.end() ? FormatHash(result.Actual) : std::string("none"));
                return result;
            }
            result.LastGoodTick = expected.Tick;
        }

        if (recording.FinalHash)
        {
            ++result.CheckpointsChecked;
            if (*recording.FinalHash != result.FinalHash)
            {
                result.DivergedTick = recording.EndTick;
                result.Expected = *recording.FinalHash;
                result.Actual = result.FinalHash;
                result.Message = std::format("Final state diverged after tick {} (expected {}, got {})", result.LastGoodTick,
                                             FormatHash(result.Expected), FormatHash(result.Actual));
                return result;
            }
            result.LastGoodTick = recording.EndTick;
        }

        result.Ok = true;
        result.Message = std::format("Verified {} hashes over {} ticks", result.CheckpointsChecked, recording.Ticks());
        return result;
    }
}
