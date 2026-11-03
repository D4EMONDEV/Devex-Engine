#include <devex/asset/Addon.hpp>

#include <devex/asset/import/MetaFile.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Path.hpp>
#include <devex/core/Uuid.hpp>
#include <devex/serialization/Text.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <map>
#include <utility>

namespace devex::asset
{
namespace
{
using Path = std::filesystem::path;

std::string attribute(const serialization::TextSection& section, std::string_view name)
{
    const auto* value = section.findAttribute(name);
    const auto* text = value != nullptr ? serialization::asString(*value) : nullptr;
    return text != nullptr ? *text : std::string{};
}

bool hidden(const Path& path, bool code)
{
    const auto name = core::toUtf8(path.filename());
    return name.starts_with('.') ||
           (code && (name == "bin" || name == "obj" || name == "CMakeFiles" ||
                     name.starts_with("cmake-build")));
}

struct Placement
{
    Path destination;
    bool code = false;
};

// Copies the regular files of a folder to where `place` says from their relative paths. Links and
// paths leaving the folder are refused, and what only tools make is left behind.
template <typename Place>
core::Result<void> copyFiles(const Path& source, Place place)
{
    std::error_code error;
    const auto root = std::filesystem::canonical(source, error);
    if (error)
        return core::makeError(core::ErrorCode::Io, "Cannot open the addon: {}", error.message());
    for (std::filesystem::recursive_directory_iterator entry(root, error), end;
         !error && entry != end; entry.increment(error))
    {
        const auto relative = entry->path().lexically_relative(root);
        const Placement placed = place(relative);
        if (hidden(entry->path(), placed.code))
        {
            if (entry->is_directory(error))
                entry.disable_recursion_pending();
            continue;
        }
        const auto status = entry->symlink_status(error);
        const auto resolved = std::filesystem::canonical(entry->path(), error);
        const auto contained = resolved.lexically_relative(root);
        if (error || std::filesystem::is_symlink(status) || contained.empty() ||
            *contained.begin() == "..")
            return core::makeError(
                core::ErrorCode::InvalidArgument,
                "Addon packages cannot contain links or paths outside their folder");
        if (!std::filesystem::is_directory(status) && !std::filesystem::is_regular_file(status))
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "An addon can contain only regular files and folders");
        if (std::filesystem::is_directory(status))
            std::filesystem::create_directories(placed.destination, error);
        else
        {
            std::filesystem::create_directories(placed.destination.parent_path(), error);
            if (!error)
                std::filesystem::copy_file(entry->path(), placed.destination,
                                           std::filesystem::copy_options::none, error);
        }
    }
    if (error)
        return core::makeError(core::ErrorCode::Io, "Cannot copy the addon: {}", error.message());
    return {};
}

core::Result<void> copyPackage(const Path& source, const Path& staged)
{
    std::error_code error;
    std::filesystem::create_directories(staged / "content", error);
    return copyFiles(source,
                     [&](const Path& relative)
                     {
                         const bool code = *relative.begin() == "code";
                         return Placement{code ? staged / "code" / relative.lexically_relative("code")
                                               : staged / "content" / relative,
                                          code};
                     });
}

Path addonContent(const Project& project, std::string_view id)
{
    return project.assetsDirectory() / "addons" / core::pathFromUtf8(id);
}

Path addonCode(const Project& project, std::string_view id)
{
    return project.codeDirectory() / "addons" / core::pathFromUtf8(id);
}

bool inside(const Path& path, const Path& folder)
{
    std::error_code error;
    const auto canonicalPath = std::filesystem::weakly_canonical(path, error);
    const auto relative = canonicalPath.lexically_relative(std::filesystem::weakly_canonical(folder, error));
    return !error && !relative.empty() && *relative.begin() != "..";
}

// Calls `visit` with the regular files under a folder, past what only tools make and `skipped`.
template <typename Visit>
void forEachFile(const Path& root, bool code, std::span<const Path> skipped, Visit visit)
{
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator entry(root, error), end;
         !error && entry != end; entry.increment(error))
    {
        const bool directory = entry->is_directory(error);
        if (hidden(entry->path(), code) || std::ranges::contains(skipped, entry->path()))
        {
            if (directory)
                entry.disable_recursion_pending();
            continue;
        }
        if (!directory && entry->is_regular_file(error))
            visit(entry->path());
    }
}

// The identifiers a .dvxmeta file gives.
std::vector<AssetId> metaIds(const Path& file)
{
    const auto contents = core::readTextFile(file);
    if (!contents)
        return {};
    const auto meta = parseMetaFile(*contents);
    if (!meta)
        return {};
    std::vector<AssetId> ids{meta->id};
    for (const auto& sub : meta->subAssets)
        ids.push_back(sub.id);
    return ids;
}

bool isNativeSource(const Path& file)
{
    const auto extension = file.extension();
    return extension == ".cpp" || extension == ".cc" || extension == ".cxx" ||
           extension == ".hpp" || extension == ".h";
}

// Identifiers used twice would make assets take each other's place, and a second entry point would
// stop the game code from linking: both are better refused than found out later.
std::vector<std::string> findConflicts(const Project& project, const Path& source,
                                       std::string_view id)
{
    std::vector<std::string> conflicts;
    if (inside(source, project.assetsDirectory()) || inside(source, project.codeDirectory()))
        conflicts.emplace_back(
            "This folder is part of the project: choose a package outside assets and code");
    std::map<AssetId, std::string> known;
    const std::array own{addonContent(project, id), addonCode(project, id)};
    for (const auto& root : {project.assetsDirectory(), project.codeDirectory()})
        forEachFile(root, false, own,
                    [&](const Path& file)
                    {
                        if (file.extension() != metaExtension)
                            return;
                        auto asset = file;
                        asset.replace_extension();
                        for (const AssetId each : metaIds(file))
                            known.emplace(each, project.resourcePath(asset));
                    });
    forEachFile(source, false, {},
                [&](const Path& file)
                {
                    const auto relative = file.lexically_relative(source);
                    if (file.extension() == metaExtension)
                    {
                        auto asset = relative;
                        asset.replace_extension();
                        const auto shown = core::toUtf8(asset);
                        for (const AssetId each : metaIds(file))
                            if (const auto [at, added] = known.emplace(each, shown); !added)
                                conflicts.push_back(std::format("{} has the same identifier as {}",
                                                                shown, at->second));
                    }
                    if (*relative.begin() == "code" && isNativeSource(file))
                        if (const auto text = core::readTextFile(file);
                            text && text->contains("DEVEX_GAME_MODULE"))
                            conflicts.push_back(std::format(
                                "{} declares DEVEX_GAME_MODULE: a project has only one entry point",
                                core::toUtf8(relative)));
                });
    return conflicts;
}

// Puts a staged package in place of what the project has of it, or leaves everything as it was.
core::Result<void> placeStaged(const Project& project, std::string_view id, const Path& staged)
{
    const auto content = addonContent(project, id);
    const auto code = addonCode(project, id);
    std::error_code error;
    std::filesystem::create_directories(content.parent_path(), error);
    if (!error && std::filesystem::exists(staged / "code", error))
        std::filesystem::create_directories(code.parent_path(), error);
    std::vector<std::pair<Path, Path>> moved;
    const auto move = [&](const Path& from, const Path& to)
    {
        if (error || !std::filesystem::exists(from, error))
            return;
        std::filesystem::rename(from, to, error);
        if (!error)
            moved.emplace_back(from, to);
    };
    move(content, staged / "old-content");
    move(code, staged / "old-code");
    move(staged / "content", content);
    move(staged / "code", code);
    if (!error)
        return {};
    const std::string reason = error.message();
    for (auto done = moved.rbegin(); done != moved.rend(); ++done)
    {
        std::error_code ignored;
        std::filesystem::rename(done->second, done->first, ignored);
    }
    return core::makeError(core::ErrorCode::Io, "Cannot install the addon: {}", reason);
}

// Stages a package and places it. Only this unique staging directory, and directories moved into
// it, are removed afterwards.
core::Result<AddonInfo> placePackage(const Project& project, const AddonPreview& preview,
                                     const Path& source)
{
    const auto staged =
        project.cacheDirectory() / "addon-install" / core::Uuid::generate().toString();
    auto placed = copyPackage(source, staged);
    if (placed)
        placed = placeStaged(project, preview.addon.id, staged);
    std::error_code ignored;
    std::filesystem::remove_all(staged, ignored);
    if (!placed)
        return std::unexpected(placed.error());
    auto addon = preview.addon;
    addon.directory = addonContent(project, addon.id);
    return addon;
}

// A name for C# from any name: "Scene Tools" gives "SceneTools".
std::string pascalName(std::string_view name)
{
    std::string pascal;
    bool start = true;
    for (const char c : name)
    {
        const auto byte = static_cast<unsigned char>(c);
        if (byte >= 0x80 || std::isalnum(byte) == 0)
        {
            start = true;
            continue;
        }
        pascal += start ? static_cast<char>(std::toupper(byte)) : c;
        start = false;
    }
    if (pascal.empty() || std::isdigit(static_cast<unsigned char>(pascal.front())) != 0)
        pascal.insert(0, "Addon");
    return pascal;
}

// Between the quotes of a C# string.
std::string csharpText(std::string_view text)
{
    std::string escaped;
    for (const char c : text)
    {
        if (c == '\\' || c == '"')
            escaped += '\\';
        escaped += c;
    }
    return escaped;
}

std::string exampleScript(std::string_view id, std::string_view name)
{
    return std::format(R"(using Devex;

namespace {0}Addon;

public static class Commands
{{
    // Shown in Editor > Addon Tools once the addon's Editor Tools are on. It runs on a copy of the
    // scene, which becomes one step of Ctrl+Z when it succeeds and is dropped when it throws.
    [EditorCommand("{1}", "Create Marker")]
    public static void CreateMarker(EditorContext context)
    {{
        Entity marker = context.CreateEntity("{2} Marker");
        marker.Transform.Position = Vec3.Zero;
    }}
}}
)",
                       pascalName(name), id, csharpText(name));
}

std::string readme(std::string_view id, const AddonDraft& draft)
{
    return std::format(R"(# {0}

{1}

## Where its files are

- `assets/addons/{2}`: this file, the manifest `addon.dvxaddon`, and the addon's scenes, textures
  and materials, with their `.dvxmeta` files.
- `code/addons/{2}`: its scripts, which compile with the project.

## Sharing it

Project Settings > Addons > Export to Folder writes a package, which Install from Folder takes in
another project. Raise the version in the manifest for each release, and add a `LICENSE` file with
the text of the license before sharing it.
)",
                       draft.name,
                       draft.description.empty() ? std::string_view{"What this addon does."}
                                                 : std::string_view{draft.description},
                       id);
}
} // namespace

bool validAddonId(std::string_view id) noexcept
{
    if (id.empty() || id.size() > 80 || id.front() < 'a' || id.front() > 'z' || id.back() == '.' ||
        id.contains(".."))
        return false;
    if (!std::ranges::all_of(id,
                             [](char c)
                             {
                                 return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                                        c == '-' || c == '_' || c == '.';
                             }))
        return false;
    const auto base = id.substr(0, id.find('.'));
    return base != "con" && base != "prn" && base != "aux" && base != "nul" &&
           !(base.size() == 4 && (base.starts_with("com") || base.starts_with("lpt")) &&
             base.back() >= '1' && base.back() <= '9');
}

core::Result<AddonInfo> readAddon(const Path& directory)
{
    auto contents = core::readTextFile(directory / addonManifest);
    if (!contents)
        return std::unexpected(contents.error());
    const auto document = serialization::parseText(*contents);
    if (!document || document->sections.size() != 1 || document->sections[0].type != "addon")
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "Expected one [addon] manifest section");
    const auto& section = document->sections[0];
    const auto* format = section.findAttribute("format");
    if (format == nullptr || serialization::asInteger(*format) != 1)
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "Unsupported addon manifest format");
    AddonInfo addon{.id = attribute(section, "id"),
                    .name = attribute(section, "name"),
                    .version = attribute(section, "version"),
                    .author = attribute(section, "author"),
                    .description = attribute(section, "description"),
                    .license = attribute(section, "license"),
                    .directory = directory};
    if (!validAddonId(addon.id) || addon.name.empty() || addon.version.empty())
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "An addon needs a valid lowercase id, a name and a version");
    return addon;
}

std::vector<AddonInfo> installedAddons(const Project& project)
{
    std::vector<AddonInfo> addons;
    std::error_code error;
    for (std::filesystem::directory_iterator entry(project.assetsDirectory() / "addons", error),
         end;
         !error && entry != end; entry.increment(error))
    {
        if (!entry->is_directory(error) || entry->is_symlink(error) || hidden(entry->path(), false))
            continue;
        const auto id = core::toUtf8(entry->path().filename());
        if (!validAddonId(id))
            continue;
        auto addon = readAddon(entry->path());
        if (!addon || addon->id != id)
            addons.push_back(
                {.id = id,
                 .name = id,
                 .directory = entry->path(),
                 .error = addon ? "The manifest id must match its folder" : addon.error().message});
        else
            addons.push_back(std::move(*addon));
    }
    std::ranges::sort(addons, {}, &AddonInfo::id);
    return addons;
}

bool addonEnabled(const Project& project, std::string_view id)
{
    const auto setting = std::ranges::find(project.addons, id, &AddonSetting::id);
    return setting != project.addons.end() && setting->enabled;
}

std::string addonIdFromName(std::string_view name)
{
    std::string id;
    for (const char c : name)
    {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x80 && std::isalnum(byte) != 0)
            id += static_cast<char>(std::tolower(byte));
        else if (!id.empty() && id.back() != '-')
            id += '-';
    }
    if (id.ends_with('-'))
        id.pop_back();
    if (!id.empty() && !validAddonId(id))
        id.insert(0, "addon-");
    return id;
}

std::optional<std::string> addonDraftProblem(const Project& project, const AddonDraft& draft)
{
    if (draft.name.find_first_not_of(' ') == std::string::npos)
        return "Give the addon a name";
    const std::string id = draft.id.empty() ? addonIdFromName(draft.name) : draft.id;
    if (!validAddonId(id))
        return "An id starts with a lowercase letter, then holds lowercase letters, digits, dashes, "
               "underscores and dots";
    if (draft.version.find_first_not_of(' ') == std::string::npos)
        return "Give the addon a version";
    std::error_code error;
    if (std::filesystem::exists(addonContent(project, id), error) ||
        std::filesystem::exists(addonCode(project, id), error))
        return std::format("The project already has an addon named {}", id);
    return std::nullopt;
}

core::Result<AddonInfo> createAddon(const Project& project, const AddonDraft& draft)
{
    if (auto problem = addonDraftProblem(project, draft))
        return core::makeError(core::ErrorCode::InvalidArgument, "{}", *problem);
    const std::string id = draft.id.empty() ? addonIdFromName(draft.name) : draft.id;
    const auto content = addonContent(project, id);
    const auto code = addonCode(project, id);
    serialization::TextDocument manifest;
    auto& section = manifest.sections.emplace_back();
    section.type = "addon";
    section.attributes = {{"format", std::int64_t{1}},
                          {"id", id},
                          {"name", draft.name},
                          {"version", draft.version}};
    if (!draft.author.empty())
        section.attributes.push_back({"author", draft.author});
    if (!draft.license.empty())
        section.attributes.push_back({"license", draft.license});
    if (!draft.description.empty())
        section.attributes.push_back({"description", draft.description});
    auto written = core::writeTextFile(content / addonManifest, serialization::writeText(manifest));
    if (written)
        written = core::writeTextFile(content / "README.md", readme(id, draft));
    if (written && draft.exampleCommand)
        written = core::writeTextFile(code / addonExampleScript, exampleScript(id, draft.name));
    core::Result<AddonInfo> addon = std::unexpected(core::Error{});
    if (written)
        addon = readAddon(content);
    else
        addon = std::unexpected(written.error());
    if (!addon)
    {
        std::error_code ignored;
        std::filesystem::remove_all(content, ignored);
        std::filesystem::remove_all(code, ignored);
    }
    return addon;
}

core::Result<AddonPreview> previewAddon(const Project& project, const Path& source)
{
    std::error_code error;
    if (!std::filesystem::exists(source / addonManifest, error))
        return core::makeError(core::ErrorCode::NotFound,
                               "This folder has no {}: choose the folder of an addon",
                               addonManifest);
    auto addon = readAddon(source);
    if (!addon)
        return std::unexpected(addon.error());
    AddonPreview preview{.addon = std::move(*addon)};
    const auto content = addonContent(project, preview.addon.id);
    if (std::filesystem::exists(content, error) ||
        std::filesystem::exists(addonCode(project, preview.addon.id), error))
    {
        const auto installed = readAddon(content);
        preview.installedVersion = installed ? installed->version : std::string{"unknown"};
    }
    forEachFile(source / "code", true, {}, [&](const Path&) { ++preview.scripts; });
    preview.conflicts = findConflicts(project, source, preview.addon.id);
    return preview;
}

core::Result<AddonInfo> installAddon(const Project& project, const Path& source)
{
    const auto preview = previewAddon(project, source);
    if (!preview)
        return std::unexpected(preview.error());
    if (preview->installedVersion)
        return core::makeError(core::ErrorCode::InvalidState,
                               "Addon {} is already installed: use Update from Folder on its card",
                               preview->addon.id);
    if (!preview->conflicts.empty())
        return core::makeError(core::ErrorCode::InvalidState, "Addon {} cannot be installed: {}",
                               preview->addon.id, preview->conflicts.front());
    return placePackage(project, *preview, source);
}

core::Result<AddonInfo> updateAddon(const Project& project, const Path& source)
{
    const auto preview = previewAddon(project, source);
    if (!preview)
        return std::unexpected(preview.error());
    if (!preview->installedVersion)
        return core::makeError(core::ErrorCode::InvalidState, "Addon {} is not installed",
                               preview->addon.id);
    if (!preview->conflicts.empty())
        return core::makeError(core::ErrorCode::InvalidState, "Addon {} cannot be updated: {}",
                               preview->addon.id, preview->conflicts.front());
    return placePackage(project, *preview, source);
}

core::Result<Path> exportAddon(const Project& project, std::string_view id, const Path& parent)
{
    const auto content = addonContent(project, id);
    const auto code = addonCode(project, id);
    const auto addon = readAddon(content);
    if (!addon)
        return std::unexpected(addon.error());
    if (inside(parent, project.assetsDirectory()) || inside(parent, project.codeDirectory()))
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "Choose a folder outside the project's assets and code");
    const auto destination = parent / core::pathFromUtf8(id);
    std::error_code error;
    if (std::filesystem::exists(destination, error))
        return core::makeError(core::ErrorCode::AlreadyExists, "{} already exists",
                               core::toUtf8(destination));
    auto copied = copyFiles(content, [&](const Path& relative)
                            { return Placement{destination / relative, false}; });
    if (copied && std::filesystem::exists(code, error))
        copied = copyFiles(code, [&](const Path& relative)
                           { return Placement{destination / "code" / relative, true}; });
    if (!copied)
    {
        std::error_code ignored;
        std::filesystem::remove_all(destination, ignored);
        return std::unexpected(copied.error());
    }
    return destination;
}

std::vector<std::string> addonResourcePaths(const Project& project, std::string_view id)
{
    std::vector<std::string> paths;
    if (!validAddonId(id))
        return paths;
    std::error_code error;
    for (const auto& folder : {addonContent(project, id), addonCode(project, id)})
        if (std::filesystem::exists(folder, error))
            paths.push_back(project.resourcePath(folder));
    return paths;
}

std::vector<AddonCommand> commandsWithoutAddon(const Project& project,
                                               std::span<const AddonCommand> commands)
{
    const auto installed = installedAddons(project);
    std::vector<AddonCommand> unknown;
    for (const auto& command : commands)
        if (!std::ranges::contains(installed, command.addon, &AddonInfo::id))
            unknown.push_back(command);
    return unknown;
}

} // namespace devex::asset
