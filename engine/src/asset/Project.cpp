#include <devex/asset/Project.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Path.hpp>
#include <devex/serialization/Text.hpp>

#include <system_error>

namespace devex::asset {
namespace {

constexpr std::int64_t projectFormatVersion = 1;

[[nodiscard]] std::filesystem::path absoluteNormal(const std::filesystem::path& path)
{
    std::error_code error;
    std::filesystem::path absolute = std::filesystem::absolute(path, error);
    return (error ? path : absolute).lexically_normal();
}

// True when a relative path is empty or leaves its base directory.
[[nodiscard]] bool escapesBase(const std::filesystem::path& relative)
{
    return relative.empty() || relative.has_root_path() || *relative.begin() == "..";
}

} // namespace

std::string Project::resourcePath(const std::filesystem::path& path) const
{
    const std::filesystem::path relative =
        absoluteNormal(path).lexically_relative(absoluteNormal(root));
    if (escapesBase(relative))
    {
        return {};
    }
    return std::string(resourceScheme) + core::toUtf8(relative);
}

std::optional<std::filesystem::path> Project::absolutePath(std::string_view resource) const
{
    if (!resource.starts_with(resourceScheme))
    {
        return std::nullopt;
    }
    const std::filesystem::path relative =
        core::pathFromUtf8(resource.substr(resourceScheme.size())).lexically_normal();
    if (escapesBase(relative))
    {
        return std::nullopt;
    }
    return absoluteNormal(root / relative);
}

core::Result<Project> loadProject(const std::filesystem::path& projectFile)
{
    const core::Result<std::string> text = core::readTextFile(projectFile);
    if (!text)
    {
        return std::unexpected(text.error());
    }
    const core::Result<serialization::TextDocument> document = serialization::parseText(*text);
    if (!document)
    {
        return core::makeError(document.error().code, "'{}': {}", core::toUtf8(projectFile),
                               document.error().message);
    }

    const serialization::TextSection* const section =
        document->sections.empty() ? nullptr : &document->sections.front();
    if (section == nullptr || section->type != "project")
    {
        return core::makeError(core::ErrorCode::Parse, "'{}' does not start with [project]",
                               core::toUtf8(projectFile));
    }
    const serialization::TextValue* const format = section->findAttribute("format");
    const std::optional<std::int64_t> version =
        format != nullptr ? serialization::asInteger(*format) : std::nullopt;
    if (!version || *version > projectFormatVersion)
    {
        return core::makeError(core::ErrorCode::Unsupported,
                               "'{}' needs a newer version of Devex", core::toUtf8(projectFile));
    }

    Project project;
    project.root = absoluteNormal(projectFile).parent_path();
    const serialization::TextValue* const name = section->findAttribute("name");
    const std::string* const nameText = name != nullptr ? serialization::asString(*name) : nullptr;
    project.name = nameText != nullptr ? *nameText : core::toUtf8(projectFile.stem());
    return project;
}

core::Result<Project> createProject(const std::filesystem::path& directory, std::string_view name)
{
    serialization::TextDocument document;
    serialization::TextSection& section = document.sections.emplace_back();
    section.type = "project";
    section.attributes.push_back({"format", serialization::TextValue(projectFormatVersion)});
    section.attributes.push_back({"name", serialization::TextValue(std::string(name))});

    const std::filesystem::path projectFile =
        directory / core::pathFromUtf8(std::string(name) + std::string(projectExtension));
    if (core::Result<void> written =
            core::writeTextFile(projectFile, serialization::writeText(document));
        !written)
    {
        return std::unexpected(written.error());
    }

    core::Result<Project> project = loadProject(projectFile);
    if (!project)
    {
        return project;
    }
    std::error_code error;
    std::filesystem::create_directories(project->assetsDirectory(), error);
    if (error)
    {
        return core::makeError(core::ErrorCode::Io, "cannot create '{}': {}",
                               core::toUtf8(project->assetsDirectory()), error.message());
    }
    return project;
}

} // namespace devex::asset
