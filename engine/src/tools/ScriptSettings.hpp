#pragma once

#include <devex/core/Error.hpp>
#include <devex/core/Export.hpp>
#include <devex/serialization/Text.hpp>

#include <array>
#include <filesystem>
#include <optional>
#include <string>
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
