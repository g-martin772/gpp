export module GPP.Simulation:SceneValidation;

import std;

namespace GPP
{
    export enum class DiagnosticSeverity { Warning, Error };

    export struct SceneDiagnostic
    {
        DiagnosticSeverity Severity{DiagnosticSeverity::Warning};
        std::string Message;
        std::uint64_t EntityGuid{0};
        std::string EntityName;
        std::string Component;
        std::string Field;
        int Line{0};
        int Column{0};

        [[nodiscard]] std::string Format(std::string_view file) const;
    };

    export [[nodiscard]] std::vector<SceneDiagnostic> ValidateSceneYaml(const std::string& text);
    export [[nodiscard]] std::vector<SceneDiagnostic> ValidateSceneFile(const std::filesystem::path& path);
    export [[nodiscard]] std::size_t CountDiagnostics(std::span<const SceneDiagnostic> diagnostics,
                                                      DiagnosticSeverity severity);
}
