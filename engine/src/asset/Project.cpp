#include <devex/asset/Project.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Path.hpp>
#include <devex/core/Uuid.hpp>
#include <devex/serialization/Text.hpp>

#include <algorithm>
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

[[nodiscard]] math::Vec3 vec3Or(const serialization::TextSection& section, std::string_view key, math::Vec3 fallback)
{
    const serialization::TextValue* const value = section.findProperty(key);
    const serialization::TextCall* const call = value != nullptr ? serialization::asCall(*value, "vec3") : nullptr;
    if (call == nullptr || call->arguments.size() != 3)
    {
        return fallback;
    }
    math::Vec3 result = fallback;
    for (std::size_t index = 0; index < 3; ++index)
    {
        if (const std::optional<double> number = serialization::asNumber(call->arguments[index]))
        {
            result[static_cast<int>(index)] = static_cast<float>(*number);
        }
    }
    return result;
}

void readPhysics(const serialization::TextDocument& document, PhysicsSettings& physics)
{
    for (const serialization::TextSection& section : document.sections)
    {
        if (section.type == "physics")
        {
            physics.gravity = vec3Or(section, "gravity", physics.gravity);
        }
        else if (section.type == "physics_layer")
        {
            const serialization::TextValue* const indexValue = section.findAttribute("index");
            const std::optional<std::int64_t> index = indexValue != nullptr ? serialization::asInteger(*indexValue) : std::nullopt;
            if (!index || *index < 0 || *index >= static_cast<std::int64_t>(physicsLayerCount))
            {
                continue;
            }
            const auto layer = static_cast<std::size_t>(*index);
            if (const serialization::TextValue* const name = section.findAttribute("name"))
            {
                if (const std::string* const text = serialization::asString(*name))
                {
                    physics.layerNames[layer] = *text;
                }
            }
            if (const serialization::TextValue* const collides = section.findProperty("collides"))
            {
                if (const std::optional<std::int64_t> mask = serialization::asInteger(*collides))
                {
                    physics.layerCollisions[layer] = static_cast<std::uint16_t>(*mask & 0xFFFF);
                }
            }
        }
    }
    // The matrix stays symmetric: a pair collides only when both layers agree.
    for (std::uint32_t a = 0; a < physicsLayerCount; ++a)
    {
        for (std::uint32_t b = a + 1; b < physicsLayerCount; ++b)
        {
            physics.setCollides(a, b, physics.collides(a, b));
        }
    }
    if (physics.layerNames[0].empty())
    {
        physics.layerNames[0] = "Default";
    }
}

[[nodiscard]] std::optional<float> numberAttribute(const serialization::TextSection& section, std::string_view key)
{
    const serialization::TextValue* const value = section.findAttribute(key);
    const std::optional<double> number = value != nullptr ? serialization::asNumber(*value) : std::nullopt;
    return number ? std::optional(static_cast<float>(*number)) : std::nullopt;
}

void readAudio(const serialization::TextDocument& document, AudioSettings& audio)
{
    for (const serialization::TextSection& section : document.sections)
    {
        if (section.type == "audio")
        {
            audio.masterVolume = std::max(0.0f, numberAttribute(section, "master_volume").value_or(audio.masterVolume));
        }
        else if (section.type == "audio_group")
        {
            const serialization::TextValue* const indexValue = section.findAttribute("index");
            const std::optional<std::int64_t> index = indexValue != nullptr ? serialization::asInteger(*indexValue) : std::nullopt;
            if (!index || *index < 0 || *index >= static_cast<std::int64_t>(audioGroupCount))
            {
                continue;
            }
            const auto group = static_cast<std::size_t>(*index);
            if (const serialization::TextValue* const name = section.findAttribute("name"))
            {
                if (const std::string* const text = serialization::asString(*name))
                {
                    audio.groupNames[group] = *text;
                }
            }
            audio.groupVolumes[group] = std::max(0.0f, numberAttribute(section, "volume").value_or(audio.groupVolumes[group]));
        }
    }
    if (audio.groupNames[0].empty())
    {
        audio.groupNames[0] = "Effects";
    }
}

[[nodiscard]] const std::string* stringAttribute(const serialization::TextSection& section, std::string_view key)
{
    const serialization::TextValue* const value = section.findAttribute(key);
    return value != nullptr ? serialization::asString(*value) : nullptr;
}

void readUnsigned(const serialization::TextSection& section, std::string_view key, std::uint32_t& value)
{
    const serialization::TextValue* const text = section.findAttribute(key);
    if (const std::optional<std::int64_t> number = text != nullptr ? serialization::asInteger(*text) : std::nullopt;
        number && *number >= 0 && *number <= 0xFFFFFFFF)
    {
        value = static_cast<std::uint32_t>(*number);
    }
}

void readBool(const serialization::TextSection& section, std::string_view key, bool& value)
{
    const serialization::TextValue* const text = section.findAttribute(key);
    if (const std::optional<bool> flag = text != nullptr ? serialization::asBool(*text) : std::nullopt)
    {
        value = *flag;
    }
}

void readWindowAndExport(const serialization::TextDocument& document, Project& project)
{
    for (const serialization::TextSection& section : document.sections)
    {
        if (section.type == "window")
        {
            WindowSettings& window = project.window;
            readUnsigned(section, "width", window.width);
            readUnsigned(section, "height", window.height);
            readBool(section, "fullscreen", window.fullscreen);
            readBool(section, "vsync", window.vsync);
            readUnsigned(section, "max_frame_rate", window.maxFrameRate);
            const serialization::TextValue* const icon = section.findAttribute("icon");
            const serialization::TextCall* const call = icon != nullptr ? serialization::asCall(*icon, "asset") : nullptr;
            const std::string* const uuid =
                call != nullptr && call->arguments.size() == 1 ? serialization::asString(call->arguments.front()) : nullptr;
            if (const std::optional<core::Uuid> parsed = uuid != nullptr ? core::Uuid::parse(*uuid) : std::nullopt)
            {
                window.icon = AssetId{*parsed};
            }
            window.width = std::max<std::uint32_t>(window.width, 64);
            window.height = std::max<std::uint32_t>(window.height, 64);
        }
        else if (section.type == "export")
        {
            if (const std::string* const output = stringAttribute(section, "output"))
            {
                project.exportSettings.output = *output;
            }
            if (const std::string* const configuration = stringAttribute(section, "configuration"))
            {
                project.exportSettings.configuration = *configuration;
            }
        }
        else if (section.type == "export_scene" || section.type == "export_folder")
        {
            if (const std::string* const path = stringAttribute(section, "path"); path != nullptr && !path->empty())
            {
                (section.type == "export_scene" ? project.exportSettings.scenes : project.exportSettings.includeFolders)
                    .push_back(*path);
            }
        }
    }
}

} // namespace

bool PhysicsSettings::collides(std::uint32_t a, std::uint32_t b) const noexcept
{
    if (a >= physicsLayerCount || b >= physicsLayerCount)
    {
        return false;
    }
    return (layerCollisions[a] & (1u << b)) != 0 && (layerCollisions[b] & (1u << a)) != 0;
}

void PhysicsSettings::setCollides(std::uint32_t a, std::uint32_t b, bool collide) noexcept
{
    if (a >= physicsLayerCount || b >= physicsLayerCount)
    {
        return;
    }
    const auto set = [collide](std::uint16_t& mask, std::uint32_t bit) {
        mask = collide ? static_cast<std::uint16_t>(mask | (1u << bit)) : static_cast<std::uint16_t>(mask & ~(1u << bit));
    };
    set(layerCollisions[a], b);
    set(layerCollisions[b], a);
}

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
    return parseProject(*text, projectFile);
}

core::Result<Project> parseProject(std::string_view text, const std::filesystem::path& projectFile)
{
    const core::Result<serialization::TextDocument> document = serialization::parseText(text);
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
    project.file = absoluteNormal(projectFile);
    project.root = project.file.parent_path();
    const serialization::TextValue* const name = section->findAttribute("name");
    const std::string* const nameText = name != nullptr ? serialization::asString(*name) : nullptr;
    project.name = nameText != nullptr ? *nameText : core::toUtf8(projectFile.stem());
    const serialization::TextValue* const startup = section->findAttribute("startup_scene");
    if (const std::string* const startupText = startup != nullptr ? serialization::asString(*startup) : nullptr)
    {
        project.startupScene = *startupText;
    }
    readPhysics(*document, project.physics);
    readAudio(*document, project.audio);
    readWindowAndExport(*document, project);
    return project;
}

core::Result<void> saveProject(const Project& project)
{
    return core::writeTextFile(project.file, writeProjectText(project));
}

std::string writeProjectText(const Project& project)
{
    serialization::TextDocument document;
    serialization::TextSection& section = document.sections.emplace_back();
    section.type = "project";
    section.attributes.push_back({"format", serialization::TextValue(projectFormatVersion)});
    section.attributes.push_back({"name", serialization::TextValue(project.name)});
    if (!project.startupScene.empty())
    {
        section.attributes.push_back({"startup_scene", serialization::TextValue(project.startupScene)});
    }

    // Physics settings are written only when they differ from the defaults.
    const PhysicsSettings defaults;
    if (project.physics.gravity != defaults.gravity)
    {
        serialization::TextSection& physics = document.sections.emplace_back();
        physics.type = "physics";
        const math::Vec3 gravity = project.physics.gravity;
        physics.properties.push_back({"gravity", serialization::makeCall("vec3", {serialization::TextValue(static_cast<double>(gravity.x)),
                                                                                    serialization::TextValue(static_cast<double>(gravity.y)),
                                                                                    serialization::TextValue(static_cast<double>(gravity.z))})});
    }
    for (std::size_t index = 0; index < physicsLayerCount; ++index)
    {
        if (project.physics.layerNames[index] == defaults.layerNames[index] &&
            project.physics.layerCollisions[index] == defaults.layerCollisions[index])
        {
            continue;
        }
        serialization::TextSection& layer = document.sections.emplace_back();
        layer.type = "physics_layer";
        layer.attributes.push_back({"index", serialization::TextValue(static_cast<std::int64_t>(index))});
        layer.attributes.push_back({"name", serialization::TextValue(project.physics.layerNames[index])});
        layer.properties.push_back({"collides", serialization::TextValue(static_cast<std::int64_t>(project.physics.layerCollisions[index]))});
    }

    // Audio settings too.
    const AudioSettings audioDefaults;
    if (project.audio.masterVolume != audioDefaults.masterVolume)
    {
        serialization::TextSection& audio = document.sections.emplace_back();
        audio.type = "audio";
        audio.attributes.push_back({"master_volume", serialization::TextValue(static_cast<double>(project.audio.masterVolume))});
    }
    for (std::size_t index = 0; index < audioGroupCount; ++index)
    {
        if (project.audio.groupNames[index] == audioDefaults.groupNames[index] &&
            project.audio.groupVolumes[index] == audioDefaults.groupVolumes[index])
        {
            continue;
        }
        serialization::TextSection& group = document.sections.emplace_back();
        group.type = "audio_group";
        group.attributes.push_back({"index", serialization::TextValue(static_cast<std::int64_t>(index))});
        group.attributes.push_back({"name", serialization::TextValue(project.audio.groupNames[index])});
        group.attributes.push_back({"volume", serialization::TextValue(static_cast<double>(project.audio.groupVolumes[index]))});
    }

    if (project.window != WindowSettings{})
    {
        serialization::TextSection& window = document.sections.emplace_back();
        window.type = "window";
        const WindowSettings& settings = project.window;
        window.attributes.push_back({"width", serialization::TextValue(static_cast<std::int64_t>(settings.width))});
        window.attributes.push_back({"height", serialization::TextValue(static_cast<std::int64_t>(settings.height))});
        window.attributes.push_back({"fullscreen", serialization::TextValue(settings.fullscreen)});
        window.attributes.push_back({"vsync", serialization::TextValue(settings.vsync)});
        window.attributes.push_back({"max_frame_rate", serialization::TextValue(static_cast<std::int64_t>(settings.maxFrameRate))});
        if (settings.icon.isValid())
        {
            window.attributes.push_back({"icon", serialization::makeCall("asset", {serialization::TextValue(settings.icon.uuid.toString())})});
        }
    }

    const ExportSettings exportDefaults;
    const ExportSettings& exportSettings = project.exportSettings;
    if (exportSettings.output != exportDefaults.output || exportSettings.configuration != exportDefaults.configuration)
    {
        serialization::TextSection& exportSection = document.sections.emplace_back();
        exportSection.type = "export";
        exportSection.attributes.push_back({"output", serialization::TextValue(exportSettings.output)});
        exportSection.attributes.push_back({"configuration", serialization::TextValue(exportSettings.configuration)});
    }
    for (const std::string& scene : exportSettings.scenes)
    {
        serialization::TextSection& sceneSection = document.sections.emplace_back();
        sceneSection.type = "export_scene";
        sceneSection.attributes.push_back({"path", serialization::TextValue(scene)});
    }
    for (const std::string& folder : exportSettings.includeFolders)
    {
        serialization::TextSection& folderSection = document.sections.emplace_back();
        folderSection.type = "export_folder";
        folderSection.attributes.push_back({"path", serialization::TextValue(folder)});
    }
    return serialization::writeText(document);
}

core::Result<Project> createProject(const std::filesystem::path& directory, std::string_view name)
{
    const std::filesystem::path projectFile =
        directory / core::pathFromUtf8(std::string(name) + std::string(projectExtension));
    Project created{.name = std::string(name), .root = absoluteNormal(directory), .file = absoluteNormal(projectFile)};
    if (core::Result<void> written = saveProject(created); !written)
    {
        return std::unexpected(written.error());
    }

    core::Result<Project> project = loadProject(projectFile);
    if (!project)
    {
        return project;
    }
    std::error_code error;
    for (const std::filesystem::path& folder : {project->assetsDirectory(), project->codeDirectory()})
    {
        std::filesystem::create_directories(folder, error);
        if (error)
        {
            return core::makeError(core::ErrorCode::Io, "cannot create '{}': {}",
                                   core::toUtf8(folder), error.message());
        }
    }
    return project;
}

} // namespace devex::asset
