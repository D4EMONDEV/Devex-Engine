#include "ToolsState.hpp"

#include <devex/core/File.hpp>
#include <devex/core/Path.hpp>

#include <algorithm>
#include <format>

namespace devex::tools::detail {

void requestNewScript(ToolsState& state, bool addToSelection)
{
    state.newFileFolder = "res://code";
    state.newScriptTarget = addToSelection ? state.selection.active() : core::Uuid{};
    state.openNewScriptPopup = true;
}

std::vector<std::string> creationFolders(const asset::Project& project, asset::ContentRoot content)
{
    const auto root = content == asset::ContentRoot::Code ? project.codeDirectory() : project.assetsDirectory();
    std::vector<std::string> folders{project.resourcePath(root)};
    std::error_code error;
    const auto base = std::filesystem::canonical(project.root, error) / (content == asset::ContentRoot::Code ? "code" : "assets");
    if (error)
    {
        return folders;
    }
    auto entry = std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied, error);
    for (auto end = std::filesystem::recursive_directory_iterator(); !error && entry != end; entry.increment(error))
    {
        if (!entry->is_directory(error))
        {
            continue;
        }
        const auto name = core::toUtf8(entry->path().filename());
        const auto canonical = std::filesystem::canonical(entry->path(), error);
        const auto relative = canonical.lexically_relative(base);
        if (error || relative.empty() || relative.is_absolute() || *relative.begin() == ".." || name.starts_with('.') ||
            (content == asset::ContentRoot::Code && (name == "bin" || name == "obj")))
        {
            entry.disable_recursion_pending();
            error.clear();
            continue;
        }
        folders.push_back(project.resourcePath(entry->path()));
    }
    std::ranges::sort(folders);
    return folders;
}

core::Result<std::filesystem::path> createContentFolder(ToolsState& state, std::string_view parent, std::string_view name)
{
    if (state.database == nullptr)
    {
        return core::makeError(core::ErrorCode::InvalidState, "No project is open.");
    }
    const auto& project = state.database->project();
    const bool code = parent == "res://code" || parent.starts_with("res://code/");
    if (code && (name == "bin" || name == "obj"))
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "This folder name is reserved for build outputs.");
    }
    const auto folder = project.newFilePath(parent, name, code ? asset::ContentRoot::Code : asset::ContentRoot::Assets);
    if (!folder)
    {
        return std::unexpected(folder.error());
    }
    std::error_code error;
    if (!std::filesystem::create_directory(*folder, error))
    {
        return core::makeError(error ? core::ErrorCode::Io : core::ErrorCode::AlreadyExists,
                               "Cannot create the folder: {}", error ? error.message() : "the name is already used.");
    }
    state.database->refresh();
    state.assetToReveal = project.resourcePath(*folder);
    return *folder;
}

core::Result<std::filesystem::path> writeNewAssetFile(ToolsState& state, std::string_view folder,
    std::string_view requestedName, std::string_view defaultName, std::string_view extension, std::string_view text)
{
    if (state.database == nullptr)
    {
        return core::makeError(core::ErrorCode::InvalidState, "No project is open.");
    }
    const auto& project = state.database->project();
    for (int number = 1; number < 1000; ++number)
    {
        const std::string name = !requestedName.empty() ? std::string(requestedName)
            : number == 1 ? std::string(defaultName) : std::format("{} {}", defaultName, number);
        const auto file = project.newFilePath(folder, name + std::string(extension), asset::ContentRoot::Assets);
        if (!file)
        {
            if (requestedName.empty() && file.error().code == core::ErrorCode::AlreadyExists)
            {
                continue;
            }
            return std::unexpected(file.error());
        }
        if (auto written = core::writeTextFile(*file, text); !written)
        {
            return std::unexpected(written.error());
        }
        state.database->refresh();
        state.assetToSelect = project.resourcePath(*file);
        return *file;
    }
    return core::makeError(core::ErrorCode::AlreadyExists, "Too many files are named {} in {}.", defaultName, folder);
}

} // namespace devex::tools::detail
