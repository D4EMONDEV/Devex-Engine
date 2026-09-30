// The scene tree, made with the interface of the engine as Godot's Scene dock is with its nodes: the
// entities as a tree whose rows only exist for the lines on screen, with thin guides from children
// to their parent. An entity dropped on a row goes before it, inside it or after it, as the part
// of the row it is let go on says.
#include "EditorUi.hpp"
#include "Selection.hpp"
#include "ToolsState.hpp"

#include <devex/core/Profiler.hpp>
#include <devex/asset/Project.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/Prefab.hpp>
#include <devex/scene/UiComponents.hpp>
#include <devex/tools/SceneCommands.hpp>

#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <optional>
#include <string>
#include <unordered_map>

namespace devex::tools::detail {

using scene::Entity;
using scene::UiRect;
using namespace rects;
using Button = PanelButton;

namespace {

// The image the panel is drawn into, among the interface surfaces of the editor.
constexpr std::uint32_t sceneTreeSurface = 5;
// Levels whose guides are drawn; deeper ones only indent.
constexpr std::size_t maxGuides = 16;
// The part of a row, from its top and from its bottom, that places a dropped entity beside it
// rather than inside it.
constexpr float dropEdge = 0.25f;

// A line of the tree.
struct TreeNode
{
    Entity entity;
    core::Uuid uuid;
    int depth = 0;
    bool hasChildren = false;
    bool expanded = false;
    // Hidden in the viewport, by itself or with an entity above it.
    bool hidden = false;
    bool ownHidden = false;
    bool fromPrefab = false;
    bool instanceRoot = false;
    bool missingPrefab = false;
    bool prefabEntity = false;
    // A component of the game's code it carries, whose file the code button opens.
    std::string gameComponent;
    // For each level above it, whether the guide of that level goes on below this line.
    std::vector<bool> continues;
    bool last = false;
};

// One line on screen and what it shows.
struct TreeRow
{
    Entity row;
    Entity arrow;
    Entity icon;
    Entity name;
    Entity field;
    Button eye;
    Button prefab;
    Button code;
    Entity mark;
    std::array<Entity, maxGuides> guides{};
    Entity ownGuide;
    Entity stub;
};

// Where a dropped entity goes.
enum class DropPlace : std::uint8_t
{
    Before,
    Inside,
    After,
};

[[nodiscard]] DropPlace placeOf(float at) noexcept
{
    return at < dropEdge ? DropPlace::Before : at > 1.0f - dropEdge ? DropPlace::After : DropPlace::Inside;
}

// Whether the entity or one of its descendants matches the filter.
[[nodiscard]] bool matchesFilter(const scene::Scene& scene, Entity entity, std::string_view filter)
{
    if (containsIgnoringCase(scene.name(entity), filter))
    {
        return true;
    }
    for (Entity child = scene.firstChild(entity); child.isValid(); child = scene.nextSibling(child))
    {
        if (matchesFilter(scene, child, filter))
        {
            return true;
        }
    }
    return false;
}

// Selects as a click in the tree does: Ctrl adds or removes, Shift selects the rows between the
// anchor and this one, as the tree listed them.
void clickRow(ToolsState& state, core::Uuid uuid)
{
    if (state.input.shift() && !state.rangeAnchor.isNil())
    {
        const auto from = std::ranges::find(state.hierarchyOrder, state.rangeAnchor);
        const auto to = std::ranges::find(state.hierarchyOrder, uuid);
        if (from != state.hierarchyOrder.end() && to != state.hierarchyOrder.end())
        {
            std::vector<core::Uuid> range(std::min(from, to), std::max(from, to) + 1);
            // The clicked row ends active.
            std::erase(range, uuid);
            range.push_back(uuid);
            if (!state.input.ctrl())
            {
                state.selection.clear();
            }
            for (const core::Uuid entity : range)
            {
                state.selection.add(entity);
            }
            return;
        }
    }
    if (state.input.ctrl())
    {
        state.selection.toggle(uuid);
    }
    else
    {
        state.selection.set(uuid);
    }
    state.rangeAnchor = uuid;
}

// Moves the dropped entity under the new parent (nil for the roots), before a sibling or last; a
// dropped entity that is selected brings the rest of the selection with it, in its order. The
// entities of prefab instances stay, and nothing moves under itself.
void moveDropped(ToolsState& state, scene::Scene& scene, core::Uuid dropped, core::Uuid newParent, core::Uuid before)
{
    const Entity target = scene.findEntity(newParent);
    const Entity beforeEntity = before.isNil() ? Entity{} : scene.findEntity(before);
    std::vector<Entity> moved;
    if (state.selection.contains(dropped))
    {
        moved = selectedRoots(scene, state.selection);
    }
    else if (const Entity entity = scene.findEntity(dropped); entity.isValid())
    {
        moved.push_back(entity);
    }
    std::vector<std::unique_ptr<Command>> commands;
    for (const Entity entity : moved)
    {
        bool underItself = false;
        for (Entity ancestor = target; ancestor.isValid(); ancestor = scene.parent(ancestor))
        {
            underItself |= ancestor == entity;
        }
        // Where it already is: before its own next sibling, or last already.
        const bool inPlace = scene.parent(entity) == target &&
                             (beforeEntity == entity || scene.nextSibling(entity) == beforeEntity);
        if (!underItself && !inPlace && !scene::isInsidePrefabInstance(scene, entity))
        {
            commands.push_back(makeReparentCommand(scene.uuid(entity), newParent, beforeEntity == entity ? core::Uuid{} : before));
        }
    }
    if (commands.size() == 1)
    {
        state.pendingCommand = std::move(commands.front());
    }
    else if (!commands.empty())
    {
        const std::size_t count = commands.size();
        state.pendingCommand = makeCompositeCommand(std::move(commands), std::format("Move {} entities", count));
    }
}

// The file of the code that declares a component of the game, found by its class or struct.
[[nodiscard]] std::optional<std::filesystem::path> sourceOf(const ToolsState& state, std::string_view component)
{
    if (state.database == nullptr)
    {
        return std::nullopt;
    }
    const std::filesystem::path code = state.database->project().codeDirectory();
    const std::string classLine = std::format("class {}", component);
    const std::string structLine = std::format("struct {}", component);
    std::error_code error;
    for (auto entry = std::filesystem::recursive_directory_iterator(code, error);
         !error && entry != std::filesystem::recursive_directory_iterator(); entry.increment(error))
    {
        const std::string name = core::toUtf8(entry->path().filename());
        if (entry->is_directory(error) && (name == "obj" || name == "bin" || name.starts_with('.')))
        {
            entry.disable_recursion_pending();
            continue;
        }
        const std::filesystem::path extension = entry->path().extension();
        if (extension != ".cs" && extension != ".hpp" && extension != ".h" && extension != ".cpp")
        {
            continue;
        }
        if (const core::Result<std::string> text = core::readTextFile(entry->path());
            text && (text->find(classLine) != std::string::npos || text->find(structLine) != std::string::npos))
        {
            return entry->path();
        }
    }
    return std::nullopt;
}

} // namespace

// The panel and the entities the code reads and changes.
struct SceneTreeUi : PanelBuilder
{
    SceneTreeUi()
        : PanelBuilder(sceneTreeSurface)
    {
    }

    bool built = false;
    float rowHeight = 26.0f;
    float indent = 18.0f;
    float arrow = 20.0f;

    Button create;
    Entity filter;
    Entity list;
    Entity scroll;
    Entity lines;
    std::vector<TreeRow> rows;
    std::vector<std::optional<std::size_t>> rowNodes;

    Entity menu;
    Button createChild;
    Button frame;
    Button rename;
    Button hide;
    Entity editSeparator;
    Button cut;
    Button copy;
    Button paste;
    Button duplicate;
    Entity prefabSeparator;
    Button openPrefab;
    Button makeLocal;
    Button saveAsPrefab;
    Entity deleteSeparator;
    Button remove;
    Button createRoot;
    // What the menu opened on: a row, or the empty space under them.
    std::optional<core::Uuid> menuTarget;
    bool menuOnRoots = false;

    std::vector<TreeNode> nodes;
    std::unordered_map<core::Uuid, bool> expanded;
    core::Uuid shownActive;
    core::Uuid renaming;
    // The components of the game, whose code a row opens, found once for each state of the registry.
    std::vector<const scene::ComponentType*> gameTypes;
    std::uint64_t gameTypesGeneration = ~std::uint64_t{0};

    void build(ToolsState& state, EditorUiKit& kit);
    void gather(const ToolsState& state, const scene::Scene& edited);
    void addNode(const ToolsState& state, const scene::Scene& edited, Entity entity, int depth, bool ancestorHidden,
                 std::vector<bool>& continues, bool last);
    void fillRows(ToolsState& state, EditorUiKit& kit, const scene::Scene& edited);
    void update(ToolsState& state, EditorUiKit& kit, scene::Scene& edited, core::Duration delta);
    void openMenu(ToolsState& state, const scene::Scene& edited);
    void answerMenu(ToolsState& state, scene::Scene& edited);
    void drop(ToolsState& state, scene::Scene& edited, const ui::Drop& dropped);

    [[nodiscard]] const TreeNode* nodeOfRow(Entity row) const
    {
        for (std::size_t index = 0; index < rows.size(); ++index)
        {
            if (rows[index].row == row && rowNodes[index])
            {
                return &nodes[*rowNodes[index]];
            }
        }
        return nullptr;
    }

    [[nodiscard]] float leftOf(int depth) const noexcept
    {
        return font * 0.3f + static_cast<float>(depth) * indent;
    }
};

void SceneTreeUi::build(ToolsState& state, EditorUiKit& kit)
{
    built = true;
    const float line = font * 2.0f;
    const Entity root = add({}, "Scene", whole());
    scene().add<scene::UiLayout>(root, scene::UiLayout{.kind = scene::UiLayoutKind::Column,
                                                       .spacing = font * 0.4f,
                                                       .align = scene::TextAlign::Left});
    const Entity toolbar = add(root, "Toolbar", wide(line));
    scene().add<scene::UiLayout>(toolbar, scene::UiLayout{.kind = scene::UiLayoutKind::Row,
                                                          .spacing = font * 0.3f,
                                                          .align = scene::TextAlign::Left});
    create = button(kit, toolbar, Icon::Plus, "", "flat", line, line);
    tooltip(create.entity, "Create an entity, under the chosen one");
    filter = searchField(kit, toolbar, grow(line), state.hierarchyFilter, "Filter Entities");

    list = add(root, "List", whole(), "list");
    scene().add<scene::UiImage>(list);
    scroll = add(list, "Scroll", whole(math::Vec4{2.0f}), "scroll");
    scene().add<scene::UiScroll>(scroll, scene::UiScroll{});
    lines = add(scroll, "Lines", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 0.0f}, .offsetMin = {0.0f, 0.0f}, .offsetMax = {-8.0f, 0.0f}});
    scene().add<scene::UiVirtualList>(lines);

    // The menu of a row, or of the empty space under the rows.
    menu = PanelBuilder::menu("Scene menu", font * 16.0f);
    createChild = menuItem(kit, menu, Icon::Plus, "Create Child...");
    createRoot = menuItem(kit, menu, Icon::Plus, "Create Entity...");
    frame = menuItem(kit, menu, Icon::Crosshair, "Frame", "F");
    rename = menuItem(kit, menu, Icon::Pencil, "Rename", "F2");
    hide = menuItem(kit, menu, Icon::EyeOff, "Hide in Viewport", "H");
    editSeparator = menuSeparator(menu);
    cut = menuItem(kit, menu, Icon::Scissors, "Cut", "Ctrl+X");
    copy = menuItem(kit, menu, Icon::Copy, "Copy", "Ctrl+C");
    paste = menuItem(kit, menu, Icon::ClipboardPaste, "Paste", "Ctrl+V");
    duplicate = menuItem(kit, menu, Icon::CopyPlus, "Duplicate", "Ctrl+D");
    prefabSeparator = menuSeparator(menu);
    openPrefab = menuItem(kit, menu, Icon::ExternalLink, "Open Prefab");
    makeLocal = menuItem(kit, menu, Icon::Unlink, "Make Local");
    saveAsPrefab = menuItem(kit, menu, Icon::Package, "Save as Prefab...");
    deleteSeparator = menuSeparator(menu);
    remove = menuItem(kit, menu, Icon::Trash, "Delete", "Delete");

    // The empty space under the rows takes entities to the roots, and has its menu.
    const math::Vec4 accent = linearColor(themeColors().accent);
    scene().add<scene::UiDropTarget>(list, scene::UiDropTarget{.accepts = {"entity", "asset"},
                                                               .highlightColor = math::Vec4{accent.x, accent.y, accent.z, 0.08f}});
    scene().add<scene::UiContextMenu>(list, scene::UiContextMenu{.popup = scene().reference(menu)});

    // The entities leave for the fields of the inspector; entities and the
    // files of FileSystem come in.
    panel.setKeyboardNavigation(false);
    panel.setDragOut([](const ui::Carried& carried) -> std::optional<EditorDrag> {
        const std::optional<core::Uuid> uuid = core::Uuid::parse(carried.data);
        if (carried.type != "entity" || !uuid)
        {
            return std::nullopt;
        }
        EditorDrag drag{.type = entityPayload, .label = carried.label};
        drag.payload.resize(uuid->bytes().size());
        std::memcpy(drag.payload.data(), uuid->bytes().data(), uuid->bytes().size());
        return drag;
    });
    panel.setDragIn([](const EditorDrag& payload) -> std::optional<std::pair<std::string, std::string>> {
        if (payload.is(entityPayload, 16))
        {
            std::array<std::uint8_t, 16> bytes{};
            std::memcpy(bytes.data(), payload.payload.data(), bytes.size());
            return std::pair{std::string("entity"), uuidFromBytes(bytes).toString()};
        }
        if (payload.is(assetPayload, sizeof(AssetPayload)))
        {
            AssetPayload asset;
            std::memcpy(&asset, payload.payload.data(), sizeof(asset));
            // Only what the tree places: models and prefabs.
            if (asset.type == asset::AssetType::Model || asset.type == asset::AssetType::Scene)
            {
                return std::pair{std::string("asset"),
                                 std::format("{}|{}", uuidFromBytes(asset.uuid).toString(), static_cast<int>(asset.type))};
            }
        }
        return std::nullopt;
    });
}

void SceneTreeUi::addNode(const ToolsState& state, const scene::Scene& edited, Entity entity, int depth, bool ancestorHidden,
                          std::vector<bool>& continues, bool last)
{
    const core::Uuid uuid = edited.uuid(entity);
    const bool filtering = !state.hierarchyFilter.empty();
    TreeNode node{.entity = entity, .uuid = uuid, .depth = depth, .continues = continues, .last = last};
    node.hasChildren = edited.firstChild(entity).isValid();
    node.expanded = node.hasChildren && (filtering || [&] {
                        const auto found = expanded.find(uuid);
                        return found == expanded.end() || found->second;
                    }());
    node.ownHidden = state.hiddenEntities.contains(uuid);
    node.hidden = ancestorHidden || node.ownHidden;
    node.fromPrefab = scene::isInsidePrefabInstance(edited, entity);
    if (const scene::PrefabInstance* const instance = edited.tryGet<scene::PrefabInstance>(entity))
    {
        node.instanceRoot = true;
        node.missingPrefab = !instance->resolved;
    }
    node.prefabEntity = edited.has<scene::PrefabEntity>(entity);
    for (const scene::ComponentType* const type : gameTypes)
    {
        if (type->find(edited, entity) != nullptr)
        {
            node.gameComponent = std::string(type->name());
            break;
        }
    }
    const bool open = node.expanded;
    const bool hidden = node.hidden;
    nodes.push_back(std::move(node));
    if (!open)
    {
        return;
    }
    std::vector<Entity> children;
    for (Entity child = edited.firstChild(entity); child.isValid(); child = edited.nextSibling(child))
    {
        if (!filtering || matchesFilter(edited, child, state.hierarchyFilter))
        {
            children.push_back(child);
        }
    }
    // The guide of this level goes on below the rows of a child that has siblings after it.
    continues.push_back(!last);
    for (std::size_t index = 0; index < children.size(); ++index)
    {
        addNode(state, edited, children[index], depth + 1, hidden, continues, index + 1 == children.size());
    }
    continues.pop_back();
}

void SceneTreeUi::gather(const ToolsState& state, const scene::Scene& edited)
{
    DEVEX_PROFILE_SCOPE("Scene tree nodes");
    const scene::ComponentRegistry& registry = scene::componentRegistry();
    if (gameTypesGeneration != registry.generation())
    {
        gameTypesGeneration = registry.generation();
        gameTypes.clear();
        for (const scene::ComponentType& type : registry.types())
        {
            if (isGameComponent(type.name()))
            {
                gameTypes.push_back(&type);
            }
        }
    }
    nodes.clear();
    std::vector<Entity> roots;
    for (Entity root = edited.firstRoot(); root.isValid(); root = edited.nextSibling(root))
    {
        if (state.hierarchyFilter.empty() || matchesFilter(edited, root, state.hierarchyFilter))
        {
            roots.push_back(root);
        }
    }
    std::vector<bool> continues;
    for (std::size_t index = 0; index < roots.size(); ++index)
    {
        addNode(state, edited, roots[index], 0, false, continues, index + 1 == roots.size());
    }
}

void SceneTreeUi::fillRows(ToolsState& state, EditorUiKit& kit, const scene::Scene& edited)
{
    DEVEX_PROFILE_SCOPE("Scene tree rows");
    const ThemeColors& colors = themeColors();
    const bool editor = state.mode == ToolsMode::Editor;
    const float iconSize = font * 1.15f;
    const float button = rowHeight * 0.85f;
    while (rows.size() * rowHeight < std::max(panel.size().y, 300.0f) + rowHeight * 2.0f)
    {
        TreeRow row;
        row.row = add(lines, "Row", fixed({1.0f, rowHeight}), "soft_row");
        scene().add<scene::UiImage>(row.row);
        scene().add<scene::UiButton>(row.row);
        scene().add<scene::UiDragSource>(row.row, scene::UiDragSource{.type = "entity"});
        scene().add<scene::UiDropTarget>(row.row, scene::UiDropTarget{.accepts = {"entity", "asset"}, .highlightColor = math::Vec4{0.0f}});
        scene().add<scene::UiContextMenu>(row.row, scene::UiContextMenu{.popup = scene().reference(menu)});
        for (Entity& guide : row.guides)
        {
            guide = add(row.row, "Guide", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 1.0f}, .visible = false}, "guide");
            scene().add<scene::UiImage>(guide, scene::UiImage{.raycastTarget = false});
        }
        row.ownGuide = add(row.row, "Guide", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 0.0f}, .visible = false}, "guide");
        scene().add<scene::UiImage>(row.ownGuide, scene::UiImage{.raycastTarget = false});
        row.stub = add(row.row, "Guide", UiRect{.anchorMin = {0.0f, 0.5f}, .anchorMax = {0.0f, 0.5f}, .visible = false}, "guide");
        scene().add<scene::UiImage>(row.stub, scene::UiImage{.raycastTarget = false});
        row.mark = add(row.row, "Drop", UiRect{.visible = false}, "drop_line");
        scene().add<scene::UiImage>(row.mark, scene::UiImage{.raycastTarget = false});
        row.arrow = add(row.row, "Arrow", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 1.0f}});
        scene().add<scene::UiFoldout>(row.arrow);
        row.icon = icon(kit, row.row, UiRect{.anchorMin = {0.0f, 0.5f}, .anchorMax = {0.0f, 0.5f}}, Icon::Box, {});
        row.name = text(row.row, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 1.0f}}, "", "text");
        row.field = field(row.row, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .visible = false}, "", "Name");
        // The buttons at the end of the row, placed by hand from its right edge.
        const auto endButton = [&](Icon glyph, int slot) {
            Button made;
            made.entity = add(row.row, "Button",
                              UiRect{.anchorMin = {1.0f, 0.5f},
                                     .anchorMax = {1.0f, 0.5f},
                                     .offsetMin = {-font * 0.25f - button * static_cast<float>(slot + 1), -button * 0.5f},
                                     .offsetMax = {-font * 0.25f - button * static_cast<float>(slot), button * 0.5f}},
                              "side");
            scene().add<scene::UiImage>(made.entity);
            scene().add<scene::UiButton>(made.entity);
            made.icon = icon(kit, made.entity, whole(math::Vec4{button * 0.2f}), glyph, "icon_dim");
            return made;
        };
        row.eye = endButton(Icon::Eye, 0);
        row.prefab = endButton(Icon::ExternalLink, 1);
        row.code = endButton(Icon::FileCode, 2);
        rows.push_back(row);
    }

    // The rows show the lines the list will place them at: the first one the scroll leaves in view.
    scene::UiVirtualList& virtualList = scene().get<scene::UiVirtualList>(lines);
    virtualList.itemCount = static_cast<std::uint32_t>(nodes.size());
    virtualList.itemSize = rowHeight;
    const scene::UiScroll& scrolled = scene().get<scene::UiScroll>(scroll);
    const std::size_t first = nodes.empty() ? 0
                                            : std::min(static_cast<std::size_t>(std::max(scrolled.offset.y, 0.0f) / rowHeight),
                                                       nodes.size() - 1);
    rowNodes.assign(rows.size(), std::nullopt);

    // Where what the pointer carries would land, from where it was over a row at the last update.
    ui::UiWorld& world = panel.world();
    const Entity target = world.carried() != nullptr ? world.dropTarget() : Entity{};
    std::optional<DropPlace> place;
    if (target.isValid() && target != list && !world.canvases().empty())
    {
        if (const ui::LaidOutRect* const rect = world.canvases().front().layout.find(target))
        {
            place = placeOf((panel.input().pointer.y - rect->min.y) / std::max(rect->size().y, 1.0f));
            // A file of FileSystem goes inside the row it is dropped on.
            if (world.carried()->type == "asset")
            {
                place = DropPlace::Inside;
            }
        }
    }

    for (std::size_t index = 0; index < rows.size(); ++index)
    {
        const TreeRow& row = rows[index];
        const std::size_t at = first + index;
        if (at >= nodes.size())
        {
            continue;
        }
        rowNodes[index] = at;
        const TreeNode& node = nodes[at];
        const std::string& name = edited.name(node.entity);
        const bool selected = state.selection.contains(node.uuid);
        scene().get<UiRect>(row.row).style = selected ? "soft_row_selected" : "soft_row";
        const float left = leftOf(node.depth);

        // The guides of the levels above, and the one that joins this row to its parent.
        for (std::size_t level = 0; level < row.guides.size(); ++level)
        {
            UiRect& guide = scene().get<UiRect>(row.guides[level]);
            // Level 0 is the roots, which have no guide; the guide of level n stands under the arrow
            // of the parent at depth n - 1.
            guide.visible = level >= 1 && level < node.continues.size() && node.continues[level];
            const float x = leftOf(static_cast<int>(level) - 1) + arrow * 0.5f;
            guide.offsetMin = {x, 0.0f};
            guide.offsetMax = {x + 1.0f, 0.0f};
        }
        UiRect& ownGuide = scene().get<UiRect>(row.ownGuide);
        UiRect& stub = scene().get<UiRect>(row.stub);
        ownGuide.visible = node.depth >= 1;
        stub.visible = node.depth >= 1;
        if (node.depth >= 1)
        {
            const float x = leftOf(node.depth - 1) + arrow * 0.5f;
            ownGuide.offsetMin = {x, 0.0f};
            ownGuide.offsetMax = {x + 1.0f, node.last ? rowHeight * 0.5f : rowHeight};
            stub.offsetMin = {x, -0.5f};
            stub.offsetMax = {left + (node.hasChildren ? arrow * 0.2f : arrow * 0.85f), 0.5f};
        }

        UiRect& arrowRect = scene().get<UiRect>(row.arrow);
        arrowRect.visible = node.hasChildren && state.hierarchyFilter.empty();
        arrowRect.offsetMin = {left, 0.0f};
        arrowRect.offsetMax = {left + arrow, 0.0f};
        scene::UiFoldout& foldout = scene().get<scene::UiFoldout>(row.arrow);
        foldout.expanded = node.expanded;
        foldout.arrowColor = linearColor(colors.textDim);

        const EntityIcon look = entityIcon(edited, node.entity);
        UiRect& iconRect = scene().get<UiRect>(row.icon);
        iconRect.offsetMin = {left + arrow, -iconSize * 0.5f};
        iconRect.offsetMax = {left + arrow + iconSize, iconSize * 0.5f};
        scene::UiImage& glyph = scene().get<scene::UiImage>(row.icon);
        glyph.texture = kit.icon(iconOf(look.icon));
        glyph.color = linearColor(node.hidden ? colors.textDim : look.color);

        // The name, in the colour of prefabs for what comes from one, in red for an instance whose
        // prefab is missing.
        const float nameLeft = left + arrow + iconSize + font * 0.45f;
        const std::string shown = name.empty() ? "(unnamed)" : name;
        UiRect& nameRect = scene().get<UiRect>(row.name);
        nameRect.visible = renaming != node.uuid;
        nameRect.offsetMin = {nameLeft, 0.0f};
        nameRect.offsetMax = {nameLeft + kit.textWidth(EditorUiKit::regularFont(), shown, font) + 2.0f, 0.0f};
        nameRect.style = node.missingPrefab                ? "error"
                         : node.hidden || name.empty()     ? "dim"
                         : node.prefabEntity || node.instanceRoot ? "prefab"
                                                           : "text";
        scene::UiText& nameText = scene().get<scene::UiText>(row.name);
        if (nameText.text != shown)
        {
            nameText.text = shown;
        }
        if (node.missingPrefab)
        {
            tooltip(row.name, "The prefab of this instance is missing");
        }

        // The buttons at the end: the eye, the prefab of an instance, the code of the game.
        const auto show = [&](const Button& button, bool visible) {
            UiRect& rect = scene().get<UiRect>(button.entity);
            rect.visible = visible;
            rect.style = selected ? "row_button_selected" : "row_button";
        };
        show(row.eye, editor);
        show(row.prefab, editor && node.instanceRoot && !node.missingPrefab);
        show(row.code, editor && !node.gameComponent.empty() && state.database != nullptr);
        scene().get<scene::UiImage>(row.eye.icon).texture = kit.icon(node.ownHidden ? Icon::EyeOff : Icon::Eye);
        scene().get<UiRect>(row.eye.icon).style = node.ownHidden ? "icon" : "icon_dim";
        scene().get<UiRect>(row.eye.icon).opacity = node.hidden && !node.ownHidden ? 0.45f : 1.0f;
        tooltip(row.eye.entity, node.ownHidden ? "Show in the viewport (H)"
                                : node.hidden  ? "Hidden with an entity above it"
                                               : "Hide in the viewport (H)");
        tooltip(row.prefab.entity, "Open the prefab of this instance");
        if (!node.gameComponent.empty())
        {
            tooltip(row.code.entity, std::format("Open the code of {}", node.gameComponent));
        }

        UiRect& fieldRect = scene().get<UiRect>(row.field);
        fieldRect.visible = renaming == node.uuid;
        fieldRect.offsetMin = {nameLeft - font * 0.3f, 2.0f};
        fieldRect.offsetMax = {-font * 0.3f - button * 3.0f, -2.0f};

        // What is dragged: this entity, or the whole selection it belongs to.
        scene::UiDragSource& source = scene().get<scene::UiDragSource>(row.row);
        source.interactable = !node.fromPrefab && renaming != node.uuid;
        source.data = node.uuid.toString();
        source.label = selected && state.selection.size() > 1 ? std::format("{} entities", state.selection.size()) : shown;

        // Where it would land: a line above or below the row, or the row lit when inside it.
        UiRect& mark = scene().get<UiRect>(row.mark);
        mark.visible = place.has_value() && row.row == target;
        if (mark.visible)
        {
            if (*place == DropPlace::Inside)
            {
                mark = UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {0.0f, 0.0f}, .offsetMax = {0.0f, 0.0f}, .style = "drop_into"};
            }
            else
            {
                const float y = *place == DropPlace::Before ? 0.0f : 1.0f;
                mark = UiRect{.anchorMin = {0.0f, y}, .anchorMax = {1.0f, y}, .offsetMin = {left, -1.0f}, .offsetMax = {0.0f, 1.0f}, .style = "drop_line"};
            }
        }
    }
}

void SceneTreeUi::openMenu(ToolsState& state, const scene::Scene& edited)
{
    const bool editor = state.mode == ToolsMode::Editor;
    const bool onRow = !menuOnRoots;
    const auto show = [&](const Button& button, bool visible) { scene().get<UiRect>(button.entity).visible = visible; };
    const Entity entity = onRow && menuTarget ? edited.findEntity(*menuTarget) : Entity{};
    const bool single = state.selection.size() <= 1;
    show(createChild, onRow);
    enable(createChild, single);
    show(createRoot, !onRow);
    show(frame, onRow && editor);
    show(rename, onRow);
    show(hide, onRow && editor);
    const bool hidden = std::ranges::all_of(state.selection.entities(),
                                            [&state](core::Uuid selected) { return state.hiddenEntities.contains(selected); });
    relabel(*state.uiKit, hide, hidden ? "Show in Viewport" : "Hide in Viewport");
    scene().get<scene::UiImage>(hide.icon).texture = state.uiKit->icon(hidden ? Icon::Eye : Icon::EyeOff);
    scene().get<UiRect>(editSeparator).visible = onRow;
    const bool deletable = canDeleteSelection(state, edited);
    show(cut, onRow);
    enable(cut, deletable);
    show(copy, onRow);
    show(duplicate, onRow);
    show(paste, true);

    const bool editing = state.playState == PlayState::Editing;
    const Entity instance = entity.isValid() ? scene::owningPrefabInstance(edited, entity) : Entity{};
    const bool prefabItems = onRow && editor;
    scene().get<UiRect>(prefabSeparator).visible = prefabItems;
    show(openPrefab, prefabItems && instance.isValid() && single);
    enable(openPrefab, editing);
    show(makeLocal, prefabItems && instance.isValid() && single);
    enable(makeLocal, editing && instance.isValid() && edited.get<scene::PrefabInstance>(instance).resolved);
    show(saveAsPrefab, prefabItems);
    enable(saveAsPrefab, single && editing && entity.isValid() && !scene::isInsidePrefabInstance(edited, entity) &&
                             state.database != nullptr);
    scene().get<UiRect>(deleteSeparator).visible = onRow;
    show(remove, onRow);
    enable(remove, deletable);
}

void SceneTreeUi::answerMenu(ToolsState& state, scene::Scene& edited)
{
    ui::UiWorld& world = panel.world();
    const core::Uuid uuid = menuTarget.value_or(core::Uuid{});
    const Entity entity = edited.findEntity(uuid);
    if (world.wasClicked(createChild.entity))
    {
        openCreateEntity(state, uuid);
    }
    else if (world.wasClicked(createRoot.entity))
    {
        openCreateEntity(state, core::Uuid{});
    }
    else if (world.wasClicked(frame.entity))
    {
        frameSelection(state, edited);
    }
    else if (world.wasClicked(rename.entity))
    {
        startRename(state, uuid);
    }
    else if (world.wasClicked(hide.entity))
    {
        toggleSelectionHidden(state, edited);
    }
    else if (world.wasClicked(cut.entity))
    {
        cutSelection(state, edited);
    }
    else if (world.wasClicked(copy.entity))
    {
        copySelection(state, edited);
    }
    else if (world.wasClicked(paste.entity))
    {
        if (menuOnRoots)
        {
            state.selection.clear();
        }
        pasteEntities(state, edited);
    }
    else if (world.wasClicked(duplicate.entity))
    {
        duplicateSelection(state, edited);
    }
    else if (world.wasClicked(openPrefab.entity) && entity.isValid())
    {
        if (const Entity instance = scene::owningPrefabInstance(edited, entity); instance.isValid())
        {
            state.prefabToOpen = edited.get<scene::PrefabInstance>(instance).prefab;
        }
    }
    else if (world.wasClicked(makeLocal.entity) && entity.isValid())
    {
        if (const Entity instance = scene::owningPrefabInstance(edited, entity); instance.isValid())
        {
            state.pendingCommand = makeReplaceEntityTreeCommand(edited.uuid(instance), scene::saveUnpackedEntityTree(edited, instance),
                                                                "Make instance local");
        }
    }
    else if (world.wasClicked(saveAsPrefab.entity))
    {
        showSaveAsPrefabDialog(state, edited, uuid);
    }
    else if (world.wasClicked(remove.entity))
    {
        deleteSelection(state, edited);
    }
}

void SceneTreeUi::drop(ToolsState& state, scene::Scene& edited, const ui::Drop& dropped)
{
    // On a row, before, inside or after it; under the rows, last among the roots.
    const TreeNode* const node = dropped.target == list ? nullptr : nodeOfRow(dropped.target);
    if (dropped.target != list && node == nullptr)
    {
        return;
    }
    if (dropped.type == "asset")
    {
        const std::size_t bar = dropped.data.find('|');
        const std::optional<core::Uuid> uuid = core::Uuid::parse(std::string_view(dropped.data).substr(0, bar));
        if (!uuid || bar == std::string::npos)
        {
            return;
        }
        const core::Uuid parent = node != nullptr ? node->uuid : core::Uuid{};
        if (std::stoi(dropped.data.substr(bar + 1)) == static_cast<int>(asset::AssetType::Model))
        {
            requestInstantiateModel(state, asset::AssetId{*uuid}, parent);
        }
        else
        {
            requestInstantiatePrefab(state, asset::AssetId{*uuid}, parent);
        }
        return;
    }
    const std::optional<core::Uuid> uuid = core::Uuid::parse(dropped.data);
    if (!uuid)
    {
        return;
    }
    if (node == nullptr)
    {
        moveDropped(state, edited, *uuid, core::Uuid{}, core::Uuid{});
        return;
    }
    const Entity parent = edited.parent(node->entity);
    const core::Uuid parentUuid = parent.isValid() ? edited.uuid(parent) : core::Uuid{};
    switch (placeOf(dropped.at.y))
    {
    case DropPlace::Before:
        moveDropped(state, edited, *uuid, parentUuid, node->uuid);
        break;
    case DropPlace::Inside:
        if (*uuid != node->uuid)
        {
            moveDropped(state, edited, *uuid, node->uuid, core::Uuid{});
            expanded[node->uuid] = true;
        }
        break;
    case DropPlace::After: {
        const Entity next = edited.nextSibling(node->entity);
        moveDropped(state, edited, *uuid, parentUuid, next.isValid() ? edited.uuid(next) : core::Uuid{});
        break;
    }
    }
}

void SceneTreeUi::update(ToolsState& state, EditorUiKit& kit, scene::Scene& edited, core::Duration delta)
{
    const ThemeColors& colors = themeColors();
    kit.refreshTheme(colors);
    font = state.theme.fontSize;
    rowHeight = std::round(font * 1.85f);
    indent = std::round(font * 1.3f);
    arrow = std::round(font * 1.4f);
    if (!built)
    {
        build(state, kit);
    }
    styleTooltips(colors);
    ui::UiWorld& world = panel.world();
    state.hierarchyFilter = scene().get<scene::UiText>(filter).text;

    // Renaming from F2 or the menu: the field of the row takes the keyboard, the name selected.
    if (state.focusRename && !state.renamedEntity.isNil())
    {
        renaming = state.renamedEntity;
        state.focusRename = false;
        for (Entity above = edited.parent(edited.findEntity(renaming)); above.isValid(); above = edited.parent(above))
        {
            expanded[edited.uuid(above)] = true;
        }
    }
    if (state.renamedEntity.isNil())
    {
        renaming = {};
    }

    gather(state, edited);
    state.hierarchyOrder.clear();
    for (const TreeNode& node : nodes)
    {
        state.hierarchyOrder.push_back(node.uuid);
    }

    // The active entity, chosen from the viewport or the keys, comes into view.
    const ui::LaidOutRect* const view = world.canvases().empty() ? nullptr : world.canvases().front().layout.find(scroll);
    const float viewHeight = view != nullptr ? view->size().y : 0.0f;
    const core::Uuid active = !renaming.isNil() ? renaming : state.selection.active();
    if (active != shownActive && viewHeight > 0.0f)
    {
        shownActive = active;
        const auto found = std::ranges::find(nodes, active, &TreeNode::uuid);
        if (found != nodes.end())
        {
            scene::UiScroll& scrolling = scene().get<scene::UiScroll>(scroll);
            const float top = static_cast<float>(found - nodes.begin()) * rowHeight;
            if (top < scrolling.offset.y)
            {
                scrolling.offset.y = top;
            }
            else if (top + rowHeight > scrolling.offset.y + viewHeight)
            {
                scrolling.offset.y = top + rowHeight - viewHeight;
            }
        }
    }
    scene().get<scene::UiScroll>(scroll).speed = rowHeight * 3.0f;
    fillRows(state, kit, edited);

    // The field of the renamed row starts with the name, selected.
    std::optional<std::size_t> renameRow;
    for (std::size_t index = 0; index < rows.size(); ++index)
    {
        if (rowNodes[index] && nodes[*rowNodes[index]].uuid == renaming && !renaming.isNil())
        {
            renameRow = index;
        }
    }
    if (renameRow && world.editedField() != rows[*renameRow].field)
    {
        scene().get<scene::UiText>(rows[*renameRow].field).text = edited.name(edited.findEntity(renaming));
        world.startEditing(scene(), rows[*renameRow].field);
    }

    panel.update(kit, delta, UiPanel::zoomFor(font));
    state.hierarchyFocused = panel.focused() && !world.isEditing();

    // The rename ends with Enter or when the field loses the keyboard, and is dropped on Escape.
    if (renameRow && world.editedField() != rows[*renameRow].field)
    {
        const std::string typed = scene().get<scene::UiText>(rows[*renameRow].field).text;
        const std::string before = edited.name(edited.findEntity(renaming));
        if (!panel.input().cancelPressed && !typed.empty() && typed != before)
        {
            state.pendingCommand = makeRenameCommand(renaming, before, typed);
        }
        state.renamedEntity = {};
        renaming = {};
    }

    // Clicks and double clicks on the rows, and the buttons at their end.
    for (std::size_t index = 0; index < rows.size(); ++index)
    {
        if (!rowNodes[index])
        {
            continue;
        }
        const TreeRow& row = rows[index];
        const TreeNode& node = nodes[*rowNodes[index]];
        if (world.wasChanged(row.arrow))
        {
            expanded[node.uuid] = !node.expanded;
        }
        if (world.wasClicked(row.eye.entity))
        {
            if (!state.hiddenEntities.erase(node.uuid))
            {
                state.hiddenEntities.insert(node.uuid);
            }
            saveEditorSettings(state);
        }
        else if (world.wasClicked(row.prefab.entity))
        {
            if (state.playState == PlayState::Editing)
            {
                state.prefabToOpen = edited.get<scene::PrefabInstance>(node.entity).prefab;
            }
        }
        else if (world.wasClicked(row.code.entity))
        {
            if (const std::optional<std::filesystem::path> file = sourceOf(state, node.gameComponent))
            {
                openTextFile(state, *file);
            }
            else
            {
                DEVEX_LOG_WARNING("The code of {} was not found in the code folder", node.gameComponent);
            }
        }
        else if (world.wasClicked(row.row))
        {
            clickRow(state, node.uuid);
            if (world.wasDoubleClicked(row.row) && state.mode == ToolsMode::Editor)
            {
                frameSelection(state, edited);
            }
        }
    }
    if (world.wasClicked(create.entity))
    {
        openCreateEntity(state, state.selection.size() == 1 ? state.selection.active() : core::Uuid{});
    }

    // The keys walk the tree while the panel has them, as they do in Godot's.
    if (state.hierarchyFocused && renaming.isNil() && !nodes.empty() && !world.isPopupOpen(scene(), menu))
    {
        const auto current = std::ranges::find(nodes, state.selection.active(), &TreeNode::uuid);
        const std::size_t at = current != nodes.end() ? static_cast<std::size_t>(current - nodes.begin()) : 0;
        std::optional<std::size_t> moved;
        if (state.input.pressed(platform::Key::Down, true))
        {
            moved = current != nodes.end() ? std::min(at + 1, nodes.size() - 1) : 0;
        }
        else if (state.input.pressed(platform::Key::Up, true))
        {
            moved = current != nodes.end() && at > 0 ? at - 1 : 0;
        }
        else if (current != nodes.end() && state.input.pressed(platform::Key::Right, true) && current->hasChildren)
        {
            if (!current->expanded)
            {
                expanded[current->uuid] = true;
            }
            else if (at + 1 < nodes.size())
            {
                moved = at + 1;
            }
        }
        else if (current != nodes.end() && state.input.pressed(platform::Key::Left, true))
        {
            if (current->hasChildren && current->expanded)
            {
                expanded[current->uuid] = false;
            }
            else
            {
                for (std::size_t above = at; above-- > 0;)
                {
                    if (nodes[above].depth < current->depth)
                    {
                        moved = above;
                        break;
                    }
                }
            }
        }
        if (moved)
        {
            state.selection.set(nodes[*moved].uuid);
            state.rangeAnchor = nodes[*moved].uuid;
        }
    }

    // The menu of the row it opened on, or of the space under the rows.
    if (world.isPopupOpen(scene(), menu))
    {
        const Entity target = world.contextTarget();
        const TreeNode* const node = nodeOfRow(target);
        const bool onRoots = node == nullptr;
        if (menuOnRoots != onRoots || (node != nullptr && menuTarget != node->uuid) || (onRoots && menuTarget))
        {
            menuOnRoots = onRoots;
            menuTarget = node != nullptr ? std::optional(node->uuid) : std::nullopt;
            if (node != nullptr && !state.selection.contains(node->uuid))
            {
                state.selection.set(node->uuid);
                state.rangeAnchor = node->uuid;
            }
            openMenu(state, edited);
        }
        fitMenu(menu, font * 16.0f);
    }
    answerMenu(state, edited);
    if (!world.isPopupOpen(scene(), menu))
    {
        menuTarget.reset();
        menuOnRoots = false;
    }

    // What was dropped on a row or under them.
    if (const ui::Drop* const dropped = world.dropped())
    {
        drop(state, edited, *dropped);
    }
    // A click under the rows chooses nothing.
    if (panel.input().pointerReleased && !world.carried() && world.hovered() == Entity{} && panel.hovered() &&
        !world.isPopupOpen(scene(), menu) && world.dropped() == nullptr && !state.input.ctrl() && !state.input.shift())
    {
        const ui::LaidOutRect* const area = world.canvases().empty() ? nullptr : world.canvases().front().layout.find(list);
        const math::Vec2 point = panel.input().pointer;
        if (area != nullptr && point.x >= area->min.x && point.x <= area->max.x && point.y >= area->min.y && point.y <= area->max.y)
        {
            state.selection.clear();
        }
    }
}

void drawHierarchyPanel(ToolsState& state, scene::Scene& scene)
{
    DEVEX_PROFILE_SCOPE("Scene tree");
    state.hierarchyFocused = false;
    if (!beginDockedPanel(state, hierarchyWindow))
    {
        return;
    }
    if (!state.uiKit)
    {
        state.uiKit = std::make_shared<EditorUiKit>(state.renderer, state.icons,
                                                    state.platform.baseDirectory() / "resources" / "fonts");
    }
    if (!state.sceneTreeUi)
    {
        state.sceneTreeUi = std::make_shared<SceneTreeUi>();
    }
    state.sceneTreeUi->update(state, *state.uiKit, scene, core::Duration(state.input.delta()));
    endDockedPanel(state);
}

void renderSceneTree(ToolsState& state, render::RenderWorld& world)
{
    if (state.sceneTreeUi && state.uiKit)
    {
        state.sceneTreeUi->panel.render(*state.uiKit, world, linearColor(themeColors().panel));
    }
}

} // namespace devex::tools::detail
