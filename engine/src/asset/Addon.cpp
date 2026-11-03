#include <devex/asset/Addon.hpp>

#include <devex/core/File.hpp>
#include <devex/core/Path.hpp>
#include <devex/core/Uuid.hpp>
#include <devex/serialization/Text.hpp>

#include <algorithm>

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

core::Result<void> copyPackage(const Path& source, const Path& staged)
{
    std::error_code error;
    const auto root = std::filesystem::canonical(source, error);
    if (error)
        return core::makeError(core::ErrorCode::Io, "Cannot open the addon: {}", error.message());
    std::filesystem::create_directories(staged / "content", error);
    for (std::filesystem::recursive_directory_iterator entry(root, error), end;
         !error && entry != end; entry.increment(error))
    {
        const auto relative = entry->path().lexically_relative(root);
        const bool code = *relative.begin() == "code";
        if (hidden(entry->path(), code))
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
        Path destination = staged / (code ? "code" : "content");
        destination /= code ? relative.lexically_relative("code") : relative;
        if (std::filesystem::is_directory(status))
            std::filesystem::create_directories(destination, error);
        else
        {
            std::filesystem::create_directories(destination.parent_path(), error);
            if (!error)
                std::filesystem::copy_file(entry->path(), destination,
                                           std::filesystem::copy_options::none, error);
        }
    }
    if (error)
        return core::makeError(core::ErrorCode::Io, "Cannot copy the addon: {}", error.message());
    return {};
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

core::Result<AddonInfo> installAddon(const Project& project, const Path& source)
{
    auto addon = readAddon(source);
    if (!addon)
        return std::unexpected(addon.error());
    const auto content = project.assetsDirectory() / "addons" / addon->id;
    const auto code = project.codeDirectory() / "addons" / addon->id;
    std::error_code error;
    if (std::filesystem::exists(content, error) || std::filesystem::exists(code, error))
        return core::makeError(core::ErrorCode::InvalidState, "Addon {} is already installed",
                               addon->id);
    if (error)
        return core::makeError(core::ErrorCode::Io, "Cannot inspect the addon destination: {}",
                               error.message());
    // Only this unique staging directory, and directories this install moved from it, are removed.
    const auto staged =
        project.cacheDirectory() / "addon-install" / core::Uuid::generate().toString();
    const auto clean = [&]
    {
        std::error_code ignored;
        std::filesystem::remove_all(staged, ignored);
    };
    if (auto copied = copyPackage(source, staged); !copied)
    {
        clean();
        return std::unexpected(copied.error());
    }
    const bool hasCode = std::filesystem::exists(staged / "code", error);
    std::filesystem::create_directories(content.parent_path(), error);
    if (!error && hasCode)
        std::filesystem::create_directories(code.parent_path(), error);
    if (!error)
        std::filesystem::rename(staged / "content", content, error);
    bool moved = !error;
    if (!error && hasCode)
        std::filesystem::rename(staged / "code", code, error);
    if (error)
    {
        if (moved)
        {
            std::error_code ignored;
            std::filesystem::rename(content, staged / "content", ignored);
        }
        clean();
        return core::makeError(core::ErrorCode::Io, "Cannot install the addon: {}",
                               error.message());
    }
    clean();
    addon->directory = content;
    return addon;
}

} // namespace devex::asset
