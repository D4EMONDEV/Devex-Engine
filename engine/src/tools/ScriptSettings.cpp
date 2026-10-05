#include "ScriptSettings.hpp"

#include <devex/core/Path.hpp>

#include <algorithm>
#include <cctype>
#include <string_view>

namespace devex::tools::detail {
namespace {

constexpr std::array<std::string_view, 3> categoryKeys{"csharp", "cpp", "devex"};
constexpr std::array<std::string_view, 3> editorKeys{"devex", "system", "custom"};

std::string stringOr(const serialization::TextSection& section, std::string_view key, std::string fallback)
{
    const auto* value = section.findAttribute(key);
    const auto* text = value != nullptr ? serialization::asString(*value) : nullptr;
    return text != nullptr ? *text : std::move(fallback);
}

} // namespace

std::optional<std::size_t> scriptEditorCategory(const std::filesystem::path& file)
{
    std::string extension = core::toUtf8(file.extension());
    std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension == ".cs" || extension == ".csproj" || extension == ".sln" || extension == ".slnx")
    {
        return 0;
    }
    if (extension == ".cpp" || extension == ".c" || extension == ".cc" || extension == ".cxx" ||
        extension == ".h" || extension == ".hpp" || extension == ".hh" || extension == ".hxx" ||
        extension == ".inl" || extension == ".cmake" || file.filename() == "CMakeLists.txt")
    {
        return 1;
    }
    return extension.starts_with(".dvx") ? std::optional<std::size_t>(2) : std::nullopt;
}

const ScriptEditorChoice& scriptEditorFor(const ScriptSettings& settings, const std::filesystem::path& file)
{
    static const ScriptEditorChoice fallback;
    const auto category = scriptEditorCategory(file);
    return category ? settings.editors[*category] : fallback;
}

ScriptSettings readScriptSettings(const serialization::TextSection& section)
{
    ScriptSettings settings;
    for (std::size_t i = 0; i < settings.editors.size(); ++i)
    {
        const std::string prefix(categoryKeys[i]);
        auto& choice = settings.editors[i];
        const std::string editor = stringOr(section, prefix + "_editor", "devex");
        const auto found = std::ranges::find(editorKeys, editor);
        if (found != editorKeys.end())
        {
            choice.editor = static_cast<ScriptEditor>(found - editorKeys.begin());
        }
        choice.executable = stringOr(section, prefix + "_executable", {});
        choice.arguments = stringOr(section, prefix + "_arguments", choice.arguments);
    }
    if (const auto* value = section.findAttribute("automatic_compilation"))
    {
        settings.automaticCompilation = serialization::asBool(*value).value_or(true);
    }
    return settings;
}

serialization::TextSection writeScriptSettings(const ScriptSettings& settings)
{
    serialization::TextSection section;
    section.type = "scripts";
    for (std::size_t i = 0; i < settings.editors.size(); ++i)
    {
        const std::string prefix(categoryKeys[i]);
        const auto& choice = settings.editors[i];
        section.attributes.push_back({prefix + "_editor", std::string(editorKeys[static_cast<std::size_t>(choice.editor)])});
        section.attributes.push_back({prefix + "_executable", choice.executable});
        section.attributes.push_back({prefix + "_arguments", choice.arguments});
    }
    section.attributes.push_back({"automatic_compilation", settings.automaticCompilation});
    return section;
}

core::Result<std::vector<std::string>> scriptEditorCommand(const ScriptEditorChoice& editor,
    const std::filesystem::path& file, const std::filesystem::path& project)
{
    if (editor.executable.empty())
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "Choose an executable in Editor Settings > Script Editors");
    }
    const auto projectFile = scriptProjectFile(file, project);
    const auto projectDirectory = projectFile.empty() ? project : projectFile.parent_path();
    const ExternalEditor kind = externalEditorKind(core::pathFromUtf8(editor.executable));
    // Existing settings used "{file}" for Rider and CLion too. Upgrade that default automatically;
    // explicitly customized templates still have full control over their arguments.
    if (editor.arguments.empty() || editor.arguments == "\"{file}\"" || editor.arguments == "{file}")
    {
        std::vector<std::string> command{editor.executable};
        if (kind == ExternalEditor::Rider || kind == ExternalEditor::CLion)
        {
            if (!projectFile.empty())
            {
                command.push_back(core::toUtf8(kind == ExternalEditor::Rider ? projectFile : projectDirectory));
                if (sameEditorPath(file, projectFile)) { return command; }
            }
            command.insert(command.end(), {"--line", "1", core::toUtf8(file)});
            return command;
        }
        if (kind == ExternalEditor::VSCode)
        {
            command.insert(command.end(), {core::toUtf8(projectDirectory), "--goto", core::toUtf8(file) + ":1"});
            return command;
        }
        if (kind == ExternalEditor::VisualStudio && !projectFile.empty())
        {
            command.push_back(core::toUtf8(scriptEditorCategory(file) == 0 ? projectFile : projectDirectory));
            if (!sameEditorPath(file, projectFile))
            {
                command.insert(command.end(), {"/Command", "File.OpenFile \"" + core::toUtf8(file) + "\""});
            }
            return command;
        }
    }
    std::vector<std::string> command{editor.executable};
    std::string token;
    bool quoted = false;
    bool started = false;
    for (const char c : editor.arguments)
    {
        if (c == '"')
        {
            quoted = !quoted;
            started = true;
        }
        else if (!quoted && std::isspace(static_cast<unsigned char>(c)))
        {
            if (started)
            {
                command.push_back(std::move(token));
                token.clear();
                started = false;
            }
        }
        else
        {
            token += c;
            started = true;
        }
    }
    if (quoted)
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "Unclosed quote in the external editor arguments");
    }
    if (started)
    {
        command.push_back(std::move(token));
    }
    bool hasFile = false;
    for (std::size_t i = 1; i < command.size(); ++i)
    {
        const std::string pattern = std::move(command[i]);
        std::string expanded;
        for (std::size_t at = 0; at < pattern.size();)
        {
            const std::string_view tail = std::string_view(pattern).substr(at);
            if (tail.starts_with("{file}"))
            {
                expanded += core::toUtf8(file);
                hasFile = true;
                at += 6;
            }
            else if (tail.starts_with("{project}"))
            {
                expanded += core::toUtf8(project);
                at += 9;
            }
            else if (tail.starts_with("{project_file}"))
            {
                expanded += core::toUtf8(projectFile.empty() ? project : projectFile);
                at += 14;
            }
            else if (tail.starts_with("{project_dir}"))
            {
                expanded += core::toUtf8(projectDirectory);
                at += 13;
            }
            else
            {
                expanded += pattern[at++];
            }
        }
        command[i] = std::move(expanded);
    }
    if (!hasFile)
    {
        command.push_back(core::toUtf8(file));
    }
    return command;
}

} // namespace devex::tools::detail
