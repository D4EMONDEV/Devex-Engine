#include <devex/asset/Addon.hpp>
#include <devex/asset/Artifact.hpp>
#include <devex/asset/IrradianceVolumeData.hpp>
#include <devex/asset/NavMeshData.hpp>
#include <devex/asset/Package.hpp>
#include <devex/asset/Project.hpp>
#include <devex/asset/ReflectionProbeData.hpp>
#include <devex/asset/TranslationData.hpp>
#include <devex/core/BuildInfo.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Path.hpp>
#include <devex/core/Uuid.hpp>
#include <devex/serialization/Text.hpp>

#include <algorithm>
#include <cctype>
#include <format>
#include <string>
#include <system_error>
#include <utility>

namespace devex::asset {

std::string_view otherDesktopSystem() noexcept
{
#if defined(_WIN32)
    return "linux";
#elif defined(__linux__) && !defined(__ANDROID__)
    return "windows";
#else
    return {};
#endif
}
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

// The sorting layers in the order of their sections, "Default" among them.
void readSorting(const serialization::TextDocument& document, SortingSettings& sorting)
{
    std::vector<std::string> layers;
    for (const serialization::TextSection& section : document.sections)
    {
        const serialization::TextValue* const name = section.findAttribute("name");
        const std::string* const nameText = name != nullptr ? serialization::asString(*name) : nullptr;
        if (section.type == "sorting_layer" && nameText != nullptr && !nameText->empty() &&
            std::ranges::find(layers, *nameText) == layers.end())
        {
            layers.push_back(*nameText);
        }
    }
    if (layers.empty())
    {
        return;
    }
    if (std::ranges::find(layers, "Default") == layers.end())
    {
        layers.insert(layers.begin(), "Default");
    }
    sorting.layers = std::move(layers);
}

// Contexts in the order of their sections, which replace the default one; actions with their
// bindings, written "key:W up" in a list.
void readInput(const serialization::TextDocument& document, InputSettings& input)
{
    bool contextsRead = false;
    for (const serialization::TextSection& section : document.sections)
    {
        const serialization::TextValue* const name = section.findAttribute("name");
        const std::string* const nameText = name != nullptr ? serialization::asString(*name) : nullptr;
        if (section.type == "input_context" && nameText != nullptr && !nameText->empty())
        {
            if (!std::exchange(contextsRead, true))
            {
                input.contexts.clear();
            }
            InputContext& context = input.contexts.emplace_back(InputContext{.name = *nameText});
            if (const serialization::TextValue* const active = section.findAttribute("active"))
            {
                context.activeAtStart = serialization::asBool(*active).value_or(true);
            }
        }
        else if (section.type == "input_action" && nameText != nullptr && !nameText->empty())
        {
            InputAction& action = input.actions.emplace_back(InputAction{.name = *nameText});
            if (const serialization::TextValue* const kind = section.findAttribute("kind"))
            {
                const std::string* const kindText = serialization::asString(*kind);
                action.kind = kindText != nullptr ? parseInputActionKind(*kindText).value_or(InputActionKind::Button)
                                                  : InputActionKind::Button;
            }
            if (const serialization::TextValue* const context = section.findAttribute("context"))
            {
                if (const std::string* const contextText = serialization::asString(*context))
                {
                    action.context = *contextText;
                }
            }
            action.deadZone = std::clamp(numberAttribute(section, "dead_zone").value_or(0.0f), 0.0f, 0.99f);
            const serialization::TextValue* const bindings = section.findProperty("bindings");
            const serialization::TextCall* const list = bindings != nullptr ? serialization::asCall(*bindings, "list") : nullptr;
            for (const serialization::TextValue& entry : list != nullptr ? list->arguments : std::vector<serialization::TextValue>{})
            {
                const std::string* const text = serialization::asString(entry);
                if (text == nullptr || text->empty())
                {
                    continue;
                }
                const std::size_t space = text->find(' ');
                InputBinding binding{.input = text->substr(0, space)};
                if (space != std::string::npos)
                {
                    binding.direction = parseInputDirection(std::string_view(*text).substr(space + 1))
                                            .value_or(InputDirection::Positive);
                }
                action.bindings.push_back(std::move(binding));
            }
        }
    }
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
            readBool(section, "desktop_entry", window.desktopEntry);
            readBool(section, "quit_on_back", window.quitOnBack);
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
            if (const std::string* const output = serialization::stringAttribute(section, "output"))
            {
                project.exportSettings.output = *output;
            }
            if (const std::string* const configuration = serialization::stringAttribute(section, "configuration"))
            {
                project.exportSettings.configuration = *configuration;
            }
            if (const std::string* const otherOutput = serialization::stringAttribute(section, "other_output"))
            {
                project.exportSettings.otherOutput = *otherOutput;
            }
            if (const auto* const terminal = section.findAttribute("show_terminal"))
            {
                project.exportSettings.showTerminal = serialization::asBool(*terminal).value_or(false);
            }
        }
        else if (section.type == "export_android")
        {
            AndroidExportSettings& android = project.exportSettings.android;
            const auto text = [&](const char* key, std::string& value) {
                if (const std::string* const found = serialization::stringAttribute(section, key))
                {
                    value = *found;
                }
            };
            const auto flag = [&](const char* key, bool& value) {
                if (const serialization::TextValue* const found = section.findAttribute(key))
                {
                    value = serialization::asBool(*found).value_or(value);
                }
            };
            text("platform", project.exportSettings.platform);
            text("output", android.output);
            text("package", android.package);
            text("version_name", android.versionName);
            if (const serialization::TextValue* const code = section.findAttribute("version_code"))
            {
                android.versionCode = static_cast<std::int32_t>(serialization::asInteger(*code).value_or(android.versionCode));
            }
            text("orientation", android.orientation);
            flag("arm64", android.arm64);
            flag("x86_64", android.x86_64);
            text("format", android.format);
            flag("release", android.release);
            flag("install", android.install);
        }
        else if (section.type == "export_scene" || section.type == "export_folder")
        {
            if (const std::string* const path = serialization::stringAttribute(section, "path"); path != nullptr && !path->empty())
            {
                (section.type == "export_scene" ? project.exportSettings.scenes : project.exportSettings.includeFolders)
                    .push_back(*path);
            }
        }
        else if (section.type == "localization")
        {
            if (const std::string* const fallback = serialization::stringAttribute(section, "fallback"); fallback != nullptr && isLanguageCode(*fallback))
            {
                project.localization.fallbackLanguage = normalizeLanguage(*fallback);
            }
            if (const std::string* const test = serialization::stringAttribute(section, "test"); test != nullptr && isLanguageCode(*test))
            {
                project.localization.testLanguage = normalizeLanguage(*test);
            }
        }
    }
}

} // namespace

std::string_view toString(InputActionKind kind) noexcept
{
    switch (kind)
    {
    case InputActionKind::Button:
        return "button";
    case InputActionKind::Axis:
        return "axis";
    case InputActionKind::Vector:
        return "vector";
    }
    return "button";
}

std::optional<InputActionKind> parseInputActionKind(std::string_view text) noexcept
{
    for (const InputActionKind kind : {InputActionKind::Button, InputActionKind::Axis, InputActionKind::Vector})
    {
        if (toString(kind) == text)
        {
            return kind;
        }
    }
    return std::nullopt;
}

std::string_view toString(InputDirection direction) noexcept
{
    switch (direction)
    {
    case InputDirection::Positive:
        return "positive";
    case InputDirection::Negative:
        return "negative";
    case InputDirection::Up:
        return "up";
    case InputDirection::Down:
        return "down";
    case InputDirection::Left:
        return "left";
    case InputDirection::Right:
        return "right";
    }
    return "positive";
}

std::optional<InputDirection> parseInputDirection(std::string_view text) noexcept
{
    for (const InputDirection direction : {InputDirection::Positive, InputDirection::Negative, InputDirection::Up,
                                           InputDirection::Down, InputDirection::Left, InputDirection::Right})
    {
        if (toString(direction) == text)
        {
            return direction;
        }
    }
    return std::nullopt;
}

std::int32_t SortingSettings::rank(std::string_view layer) const noexcept
{
    const auto position = [&](std::string_view name) {
        const auto found = std::ranges::find(layers, name);
        return found != layers.end() ? static_cast<std::int32_t>(found - layers.begin()) : -1;
    };
    const std::int32_t defaultPosition = std::max(position("Default"), 0);
    const std::int32_t found = layer.empty() ? -1 : position(layer);
    return found < 0 ? 0 : found - defaultPosition;
}

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

core::Result<std::filesystem::path> Project::newFilePath(std::string_view folder, std::string_view fileName,
                                                       ContentRoot content) const
{
    const std::string_view base = content == ContentRoot::Code ? "res://code" : "res://assets";
    if (fileName.empty() || fileName.front() == '.' || fileName.front() == ' ' ||
        fileName.back() == '.' || fileName.back() == ' ' ||
        std::ranges::any_of(fileName, [](unsigned char ch) { return ch < 32 || std::string_view("<>:\"/\\|?*").contains(static_cast<char>(ch)); }))
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "Enter a file name without path separators or reserved characters.");
    }
    std::string stem(fileName.substr(0, fileName.find('.')));
    std::ranges::transform(stem, stem.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" ||
        (stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) && stem.back() >= '1' && stem.back() <= '9'))
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "This name is reserved by the operating system.");
    }
    const auto directory = absolutePath(folder);
    const std::string resource = directory ? resourcePath(*directory) : std::string{};
    if (!directory || (resource != base && !resource.starts_with(std::string(base) + "/")))
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "Choose a folder inside {}.", base);
    }
    const auto relative = directory->lexically_relative(content == ContentRoot::Code ? codeDirectory() : assetsDirectory());
    for (const auto& part : relative)
    {
        const auto partName = core::toUtf8(part);
        if (partName != "." && (partName.starts_with('.') || (content == ContentRoot::Code && (partName == "bin" || partName == "obj"))))
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "Choose a content folder, outside hidden and build directories.");
        }
    }
    std::error_code error;
    const auto canonicalRoot = std::filesystem::canonical(root, error);
    if (error)
    {
        return core::makeError(core::ErrorCode::Io, "Cannot read the project folder: {}", error.message());
    }
    const auto canonicalDirectory = std::filesystem::canonical(*directory, error);
    if (error || !std::filesystem::is_directory(canonicalDirectory, error))
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "The destination folder does not exist or cannot be read.");
    }
    const auto canonicalRelative = canonicalDirectory.lexically_relative(canonicalRoot / (content == ContentRoot::Code ? "code" : "assets"));
    if (escapesBase(canonicalRelative))
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "The destination points outside {}.", base);
    }
    const auto destination = *directory / core::pathFromUtf8(fileName);
    const auto occupied = [&](const std::filesystem::path& path) {
        const auto status = std::filesystem::symlink_status(path, error);
        if (error == std::errc::no_such_file_or_directory)
        {
            error.clear();
            return false;
        }
        return error || status.type() != std::filesystem::file_type::not_found;
    };
    auto meta = destination;
    meta += ".dvxmeta";
    if (occupied(destination) || (content == ContentRoot::Assets && occupied(meta)))
    {
        return core::makeError(error ? core::ErrorCode::Io : core::ErrorCode::AlreadyExists,
                               "The file name is already used or cannot be accessed.");
    }
    return destination;
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
    if (const std::string* const engine = serialization::stringAttribute(*section, "engine"))
    {
        project.engineVersion = *engine;
    }
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
    readSorting(*document, project.sorting);
    readWindowAndExport(*document, project);
    readInput(*document, project.input);
    for (const auto& entry : document->sections)
    {
        if (entry.type != "global_scene")
            continue;
        GlobalScene global;
        if (const auto* value = entry.findAttribute("name"))
            if (const auto* globalName = serialization::asString(*value))
                global.name = *globalName;
        if (const auto* value = entry.findAttribute("path"))
            if (const auto* globalPath = serialization::asString(*value))
                global.path = *globalPath;
        if (const auto* value = entry.findAttribute("enabled"))
            global.enabled = serialization::asBool(*value).value_or(true);
        project.globals.push_back(std::move(global));
    }
    if (auto valid = validateGlobals(project.globals); !valid)
        return std::unexpected(valid.error());
    for (const auto& entry : document->sections)
    {
        if (entry.type != "addon")
            continue;
        const auto* id = entry.findAttribute("id");
        const auto* addonId = id != nullptr ? serialization::asString(*id) : nullptr;
        if (addonId == nullptr || !validAddonId(*addonId) ||
            std::ranges::find(project.addons, *addonId, &AddonSetting::id) != project.addons.end())
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "Invalid or duplicate addon id");
        const auto* enabled = entry.findAttribute("enabled");
        project.addons.push_back(
            {*addonId, enabled != nullptr && serialization::asBool(*enabled).value_or(false)});
    }
    return project;
}

core::Result<void> validateGlobals(std::span<const GlobalScene> globals)
{
    std::vector<std::string_view> names;
    for (const auto& global : globals)
    {
        const auto letter = [](char c)
        { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; };
        if (global.name.empty() || !letter(global.name.front()) ||
            !std::ranges::all_of(global.name,
                                 [&](char c) { return letter(c) || (c >= '0' && c <= '9'); }))
            return core::makeError(
                core::ErrorCode::InvalidArgument,
                "Global names use letters, digits and underscores, and cannot start with a digit");
        if (std::ranges::find(names, global.name) != names.end())
            return core::makeError(core::ErrorCode::InvalidArgument, "Another global is named {}",
                                   global.name);
        names.push_back(global.name);
        if (!global.path.starts_with("res://assets/") || !global.path.ends_with(".dvxscene") ||
            global.path.find("/../") != std::string::npos ||
            global.path.find('\\') != std::string::npos)
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "Global {} needs a scene under res://assets/", global.name);
    }
    return {};
}

ProjectAge projectAge(const Project& project) noexcept
{
    const int order = core::compareVersions(project.engineVersion, core::version());
    return order < 0 ? ProjectAge::Older : (order > 0 ? ProjectAge::Newer : ProjectAge::Current);
}

core::Result<std::filesystem::path> backUpProject(const Project& project)
{
    namespace fs = std::filesystem;
    const std::string version = project.engineVersion.empty() ? std::string("older") : project.engineVersion;
    std::error_code error;
    fs::path destination = project.cacheDirectory() / "backups" / version;
    for (int attempt = 2; fs::exists(destination, error); ++attempt)
    {
        destination = project.cacheDirectory() / "backups" / std::format("{}-{}", version, attempt);
    }
    // What is baked, imported or built comes back by itself, unlike a terrain that was sculpted.
    const auto isMadeByHand = [](const fs::path& file) {
        const std::string extension = core::toUtf8(file.extension());
        return extension.starts_with(".dvx") && extension != artifactExtension && extension != packageExtension && extension != navMeshExtension &&
               extension != reflectionProbeExtension && extension != irradianceVolumeExtension;
    };
    const auto isGenerated = [](const fs::path& folder) {
        return core::toUtf8(folder.filename()).starts_with('.') || folder == fs::path("out") || folder == fs::path("code") / "bin" || folder == fs::path("code") / "obj";
    };
    fs::recursive_directory_iterator walk(project.root, fs::directory_options::skip_permission_denied, error);
    const fs::recursive_directory_iterator end;
    while (!error && walk != end)
    {
        const fs::path file = walk->path();
        const fs::path relative = file.lexically_relative(project.root);
        std::error_code ignored;
        if (walk->is_directory(ignored))
        {
            if (isGenerated(relative))
            {
                walk.disable_recursion_pending();
            }
        }
        else if (walk->is_regular_file(ignored) && isMadeByHand(file))
        {
            const fs::path target = destination / relative;
            fs::create_directories(target.parent_path(), error);
            if (!error)
            {
                fs::copy_file(file, target, fs::copy_options::overwrite_existing, error);
            }
            if (error)
            {
                return core::makeError(core::ErrorCode::Io, "cannot copy '{}': {}", core::toUtf8(file), error.message());
            }
        }
        walk.increment(error);
    }
    if (error)
    {
        return core::makeError(core::ErrorCode::Io, "cannot read the folder of the project: {}", error.message());
    }
    return destination;
}

core::Result<std::filesystem::path> upgradeProject(Project& project)
{
    core::Result<std::filesystem::path> backup = backUpProject(project);
    if (!backup)
    {
        return backup;
    }
    project.engineVersion = std::string(core::version());
    if (core::Result<void> saved = saveProject(project); !saved)
    {
        return std::unexpected(saved.error());
    }
    return backup;
}

core::Result<void> saveProject(const Project& project)
{
    if (auto valid = validateGlobals(project.globals); !valid)
        return valid;
    std::vector<std::string_view> ids;
    for (const auto& addon : project.addons)
    {
        if (!validAddonId(addon.id) || std::ranges::find(ids, addon.id) != ids.end())
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "Invalid or duplicate addon id");
        ids.push_back(addon.id);
    }
    return core::writeTextFile(project.file, writeProjectText(project));
}

std::string writeProjectText(const Project& project)
{
    serialization::TextDocument document;
    serialization::TextSection& section = document.sections.emplace_back();
    section.type = "project";
    section.attributes.push_back({"format", serialization::TextValue(projectFormatVersion)});
    // Never going back: an older engine that saves the project of a newer one does not make it its own.
    const bool newer = core::compareVersions(project.engineVersion, core::version()) > 0;
    section.attributes.push_back({"engine", serialization::TextValue(newer ? project.engineVersion : std::string(core::version()))});
    section.attributes.push_back({"name", serialization::TextValue(project.name)});
    if (!project.startupScene.empty())
    {
        section.attributes.push_back({"startup_scene", serialization::TextValue(project.startupScene)});
    }

    // Physics settings are written only when they differ from the defaults.
    for (const auto& addon : project.addons)
    {
        auto& entry = document.sections.emplace_back();
        entry.type = "addon";
        entry.attributes = {{"id", addon.id}, {"enabled", addon.enabled}};
    }
    for (const auto& global : project.globals)
    {
        auto& entry = document.sections.emplace_back();
        entry.type = "global_scene";
        entry.attributes.push_back({"name", serialization::TextValue(global.name)});
        entry.attributes.push_back({"path", serialization::TextValue(global.path)});
        entry.attributes.push_back({"enabled", serialization::TextValue(global.enabled)});
    }
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
        // Written when turned off only: the projects that keep it as it is keep their files.
        if (!settings.desktopEntry)
        {
            window.attributes.push_back({"desktop_entry", serialization::TextValue(false)});
        }
        if (!settings.quitOnBack)
        {
            window.attributes.push_back({"quit_on_back", serialization::TextValue(false)});
        }
    }

    if (project.sorting != SortingSettings{})
    {
        for (const std::string& layer : project.sorting.layers)
        {
            serialization::TextSection& layerSection = document.sections.emplace_back();
            layerSection.type = "sorting_layer";
            layerSection.attributes.push_back({"name", serialization::TextValue(layer)});
        }
    }

    if (project.localization != LocalizationSettings{})
    {
        serialization::TextSection& localization = document.sections.emplace_back();
        localization.type = "localization";
        localization.attributes.push_back({"fallback", serialization::TextValue(project.localization.fallbackLanguage)});
        if (!project.localization.testLanguage.empty())
        {
            localization.attributes.push_back({"test", serialization::TextValue(project.localization.testLanguage)});
        }
    }

    const ExportSettings exportDefaults;
    const ExportSettings& exportSettings = project.exportSettings;
    if (exportSettings.output != exportDefaults.output || exportSettings.configuration != exportDefaults.configuration ||
        exportSettings.otherOutput != exportDefaults.otherOutput || exportSettings.showTerminal != exportDefaults.showTerminal)
    {
        serialization::TextSection& exportSection = document.sections.emplace_back();
        exportSection.type = "export";
        exportSection.attributes.push_back({"output", serialization::TextValue(exportSettings.output)});
        exportSection.attributes.push_back({"configuration", serialization::TextValue(exportSettings.configuration)});
        if (exportSettings.otherOutput != exportDefaults.otherOutput)
        {
            exportSection.attributes.push_back({"other_output", serialization::TextValue(exportSettings.otherOutput)});
        }
        if (exportSettings.showTerminal)
        {
            exportSection.attributes.push_back({"show_terminal", true});
        }
    }
    if (exportSettings.platform != exportDefaults.platform || exportSettings.android != exportDefaults.android)
    {
        const AndroidExportSettings& android = exportSettings.android;
        serialization::TextSection& entry = document.sections.emplace_back();
        entry.type = "export_android";
        entry.attributes = {{"platform", exportSettings.platform},
                            {"output", android.output},
                            {"package", android.package},
                            {"version_name", android.versionName},
                            {"version_code", std::int64_t{android.versionCode}},
                            {"orientation", android.orientation},
                            {"arm64", android.arm64},
                            {"x86_64", android.x86_64},
                            {"format", android.format},
                            {"release", android.release},
                            {"install", android.install}};
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

    if (project.input.contexts != InputSettings{}.contexts)
    {
        for (const InputContext& context : project.input.contexts)
        {
            serialization::TextSection& contextSection = document.sections.emplace_back();
            contextSection.type = "input_context";
            contextSection.attributes.push_back({"name", serialization::TextValue(context.name)});
            contextSection.attributes.push_back({"active", serialization::TextValue(context.activeAtStart)});
        }
    }
    for (const InputAction& action : project.input.actions)
    {
        serialization::TextSection& actionSection = document.sections.emplace_back();
        actionSection.type = "input_action";
        actionSection.attributes.push_back({"name", serialization::TextValue(action.name)});
        actionSection.attributes.push_back({"kind", serialization::TextValue(std::string(toString(action.kind)))});
        if (!action.context.empty())
        {
            actionSection.attributes.push_back({"context", serialization::TextValue(action.context)});
        }
        if (action.deadZone > 0.0f)
        {
            // The shortest text of the float, not of the double it widens to: 0.1 rather than 0.10000000149.
            actionSection.attributes.push_back({"dead_zone", serialization::TextValue(std::stod(std::format("{}", action.deadZone)))});
        }
        std::vector<serialization::TextValue> bindings;
        for (const InputBinding& binding : action.bindings)
        {
            bindings.emplace_back(binding.direction == InputDirection::Positive
                                      ? binding.input
                                      : binding.input + " " + std::string(toString(binding.direction)));
        }
        actionSection.properties.push_back({"bindings", serialization::makeCall("list", std::move(bindings))});
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

bool validAndroidPackage(std::string_view package)
{
    if (package.find('.') == std::string_view::npos)
    {
        return false;
    }
    bool start = true;
    for (const char character : package)
    {
        if (character == '.')
        {
            if (start)
            {
                return false;
            }
            start = true;
            continue;
        }
        const bool letter = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
        const bool other = (character >= '0' && character <= '9') || character == '_';
        if (start ? !letter : !(letter || other))
        {
            return false;
        }
        start = false;
    }
    return !start;
}

std::string androidPackageFromName(std::string_view name)
{
    std::string word;
    for (const char character : name)
    {
        if ((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9'))
        {
            word += character;
        }
        else if (character >= 'A' && character <= 'Z')
        {
            word += static_cast<char>(character - 'A' + 'a');
        }
    }
    if (word.empty() || (word.front() >= '0' && word.front() <= '9'))
    {
        word.insert(0, "game");
    }
    return "com.devex." + word;
}

namespace {

[[nodiscard]] std::filesystem::path androidSigningFile(const Project& project)
{
    return project.cacheDirectory() / "android-signing.dvx";
}

} // namespace

AndroidSigning readAndroidSigning(const Project& project)
{
    AndroidSigning signing;
    const core::Result<std::string> contents = core::readTextFile(androidSigningFile(project));
    if (!contents)
    {
        return signing;
    }
    const auto document = serialization::parseText(*contents);
    if (!document || document->sections.empty())
    {
        return signing;
    }
    const serialization::TextSection& section = document->sections.front();
    if (const std::string* const keystore = serialization::stringAttribute(section, "keystore"))
    {
        signing.keystore = core::pathFromUtf8(*keystore);
    }
    if (const std::string* const alias = serialization::stringAttribute(section, "alias"))
    {
        signing.alias = *alias;
    }
    if (const std::string* const password = serialization::stringAttribute(section, "store_password"))
    {
        signing.storePassword = *password;
    }
    if (const std::string* const password = serialization::stringAttribute(section, "key_password"))
    {
        signing.keyPassword = *password;
    }
    return signing;
}

core::Result<void> writeAndroidSigning(const Project& project, const AndroidSigning& signing)
{
    // Whatever the project ignores, its passwords stay out of version control.
    if (core::Result<void> ignored = core::writeTextFile(project.cacheDirectory() / ".gitignore", "*\n"); !ignored)
    {
        return ignored;
    }
    serialization::TextDocument document;
    serialization::TextSection& section = document.sections.emplace_back();
    section.type = "android_signing";
    section.attributes = {{"keystore", core::toUtf8(signing.keystore)},
                          {"alias", signing.alias},
                          {"store_password", signing.storePassword},
                          {"key_password", signing.keyPassword}};
    return core::writeTextFile(androidSigningFile(project), serialization::writeText(document));
}

std::string gameFileName(std::string_view name)
{
    std::string file;
    for (const char character : name)
    {
        const auto byte = static_cast<unsigned char>(character);
        const bool forbidden = byte < 0x20 || std::string_view("<>:\"/\\|?*").find(character) != std::string_view::npos;
        file.push_back(forbidden ? '_' : character);
    }
    const auto trimmed = [](char character) { return character == ' ' || character == '.'; };
    while (!file.empty() && trimmed(file.back()))
    {
        file.pop_back();
    }
    while (!file.empty() && trimmed(file.front()))
    {
        file.erase(file.begin());
    }
    return file.empty() ? std::string("Game") : file;
}

} // namespace devex::asset
