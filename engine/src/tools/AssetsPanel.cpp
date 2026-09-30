// FileSystem, made with the interface of the engine as Godot's dock is with its nodes: the files of the
// project as a tree whose rows only exist for the lines on screen. Its files are dragged onto the
// viewport and the properties, and an entity dropped on one of its folders becomes a prefab there.
#include "EditorUi.hpp"
#include "ToolsState.hpp"

#include <devex/core/Profiler.hpp>
#include <devex/asset/Project.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>
#include <devex/scene/UiComponents.hpp>

#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>

namespace devex::tools::detail {

using scene::Entity;
using scene::UiRect;
using namespace rects;
using Button = PanelButton;

namespace {

// The image the panel is drawn into, among the interface surfaces of the editor.
constexpr std::uint32_t fileSystemSurface = 2;
// How often the folders of the code are read again, since nothing tells when they change.
constexpr double codeListingSeconds = 1.0;

// A folder of the project, built from the paths of the source files.
struct Folder
{
    std::map<std::string, Folder, std::less<>> folders;
    std::vector<const asset::SourceFile*> files;
};

// The folder tree points into the sources, which must outlive it.
[[nodiscard]] Folder buildTree(const std::vector<asset::SourceFile>& sources)
{
    Folder root;
    for (const asset::SourceFile& source : sources)
    {
        std::string_view path = source.path;
        if (path.starts_with(asset::resourceScheme))
        {
            path.remove_prefix(asset::resourceScheme.size());
        }
        Folder* folder = &root;
        for (std::size_t slash = path.find('/'); slash != std::string_view::npos; slash = path.find('/'))
        {
            const std::string_view name = path.substr(0, slash);
            auto found = folder->folders.find(name);
            if (found == folder->folders.end())
            {
                found = folder->folders.emplace(std::string(name), Folder{}).first;
            }
            folder = &found->second;
            path.remove_prefix(slash + 1);
        }
        folder->files.push_back(&source);
    }
    return root;
}

[[nodiscard]] std::string_view fileName(std::string_view path)
{
    const std::size_t slash = path.rfind('/');
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

[[nodiscard]] EntityIcon assetIcon(const asset::SourceFile& source, const asset::AssetInfo* mainAsset)
{
    const ThemeColors& colors = themeColors();
    if (mainAsset == nullptr)
    {
        return {icons::File, colors.neutral};
    }
    switch (mainAsset->type)
    {
    case asset::AssetType::Scene:
        return {icons::Clapperboard, colors.scene};
    case asset::AssetType::Model:
        return {icons::Package, colors.entity};
    case asset::AssetType::Mesh:
        return {icons::Box, colors.entity};
    case asset::AssetType::Material:
        return {icons::Palette, colors.material};
    case asset::AssetType::Texture:
        return source.path.ends_with(".hdr") ? EntityIcon{icons::Mountain, colors.environment}
                                             : EntityIcon{icons::Image, colors.texture};
    case asset::AssetType::AudioClip:
        return {icons::AudioWaveform, colors.audio};
    case asset::AssetType::AnimationClip:
        return {icons::Film, colors.animation};
    case asset::AssetType::Curve:
        return {icons::Activity, colors.animation};
    case asset::AssetType::SpriteFrames:
        return {icons::Clapperboard, colors.animation};
    case asset::AssetType::Tileset:
        return {icons::Grid, colors.texture};
    case asset::AssetType::Animator:
        return {icons::Workflow, colors.animation};
    case asset::AssetType::NavMesh:
        return {icons::Footprints, colors.physics};
    default:
        break;
    }
    return {icons::File, colors.neutral};
}

[[nodiscard]] EntityIcon subAssetIcon(asset::AssetType type)
{
    const ThemeColors& colors = themeColors();
    switch (type)
    {
    case asset::AssetType::Mesh:
        return {icons::Box, colors.entity};
    case asset::AssetType::Material:
        return {icons::Palette, colors.material};
    case asset::AssetType::Texture:
    case asset::AssetType::Sprite:
        return {icons::Image, colors.texture};
    case asset::AssetType::AnimationClip:
        return {icons::Film, colors.animation};
    default:
        return {icons::File, colors.neutral};
    }
}

[[nodiscard]] bool isCodeFile(const std::filesystem::path& path)
{
    const std::filesystem::path extension = path.extension();
    return extension == ".cs" || extension == ".cpp" || extension == ".hpp" || extension == ".h" ||
           extension == ".txt" || extension == ".csproj";
}

void selectCodeFile(ToolsState& state, const std::filesystem::path& file)
{
    state.selectedCode = file;
    // The inspector shows one thing at a time.
    state.selection.clear();
    state.selectedAsset = {};
}

void showInFileManager(ToolsState& state, const std::filesystem::path& folder)
{
    if (core::Result<void> opened = state.platform.openPath(folder); !opened)
    {
        DEVEX_LOG_WARNING("{}", opened.error());
    }
}

enum class NodeKind : std::uint8_t
{
    Folder,
    Source,
    SubAsset,
    ProjectFile,
    CodeFolder,
    CodeFile,
};

// A line of the tree.
struct Node
{
    NodeKind kind = NodeKind::Folder;
    int depth = 0;
    // What remembers whether it is open and whether it is chosen.
    std::string key;
    std::string label;
    std::string detail;
    EntityIcon icon{icons::File, ImVec4()};
    bool expandable = false;
    bool expanded = false;
    // Folders: their res:// path. Sources: their res:// path too.
    std::string path;
    // Sources and the assets inside them.
    asset::AssetId asset;
    asset::AssetType type = asset::AssetType::Mesh;
    bool hasAsset = false;
    asset::ImportStatus status = asset::ImportStatus::Ready;
    std::string error;
    // The files of the code and of the project.
    std::filesystem::path file;
};

// One line on screen and what it shows.
struct Row
{
    Entity row;
    Entity arrow;
    Entity icon;
    Entity label;
    Entity detail;
    Entity status;
};

// The entries of a folder of the code, read again once in a while.
struct CodeListing
{
    double readAt = -1.0e9;
    std::vector<std::pair<std::filesystem::path, bool>> entries;
};

} // namespace

// The panel and the entities the code reads and changes.
struct FileSystemUi : PanelBuilder
{
    FileSystemUi()
        : PanelBuilder(fileSystemSurface)
    {
    }

    bool built = false;
    float rowHeight = 26.0f;
    float indent = 16.0f;

    Entity filter;
    Entity list;
    Entity scroll;
    Entity lines;
    std::vector<Row> rows;
    // The node each row showed at the last update, or none.
    std::vector<std::optional<std::size_t>> rowNodes;

    Entity menu;
    Button editText;
    Button editMeta;
    Button openScene;
    Button place;
    Button startup;
    Entity sourceSeparator;
    Button reimport;
    Button copyPath;
    Button newCurve;
    Button newFrames;
    Button newTileset;
    Button newAnimator;
    Entity folderSeparator;
    Button externalEditor;
    Button showFolder;
    // What the menu acts on, from the moment it opened.
    std::optional<Node> menuNode;

    std::vector<asset::SourceFile> sources;
    std::vector<Node> nodes;
    std::unordered_map<std::string, bool> expanded;
    std::string selected;
    std::map<std::filesystem::path, CodeListing> codeListings;
    double clock = 0.0;

    void build(ToolsState& state, EditorUiKit& kit);
    void gather(ToolsState& state);
    void addFolder(ToolsState& state, const std::string& name, const std::string& path, const Folder& folder, int depth);
    void addSource(ToolsState& state, const asset::SourceFile& source, int depth, bool wholePath);
    void addCode(const std::filesystem::path& path, bool directory, int depth);
    void update(ToolsState& state, EditorUiKit& kit, scene::Scene& edited, core::Duration delta);
    void choose(ToolsState& state, const Node& node);
    void activate(ToolsState& state, scene::Scene& edited, const Node& node);
    void openMenu(ToolsState& state, EditorUiKit& kit);
    void answerMenu(ToolsState& state, scene::Scene& edited);
    void dropEntity(ToolsState& state, const scene::Scene& edited, const Node& folder, const std::string& uuid);

    [[nodiscard]] bool isOpen(const std::string& key, bool byDefault) const
    {
        const auto found = expanded.find(key);
        return found != expanded.end() ? found->second : byDefault;
    }
};

void FileSystemUi::build(ToolsState& state, EditorUiKit& kit)
{
    built = true;
    const Entity root = add({}, "FileSystem", whole());
    scene().add<scene::UiLayout>(root, scene::UiLayout{.kind = scene::UiLayoutKind::Column,
                                                       .spacing = font * 0.4f,
                                                       .align = scene::TextAlign::Left});
    filter = searchField(kit, root, wide(font * 2.0f), state.assetFilter, "Filter Files");

    list = add(root, "List", whole(), "list");
    scene().add<scene::UiImage>(list);
    scroll = add(list, "Scroll", whole(math::Vec4{2.0f}), "scroll");
    scene().add<scene::UiScroll>(scroll, scene::UiScroll{});
    lines = add(scroll, "Lines", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 0.0f}, .offsetMin = {0.0f, 0.0f}, .offsetMax = {-8.0f, 0.0f}});
    scene().add<scene::UiVirtualList>(lines);

    menu = PanelBuilder::menu("FileSystem menu", font * 15.0f);
    editText = menuItem(kit, menu, Icon::FileText, "Edit as Text");
    editMeta = menuItem(kit, menu, Icon::FileText, "Edit Import Metadata");
    openScene = menuItem(kit, menu, Icon::FolderOpen, "Open Scene");
    place = menuItem(kit, menu, Icon::Plus, "Place in Scene");
    startup = menuItem(kit, menu, Icon::House, "Set as Startup Scene");
    sourceSeparator = menuSeparator(menu);
    reimport = menuItem(kit, menu, Icon::Refresh, "Reimport");
    copyPath = menuItem(kit, menu, Icon::Copy, "Copy Path");
    newCurve = menuItem(kit, menu, Icon::Activity, "New Curve");
    tooltip(newCurve.entity, "A curve drawn by hand, which eases tweens and Tweeners");
    newFrames = menuItem(kit, menu, Icon::Clapperboard, "New Sprite Frames");
    tooltip(newFrames.entity, "Named animations of sprites, which SpriteAnimator components play");
    newTileset = menuItem(kit, menu, Icon::Grid, "New Tileset");
    tooltip(newTileset.entity, "The tiles Tilemap components paint their cells with");
    newAnimator = menuItem(kit, menu, Icon::Workflow, "New Animator");
    tooltip(newAnimator.entity, "A state machine of animations, which Animator components play");
    folderSeparator = menuSeparator(menu);
    externalEditor = menuItem(kit, menu, Icon::ExternalLink, "Open in External Editor");
    showFolder = menuItem(kit, menu, Icon::FolderOpen, "Show in File Manager");

    // The files leave as the assets ImGui windows take; entities come in to be made prefabs.
    panel.setKeyboardNavigation(false);
    panel.setDragOut([&state](const ui::Carried& carried) -> std::optional<ImGuiDrag> {
        const std::optional<core::Uuid> uuid = core::Uuid::parse(carried.data);
        const asset::AssetInfo* const info =
            carried.type == "asset" && uuid && state.database != nullptr ? state.database->find(asset::AssetId{*uuid}) : nullptr;
        if (info == nullptr)
        {
            return std::nullopt;
        }
        const AssetPayload payload{.uuid = uuid->bytes(), .type = info->type};
        // The same name out of the panel as in it.
        ImGuiDrag drag{.type = assetPayload, .label = carried.label.empty() ? info->name : carried.label};
        drag.payload.resize(sizeof(payload));
        std::memcpy(drag.payload.data(), &payload, sizeof(payload));
        return drag;
    });
    panel.setDragIn([&state](const ImGuiPayload& payload) -> std::optional<std::pair<std::string, std::string>> {
        if (!payload.IsDataType(entityPayload) || payload.DataSize != 16 || state.mode != ToolsMode::Editor ||
            state.playState != PlayState::Editing)
        {
            return std::nullopt;
        }
        std::array<std::uint8_t, 16> bytes{};
        std::memcpy(bytes.data(), payload.Data, bytes.size());
        return std::pair{std::string("entity"), uuidFromBytes(bytes).toString()};
    });
}

void FileSystemUi::addSource(ToolsState& state, const asset::SourceFile& source, int depth, bool wholePath)
{
    const asset::AssetDatabase& database = *state.database;
    const asset::AssetInfo* const mainAsset = database.find(source.id);
    std::size_t children = 0;
    for (const asset::AssetId id : source.assets)
    {
        children += id != source.id && database.find(id) != nullptr ? 1 : 0;
    }
    Node node{.kind = NodeKind::Source,
              .depth = depth,
              .key = "source:" + source.id.uuid.toString(),
              .label = wholePath ? source.path : std::string(fileName(source.path)),
              .icon = assetIcon(source, mainAsset),
              .expandable = children > 0,
              .path = source.path,
              .asset = source.id,
              .hasAsset = mainAsset != nullptr,
              .status = source.status,
              .error = source.error};
    if (mainAsset != nullptr)
    {
        node.type = mainAsset->type;
        if (mainAsset->type == asset::AssetType::Scene && database.project().startupScene == source.path)
        {
            node.detail = "startup";
        }
    }
    node.expanded = node.expandable && isOpen(node.key, false);
    const bool open = node.expanded;
    nodes.push_back(std::move(node));
    if (!open)
    {
        return;
    }
    for (const asset::AssetId id : source.assets)
    {
        const asset::AssetInfo* const info = database.find(id);
        if (id == source.id || info == nullptr)
        {
            continue;
        }
        nodes.push_back(Node{.kind = NodeKind::SubAsset,
                             .depth = depth + 1,
                             .key = "asset:" + id.uuid.toString(),
                             .label = info->name,
                             .icon = subAssetIcon(info->type),
                             .path = source.path,
                             .asset = id,
                             .type = info->type,
                             .hasAsset = true});
    }
}

void FileSystemUi::addCode(const std::filesystem::path& path, bool directory, int depth)
{
    const std::string name = core::toUtf8(path.filename());
    if (!directory)
    {
        nodes.push_back(Node{.kind = NodeKind::CodeFile,
                             .depth = depth,
                             .key = "code:" + core::toUtf8(path),
                             .label = name,
                             .icon = codeIcon(path),
                             .file = path});
        return;
    }
    Node node{.kind = NodeKind::CodeFolder,
              .depth = depth,
              .key = "code:" + core::toUtf8(path),
              .label = name,
              .icon = {icons::Folder, themeColors().folder},
              .expandable = true,
              .file = path};
    node.expanded = isOpen(node.key, false);
    if (node.expanded)
    {
        node.icon.icon = icons::FolderOpen;
    }
    const bool open = node.expanded;
    nodes.push_back(std::move(node));
    if (!open)
    {
        return;
    }
    // Read again once in a while: files written outside the editor show up by themselves.
    CodeListing& listing = codeListings[path];
    if (clock - listing.readAt >= codeListingSeconds)
    {
        listing.readAt = clock;
        listing.entries.clear();
        std::error_code error;
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(path, error))
        {
            // Build folders hold no code of the game.
            const std::string entryName = core::toUtf8(entry.path().filename());
            if (entryName == "obj" || entryName == "bin" || entryName.starts_with('.'))
            {
                continue;
            }
            const bool isDirectory = entry.is_directory(error);
            if (isDirectory || isCodeFile(entry.path()))
            {
                listing.entries.emplace_back(entry.path(), isDirectory);
            }
        }
        std::ranges::sort(listing.entries);
    }
    for (const auto& [entry, isDirectory] : listing.entries)
    {
        addCode(entry, isDirectory, depth + 1);
    }
}

void FileSystemUi::addFolder(ToolsState& state, const std::string& name, const std::string& path, const Folder& folder,
                             int depth)
{
    Node node{.kind = NodeKind::Folder,
              .depth = depth,
              .key = "folder:" + path,
              .label = name,
              .icon = {icons::Folder, themeColors().folder},
              .expandable = true,
              .path = path};
    node.expanded = isOpen(node.key, depth <= 1);
    if (node.expanded)
    {
        node.icon.icon = icons::FolderOpen;
    }
    const bool open = node.expanded;
    nodes.push_back(std::move(node));
    if (!open)
    {
        return;
    }
    for (const auto& [childName, child] : folder.folders)
    {
        addFolder(state, childName, path.ends_with('/') ? path + childName : path + "/" + childName, child, depth + 1);
    }
    for (const asset::SourceFile* const source : folder.files)
    {
        addSource(state, *source, depth + 1, false);
    }
    // res:// is the project root: its project file and code belong inside the same tree.
    if (depth == 0)
    {
        const std::filesystem::path projectFile = state.database->project().file;
        nodes.push_back(Node{.kind = NodeKind::ProjectFile,
                             .depth = depth + 1,
                             .key = "code:" + core::toUtf8(projectFile),
                             .label = core::toUtf8(projectFile.filename()),
                             .icon = {icons::FileText, themeColors().neutral},
                             .file = projectFile});
        const std::filesystem::path code = state.database->project().codeDirectory();
        std::error_code error;
        if (std::filesystem::is_directory(code, error))
        {
            addCode(code, true, depth + 1);
        }
    }
}

void FileSystemUi::gather(ToolsState& state)
{
    nodes.clear();
    sources = state.database->sources();
    if (state.assetFilter.empty())
    {
        addFolder(state, "res://", std::string(asset::resourceScheme), buildTree(sources), 0);
        return;
    }
    // A filter lists the matching files with their whole path.
    for (const asset::SourceFile& source : sources)
    {
        if (containsIgnoringCase(source.path, state.assetFilter))
        {
            addSource(state, source, 0, true);
        }
    }
}

void FileSystemUi::choose(ToolsState& state, const Node& node)
{
    selected = node.key;
    if (node.kind == NodeKind::CodeFile || node.kind == NodeKind::ProjectFile)
    {
        if (node.kind == NodeKind::CodeFile)
        {
            selectCodeFile(state, node.file);
        }
        if (state.mode == ToolsMode::Editor || node.kind == NodeKind::ProjectFile)
        {
            openTextFile(state, node.file);
        }
        return;
    }
    if (node.kind != NodeKind::Source || !node.hasAsset)
    {
        return;
    }
    // Curves, sprite frames, textures, tilesets, animators, models and clips show in the inspector.
    switch (node.type)
    {
    case asset::AssetType::Curve:
    case asset::AssetType::SpriteFrames:
    case asset::AssetType::Texture:
    case asset::AssetType::Tileset:
    case asset::AssetType::Animator:
    case asset::AssetType::Model:
    case asset::AssetType::AudioClip:
        selectAsset(state, node.asset);
        break;
    default:
        break;
    }
}

void FileSystemUi::activate(ToolsState& state, scene::Scene& edited, const Node& node)
{
    if (node.expandable && (node.kind == NodeKind::Folder || node.kind == NodeKind::CodeFolder))
    {
        expanded[node.key] = !node.expanded;
        return;
    }
    if (node.kind == NodeKind::CodeFile && state.mode != ToolsMode::Editor)
    {
        openTextFile(state, node.file);
        return;
    }
    if (node.kind != NodeKind::Source || !node.hasAsset)
    {
        return;
    }
    const asset::AssetDatabase& database = *state.database;
    const std::optional<std::filesystem::path> path = database.project().absolutePath(node.path);
    switch (node.type)
    {
    case asset::AssetType::Material:
        if (path && path->extension() == ".dvxmat")
        {
            openTextFile(state, *path);
        }
        break;
    case asset::AssetType::Animator:
        // Animators open in the Animator panel.
        focusPanel(state, animatorWindow);
        break;
    case asset::AssetType::Model:
        requestInstantiateModel(state, node.asset, core::Uuid{});
        break;
    case asset::AssetType::AudioClip:
        previewAudioClip(state, node.asset);
        break;
    case asset::AssetType::Scene:
        if (state.mode == ToolsMode::Editor && path)
        {
            openSceneTab(state, edited, *path);
        }
        break;
    default:
        break;
    }
}

void FileSystemUi::openMenu(ToolsState& state, EditorUiKit& kit)
{
    // The entries that act on what the menu opened on.
    const Node& node = *menuNode;
    const bool source = node.kind == NodeKind::Source;
    const bool folder = node.kind == NodeKind::Folder;
    const bool code = node.kind == NodeKind::CodeFile || node.kind == NodeKind::ProjectFile;
    const bool isScene = source && node.hasAsset && node.type == asset::AssetType::Scene;
    const auto show = [&](const Button& button, bool shown) { scene().get<UiRect>(button.entity).visible = shown; };
    show(editText, source || code);
    show(editMeta, source);
    show(openScene, isScene && state.mode == ToolsMode::Editor);
    show(place, source && node.hasAsset && node.type == asset::AssetType::Model);
    show(startup, isScene);
    enable(startup, node.detail.empty());
    scene().get<UiRect>(sourceSeparator).visible = source;
    show(reimport, source);
    show(copyPath, source);
    show(newCurve, folder);
    show(newFrames, folder);
    show(newTileset, folder);
    show(newAnimator, folder);
    scene().get<UiRect>(folderSeparator).visible = folder;
    show(externalEditor, node.kind == NodeKind::CodeFile);
    show(showFolder, source || folder || code || node.kind == NodeKind::CodeFolder);
    static_cast<void>(kit);
}

void FileSystemUi::answerMenu(ToolsState& state, scene::Scene& edited)
{
    if (!menuNode)
    {
        return;
    }
    ui::UiWorld& world = panel.world();
    const Node& node = *menuNode;
    asset::AssetDatabase& database = *state.database;
    const std::optional<std::filesystem::path> path =
        node.kind == NodeKind::Source ? database.project().absolutePath(node.path)
        : node.kind == NodeKind::Folder ? database.project().absolutePath(node.path)
                                        : std::optional<std::filesystem::path>(node.file);
    const auto created = [](core::Result<std::filesystem::path> result, std::string_view what) {
        if (!result)
        {
            DEVEX_LOG_ERROR("Cannot create the {}: {}", what, result.error());
        }
    };
    if (world.wasClicked(editText.entity) && path)
    {
        openTextFile(state, *path);
    }
    else if (world.wasClicked(editMeta.entity) && path)
    {
        std::filesystem::path meta = *path;
        meta += ".dvxmeta";
        openTextFile(state, meta);
    }
    else if (world.wasClicked(openScene.entity) && path)
    {
        openSceneTab(state, edited, *path);
    }
    else if (world.wasClicked(place.entity))
    {
        requestInstantiateModel(state, node.asset, core::Uuid{});
    }
    else if (world.wasClicked(startup.entity))
    {
        asset::Project project = database.project();
        project.startupScene = node.path;
        if (core::Result<void> saved = database.updateProject(project); !saved)
        {
            DEVEX_LOG_ERROR("Cannot save the project: {}", saved.error());
        }
        else
        {
            DEVEX_LOG_INFO("{} is the startup scene", node.path);
        }
    }
    else if (world.wasClicked(reimport.entity))
    {
        if (core::Result<void> queued = database.reimport(node.asset); !queued)
        {
            DEVEX_LOG_WARNING("{}", queued.error());
        }
    }
    else if (world.wasClicked(copyPath.entity))
    {
        ImGui::SetClipboardText(node.path.c_str());
    }
    else if (world.wasClicked(newCurve.entity))
    {
        created(createCurveFile(state, node.path), "curve");
    }
    else if (world.wasClicked(newFrames.entity))
    {
        created(createSpriteFramesFile(state, node.path), "sprite frames");
    }
    else if (world.wasClicked(newTileset.entity))
    {
        created(createTilesetFile(state, node.path), "tileset");
    }
    else if (world.wasClicked(newAnimator.entity))
    {
        created(createAnimatorFile(state, node.path), "animator");
    }
    else if (world.wasClicked(externalEditor.entity))
    {
        openInCodeEditor(state, node.file);
    }
    else if (world.wasClicked(showFolder.entity) && path)
    {
        const bool isFolder = node.kind == NodeKind::Folder || node.kind == NodeKind::CodeFolder;
        showInFileManager(state, isFolder ? *path : path->parent_path());
    }
}

void FileSystemUi::dropEntity(ToolsState& state, const scene::Scene& edited, const Node& folder, const std::string& uuid)
{
    // As Godot saves a branch dropped on its FileSystem as a scene: a prefab in that folder, named after
    // the entity, next to the files already there.
    const std::optional<core::Uuid> parsed = core::Uuid::parse(uuid);
    const scene::Entity entity = parsed ? edited.findEntity(*parsed) : scene::Entity{};
    const std::optional<std::filesystem::path> directory = state.database->project().absolutePath(folder.path);
    if (!entity.isValid() || !directory || state.dialogAnswers == nullptr)
    {
        return;
    }
    std::string name = edited.name(entity).empty() ? std::string("prefab") : edited.name(entity);
    std::ranges::replace_if(name, [](char character) { return std::string_view("<>:\"/\\|?*").contains(character); }, '_');
    std::filesystem::path file = *directory / core::pathFromUtf8(name + ".dvxscene");
    std::error_code error;
    for (int copy = 2; std::filesystem::exists(file, error); ++copy)
    {
        file = *directory / core::pathFromUtf8(std::format("{} {}.dvxscene", name, copy));
    }
    state.prefabEntity = *parsed;
    state.dialogAnswers->saveAsPrefab = file;
}

void FileSystemUi::update(ToolsState& state, EditorUiKit& kit, scene::Scene& edited, core::Duration delta)
{
    const ThemeColors& colors = themeColors();
    kit.refreshTheme(colors);
    font = state.theme.fontSize;
    rowHeight = std::round(font * 1.85f);
    indent = std::round(font * 1.3f);
    clock += std::chrono::duration<double>(delta).count();
    if (!built)
    {
        build(state, kit);
    }
    styleTooltips(colors);
    ui::UiWorld& world = panel.world();
    tooltip(filter, state.mode == ToolsMode::Editor
                        ? "Drag a model into the viewport or the scene tree, or an asset onto a property. "
                          "Double-click a scene to open it. Drop an entity on a folder to save it as a prefab."
                        : "Drag a model into the scene tree, or an asset onto a property.");

    // A file created here is chosen once imported, and shows in the inspector.
    if (!state.assetToSelect.empty())
    {
        const std::optional<asset::AssetId> createdId = state.database->findByPath(state.assetToSelect);
        if (createdId && state.database->find(*createdId) != nullptr)
        {
            selectAsset(state, *createdId);
            selected = "source:" + createdId->uuid.toString();
            state.assetToSelect.clear();
        }
    }
    // A file another panel asks to show: the folders around it open, and its line is chosen.
    std::optional<std::string> revealed;
    if (!state.assetToReveal.empty())
    {
        const std::string resource = std::exchange(state.assetToReveal, std::string{});
        if (const std::optional<asset::AssetId> id = state.database->findByPath(resource))
        {
            const std::size_t scheme = asset::resourceScheme.size();
            expanded["folder:" + resource.substr(0, scheme)] = true;
            for (std::size_t slash = resource.find('/', scheme); slash != std::string::npos; slash = resource.find('/', slash + 1))
            {
                expanded["folder:" + resource.substr(0, slash)] = true;
            }
            // A filter that would hide it gives way.
            std::string& filterText = scene().get<scene::UiText>(filter).text;
            if (!containsIgnoringCase(resource, filterText))
            {
                filterText.clear();
            }
            selected = "source:" + id->uuid.toString();
            revealed = selected;
        }
    }
    state.assetFilter = scene().get<scene::UiText>(filter).text;
    gather(state);

    // As many rows as the view holds, which show the lines the list will place them at: the first
    // one the scroll leaves in view.
    scene::UiVirtualList& virtualList = scene().get<scene::UiVirtualList>(lines);
    virtualList.itemCount = static_cast<std::uint32_t>(nodes.size());
    virtualList.itemSize = rowHeight;
    const scene::UiScroll& scrolled = scene().get<scene::UiScroll>(scroll);
    scene().get<scene::UiScroll>(scroll).speed = rowHeight * 3.0f;
    const ui::LaidOutRect* const view = world.canvases().empty() ? nullptr : world.canvases().front().layout.find(scroll);
    const float viewHeight = view != nullptr ? view->size().y : 0.0f;
    if (revealed)
    {
        // In the middle of the list, as far as it goes.
        if (const auto found = std::ranges::find(nodes, *revealed, &Node::key); found != nodes.end())
        {
            const float top = static_cast<float>(found - nodes.begin()) * rowHeight;
            const float bottom = std::max(static_cast<float>(nodes.size()) * rowHeight - viewHeight, 0.0f);
            scene().get<scene::UiScroll>(scroll).offset.y = std::clamp(top - (viewHeight - rowHeight) * 0.5f, 0.0f, bottom);
        }
    }
    const std::size_t needed = static_cast<std::size_t>(std::ceil(std::max(viewHeight, 300.0f) / rowHeight)) + 2;
    const float iconSize = font * 1.15f;
    const float arrow = font * 1.5f;
    while (rows.size() < needed)
    {
        Row row;
        row.row = add(lines, "Row", fixed({1.0f, rowHeight}), "row");
        scene().add<scene::UiImage>(row.row);
        scene().add<scene::UiButton>(row.row);
        scene().add<scene::UiDragSource>(row.row, scene::UiDragSource{.type = "asset"});
        scene().add<scene::UiDropTarget>(row.row);
        scene().add<scene::UiContextMenu>(row.row, scene::UiContextMenu{.popup = scene().reference(menu)});
        row.arrow = add(row.row, "Arrow", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 1.0f}});
        scene().add<scene::UiFoldout>(row.arrow);
        row.icon = icon(kit, row.row, UiRect{.anchorMin = {0.0f, 0.5f}, .anchorMax = {0.0f, 0.5f}}, Icon::File, {});
        row.label = text(row.row, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 1.0f}}, "", "text");
        row.detail = text(row.row, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 1.0f}}, "", "dim");
        row.status = icon(kit, row.row, UiRect{.anchorMin = {1.0f, 0.5f}, .anchorMax = {1.0f, 0.5f}}, Icon::Loader, "icon_warning");
        tooltip(row.status, "");
        rows.push_back(row);
    }
    rowNodes.assign(rows.size(), std::nullopt);
    const std::size_t first = nodes.empty() ? 0
                                            : std::min(static_cast<std::size_t>(std::max(scrolled.offset.y, 0.0f) / rowHeight),
                                                       nodes.size() - 1);
    const math::Vec4 drop = linearColor(ImVec4(colors.accent.x, colors.accent.y, colors.accent.z, 0.3f));
    for (std::size_t index = 0; index < rows.size(); ++index)
    {
        const Row& row = rows[index];
        const std::size_t at = first + index;
        if (at >= nodes.size())
        {
            continue;
        }
        rowNodes[index] = at;
        const Node& node = nodes[at];
        const bool chosen = node.key == selected || (node.hasAsset && node.asset == state.selectedAsset && node.kind == NodeKind::Source) ||
                            (node.kind == NodeKind::CodeFile && !state.selectedCode.empty() && node.file == state.selectedCode);
        scene().get<UiRect>(row.row).style = chosen ? "row_selected" : "row";

        const float left = font * 0.3f + static_cast<float>(node.depth) * indent;
        UiRect& arrowRect = scene().get<UiRect>(row.arrow);
        arrowRect.visible = node.expandable;
        arrowRect.offsetMin = {left, 0.0f};
        arrowRect.offsetMax = {left + arrow, 0.0f};
        scene().get<scene::UiFoldout>(row.arrow).expanded = node.expanded;
        scene().get<scene::UiFoldout>(row.arrow).arrowColor = linearColor(colors.textDim);

        const float iconLeft = left + arrow;
        UiRect& iconRect = scene().get<UiRect>(row.icon);
        iconRect.offsetMin = {iconLeft, -iconSize * 0.5f};
        iconRect.offsetMax = {iconLeft + iconSize, iconSize * 0.5f};
        scene::UiImage& iconImage = scene().get<scene::UiImage>(row.icon);
        iconImage.texture = kit.icon(iconOf(node.icon.icon));
        iconImage.color = linearColor(node.icon.color);

        const float labelLeft = iconLeft + iconSize + font * 0.45f;
        const float labelWidth = kit.textWidth(EditorUiKit::regularFont(), node.label, font) + 2.0f;
        scene::UiText& label = scene().get<scene::UiText>(row.label);
        if (label.text != node.label)
        {
            label.text = node.label;
        }
        UiRect& labelRect = scene().get<UiRect>(row.label);
        labelRect.offsetMin = {labelLeft, 0.0f};
        labelRect.offsetMax = {labelLeft + labelWidth, 0.0f};
        scene::UiText& detail = scene().get<scene::UiText>(row.detail);
        if (detail.text != node.detail)
        {
            detail.text = node.detail;
        }
        UiRect& detailRect = scene().get<UiRect>(row.detail);
        detailRect.offsetMin = {labelLeft + labelWidth + font * 0.5f, 0.0f};
        detailRect.offsetMax = {labelLeft + labelWidth + font * 0.5f + kit.textWidth(EditorUiKit::regularFont(), node.detail, font) + 2.0f, 0.0f};

        // The import status at the right of the row, when it needs attention.
        const bool attention = node.kind == NodeKind::Source && node.status != asset::ImportStatus::Ready;
        const bool failed = node.status == asset::ImportStatus::Failed;
        UiRect& statusRect = scene().get<UiRect>(row.status);
        statusRect.visible = attention;
        statusRect.offsetMin = {-font * 0.4f - iconSize, -iconSize * 0.5f};
        statusRect.offsetMax = {-font * 0.4f, iconSize * 0.5f};
        if (attention)
        {
            statusRect.style = failed ? "icon_error" : "icon_warning";
            scene().get<scene::UiImage>(row.status).texture = kit.icon(failed ? Icon::CircleX : Icon::Loader);
            tooltip(row.status, failed ? node.error : "Importing...");
        }

        // Assets leave by their row; folders take the entities dropped on them.
        scene::UiDragSource& source = scene().get<scene::UiDragSource>(row.row);
        source.interactable = node.hasAsset;
        source.data = node.hasAsset ? node.asset.uuid.toString() : std::string{};
        source.label = node.label;
        scene::UiDropTarget& target = scene().get<scene::UiDropTarget>(row.row);
        target.accepts.assign(node.kind == NodeKind::Folder ? 1 : 0, "entity");
        target.highlightColor = drop;
    }

    panel.update(kit, delta, UiPanel::zoomFor(font));

    // What was done to the rows this frame.
    const auto nodeOf = [&](Entity entity) -> const Node* {
        for (std::size_t index = 0; index < rows.size(); ++index)
        {
            if (rows[index].row == entity && rowNodes[index])
            {
                return &nodes[*rowNodes[index]];
            }
        }
        return nullptr;
    };
    std::optional<Node> clicked;
    std::optional<Node> doubleClicked;
    for (std::size_t index = 0; index < rows.size(); ++index)
    {
        if (!rowNodes[index])
        {
            continue;
        }
        const Node& node = nodes[*rowNodes[index]];
        if (world.wasChanged(rows[index].arrow))
        {
            expanded[node.key] = !node.expanded;
        }
        if (world.wasClicked(rows[index].row))
        {
            clicked = node;
        }
        if (world.wasDoubleClicked(rows[index].row))
        {
            doubleClicked = node;
        }
    }
    if (clicked)
    {
        choose(state, *clicked);
    }
    if (doubleClicked)
    {
        activate(state, edited, *doubleClicked);
    }

    // The keys walk the tree while the panel has them, as they do in Godot's.
    if (panel.focused() && !world.isEditing() && !world.isPopupOpen(scene(), menu) && !nodes.empty())
    {
        const auto current = std::ranges::find(nodes, selected, &Node::key);
        std::size_t index = current != nodes.end() ? static_cast<std::size_t>(current - nodes.begin()) : 0;
        std::optional<std::size_t> moved;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true))
        {
            moved = current != nodes.end() ? std::min(index + 1, nodes.size() - 1) : 0;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true))
        {
            moved = current != nodes.end() && index > 0 ? index - 1 : 0;
        }
        else if (current != nodes.end() && ImGui::IsKeyPressed(ImGuiKey_RightArrow, true) && current->expandable)
        {
            if (!current->expanded)
            {
                expanded[current->key] = true;
            }
            else if (index + 1 < nodes.size())
            {
                moved = index + 1;
            }
        }
        else if (current != nodes.end() && ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true))
        {
            if (current->expandable && current->expanded)
            {
                expanded[current->key] = false;
            }
            else
            {
                // To the folder around it.
                for (std::size_t above = index; above-- > 0;)
                {
                    if (nodes[above].depth < current->depth)
                    {
                        moved = above;
                        break;
                    }
                }
            }
        }
        else if (current != nodes.end() && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)))
        {
            activate(state, edited, *current);
        }
        if (moved)
        {
            choose(state, nodes[*moved]);
            // The chosen line stays in view.
            scene::UiScroll& scrolling = scene().get<scene::UiScroll>(scroll);
            const float top = static_cast<float>(*moved) * rowHeight;
            if (top < scrolling.offset.y)
            {
                scrolling.offset.y = top;
            }
            else if (viewHeight > 0.0f && top + rowHeight > scrolling.offset.y + viewHeight)
            {
                scrolling.offset.y = top + rowHeight - viewHeight;
            }
        }
    }

    // The menu of the row it opened on, and what it chose.
    if (world.isPopupOpen(scene(), menu))
    {
        if (const Node* const target = nodeOf(world.contextTarget()); target != nullptr && (!menuNode || menuNode->key != target->key))
        {
            menuNode = *target;
            if (target->kind == NodeKind::SubAsset)
            {
                world.closePopup(scene(), menu);
                menuNode.reset();
            }
            else
            {
                selected = target->key;
                openMenu(state, kit);
            }
        }
        fitMenu(menu, font * 15.0f);
    }
    answerMenu(state, edited);
    if (!world.isPopupOpen(scene(), menu))
    {
        menuNode.reset();
    }

    // An entity dropped on a folder.
    if (const ui::Drop* const dropped = world.dropped(); dropped != nullptr && dropped->type == "entity")
    {
        if (const Node* const folder = nodeOf(dropped->target); folder != nullptr && folder->kind == NodeKind::Folder)
        {
            dropEntity(state, edited, *folder, dropped->data);
        }
    }
}

bool containsIgnoringCase(std::string_view text, std::string_view part)
{
    return part.empty() || !std::ranges::search(text, part, [](char left, char right) {
                                return std::tolower(static_cast<unsigned char>(left)) ==
                                       std::tolower(static_cast<unsigned char>(right));
                            }).empty();
}

void drawAssetsPanel(ToolsState& state, scene::Scene& scene)
{
    DEVEX_PROFILE_SCOPE("FileSystem");
    if (!beginDockedPanel(state, assetsWindow))
    {
        return;
    }
    if (state.database == nullptr)
    {
        ImGui::TextDisabled("No project: set ApplicationConfig::project to import assets.");
        ImGui::End();
        return;
    }
    if (!state.uiKit)
    {
        state.uiKit = std::make_shared<EditorUiKit>(state.renderer, state.icons,
                                                    state.platform.baseDirectory() / "resources" / "fonts");
    }
    if (!state.fileSystemUi)
    {
        state.fileSystemUi = std::make_shared<FileSystemUi>();
    }
    state.fileSystemUi->update(state, *state.uiKit, scene, core::Duration(ImGui::GetIO().DeltaTime));
    ImGui::End();
}

void revealInFileSystem(ToolsState& state, std::string resource)
{
    state.assetToReveal = std::move(resource);
    focusPanel(state, assetsWindow);
}

void renderFileSystem(ToolsState& state, render::RenderWorld& world)
{
    if (state.fileSystemUi && state.uiKit)
    {
        state.fileSystemUi->panel.render(*state.uiKit, world, linearColor(themeColors().panel));
    }
}

} // namespace devex::tools::detail
