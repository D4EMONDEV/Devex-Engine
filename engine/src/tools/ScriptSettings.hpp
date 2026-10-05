#pragma once

#include <devex/core/Error.hpp>
#include <devex/core/Export.hpp>
#include <devex/serialization/Text.hpp>

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <span>
#include <vector>

namespace devex::tools::detail {

enum class ScriptEditor : std::uint8_t
{
    Devex,
    System,
    Custom,
};

struct ScriptEditorChoice
{
    ScriptEditor editor = ScriptEditor::Devex;
    std::string executable;
    std::string arguments = "\"{file}\"";

    bool operator==(const ScriptEditorChoice&) const = default;
};

// User preferences, independent of the project. The three entries are C#, C++ and Devex text.
struct ScriptSettings
{
    std::array<ScriptEditorChoice, 3> editors;
    bool automaticCompilation = true;

    bool operator==(const ScriptSettings&) const = default;
};

enum class ExternalEditor : std::uint8_t { Unknown, Rider, CLion, VisualStudio, VSCode };

struct InstalledScriptEditor
{
    std::string name;
    std::filesystem::path executable;
};

[[nodiscard]] DEVEX_API ExternalEditor externalEditorKind(const std::filesystem::path& executable);
[[nodiscard]] DEVEX_API bool sameEditorPath(const std::filesystem::path& left, const std::filesystem::path& right);
// Only known installation locations and PATH are inspected; discovery never launches an IDE.
[[nodiscard]] DEVEX_API std::vector<InstalledScriptEditor> detectScriptEditors();
[[nodiscard]] DEVEX_API std::vector<InstalledScriptEditor> findScriptEditors(std::span<const std::filesystem::path> candidates);
[[nodiscard]] DEVEX_API std::filesystem::path scriptProjectFile(const std::filesystem::path& file,
                                                              const std::filesystem::path& project);

[[nodiscard]] DEVEX_API std::optional<std::size_t> scriptEditorCategory(const std::filesystem::path& file);
[[nodiscard]] DEVEX_API const ScriptEditorChoice& scriptEditorFor(const ScriptSettings& settings,
                                                                const std::filesystem::path& file);
[[nodiscard]] DEVEX_API ScriptSettings readScriptSettings(const serialization::TextSection& section);
[[nodiscard]] DEVEX_API serialization::TextSection writeScriptSettings(const ScriptSettings& settings);
// Split the template before substituting paths, so spaces and quotes in a path stay in one argument.
// No shell is involved. Backslashes in Windows paths are preserved.
[[nodiscard]] DEVEX_API core::Result<std::vector<std::string>> scriptEditorCommand(
    const ScriptEditorChoice& editor, const std::filesystem::path& file, const std::filesystem::path& project);

} // namespace devex::tools::detail
