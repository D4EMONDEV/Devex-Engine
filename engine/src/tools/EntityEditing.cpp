#include "EditorFrame.hpp"
#include "ToolsState.hpp"

#include <devex/core/Log.hpp>
#include <devex/scene/EntityCopy.hpp>
#include <devex/scene/Prefab.hpp>
#include <devex/tools/SceneCommands.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <utility>

namespace devex::tools::detail {
namespace {

// One command for several, or none.
[[nodiscard]] std::unique_ptr<Command> combine(std::vector<std::unique_ptr<Command>> commands, std::string description)
{
    if (commands.empty())
    {
        return nullptr;
    }
    if (commands.size() == 1)
    {
        return std::move(commands.front());
    }
    return makeCompositeCommand(std::move(commands), std::move(description));
}

[[nodiscard]] std::string countedDescription(std::string_view action, std::size_t count)
{
    return count == 1 ? std::format("{} entity", action) : std::format("{} {} entities", action, count);
}

// Creates the copy under parent, renamed when a sibling or an earlier copy has its name.
void addCopy(const scene::Scene& scene, scene::EntityTreeCopy copy, scene::Entity parent, core::Uuid before,
             std::string_view action, std::vector<std::string>& taken,
             std::vector<std::unique_ptr<Command>>& commands)
{
    const std::string name = uniqueChildName(scene, parent, copy.name, taken);
    commands.push_back(makeCreateEntityTreeCommand(std::move(copy.text), copy.root,
                                                   parent.isValid() ? scene.uuid(parent) : core::Uuid{},
                                                   std::string(action), before));
    if (name != copy.name)
    {
        commands.push_back(makeRenameCommand(copy.root, copy.name, name));
    }
    taken.push_back(name);
}

void collectAll(const scene::Scene& scene, scene::Entity entity, std::vector<core::Uuid>& entities)
{
    for (; entity.isValid(); entity = scene.nextSibling(entity))
    {
        entities.push_back(scene.uuid(entity));
        collectAll(scene, scene.firstChild(entity), entities);
    }
}

} // namespace

std::string uniqueChildName(const scene::Scene& scene, scene::Entity parent, std::string_view name,
                            const std::vector<std::string>& taken)
{
    const auto used = [&](std::string_view candidate) {
        if (std::ranges::find(taken, candidate) != taken.end())
        {
            return true;
        }
        for (scene::Entity child = parent.isValid() ? scene.firstChild(parent) : scene.firstRoot(); child.isValid();
             child = scene.nextSibling(child))
        {
            if (scene.name(child) == candidate)
            {
                return true;
            }
        }
        return false;
    };
    if (!used(name))
    {
        return std::string(name);
    }
    // A number at the end counts on; otherwise the copies start at 2.
    std::size_t digits = name.size();
    while (digits > 0 && std::isdigit(static_cast<unsigned char>(name[digits - 1])) != 0)
    {
        --digits;
    }
    std::string stem(name.substr(0, digits));
    std::uint64_t number = 1;
    if (digits < name.size() && name.size() - digits <= 9)
    {
        number = std::stoull(std::string(name.substr(digits)));
    }
    else
    {
        stem = std::string(name) + " ";
    }
    for (;;)
    {
        std::string candidate = stem + std::to_string(++number);
        if (!used(candidate))
        {
            return candidate;
        }
    }
}

void copySelection(ToolsState& state, const scene::Scene& scene)
{
    const std::vector<scene::Entity> roots = selectedRoots(scene, state.selection);
    if (!roots.empty())
    {
        state.platform.setClipboardText(scene::saveEntityTrees(scene, roots));
    }
}

bool canDeleteSelection(const ToolsState& state, const scene::Scene& scene)
{
    return std::ranges::any_of(selectedRoots(scene, state.selection), [&scene](scene::Entity root) {
        return !scene::isInsidePrefabInstance(scene, root);
    });
}

void deleteSelection(ToolsState& state, scene::Scene& scene)
{
    std::vector<std::unique_ptr<Command>> commands;
    for (const scene::Entity root : selectedRoots(scene, state.selection))
    {
        // The entities of a prefab instance go with it.
        if (!scene::isInsidePrefabInstance(scene, root))
        {
            commands.push_back(makeDestroyEntityCommand(scene.uuid(root)));
        }
    }
    const std::size_t count = commands.size();
    if (std::unique_ptr<Command> command = combine(std::move(commands), countedDescription("Delete", count)))
    {
        state.pendingCommand = std::move(command);
        state.selection.clear();
    }
}

void cutSelection(ToolsState& state, scene::Scene& scene)
{
    if (canDeleteSelection(state, scene))
    {
        copySelection(state, scene);
        deleteSelection(state, scene);
    }
}

void pasteEntities(ToolsState& state, scene::Scene& scene)
{
    const std::string text = state.platform.clipboardText();
    if (!scene::isEntityCopy(text))
    {
        return;
    }
    core::Result<std::vector<scene::EntityTreeCopy>> copies = scene::copyEntityTrees(text);
    if (!copies)
    {
        DEVEX_LOG_WARNING("Cannot paste the entities: {}", copies.error());
        return;
    }
    // Beside the active entity, as its siblings.
    const scene::Entity active = scene.findEntity(state.selection.active());
    const scene::Entity parent = active.isValid() ? scene.parent(active) : scene::Entity{};
    std::vector<std::unique_ptr<Command>> commands;
    std::vector<std::string> taken;
    std::vector<core::Uuid> pasted;
    for (scene::EntityTreeCopy& copy : *copies)
    {
        pasted.push_back(copy.root);
        addCopy(scene, std::move(copy), parent, core::Uuid{}, "Paste", taken, commands);
    }
    state.pendingCommand = makeCompositeCommand(std::move(commands), countedDescription("Paste", pasted.size()));
    state.selection.set(pasted);
}

void duplicateSelection(ToolsState& state, scene::Scene& scene)
{
    std::vector<std::unique_ptr<Command>> commands;
    std::vector<std::pair<scene::Entity, std::vector<std::string>>> takenByParent;
    std::vector<core::Uuid> duplicates;
    for (const scene::Entity root : selectedRoots(scene, state.selection))
    {
        const std::array roots{root};
        core::Result<std::vector<scene::EntityTreeCopy>> copies =
            scene::copyEntityTrees(scene::saveEntityTrees(scene, roots));
        if (!copies || copies->size() != 1)
        {
            DEVEX_LOG_WARNING("Cannot duplicate {}: {}", scene.name(root),
                              copies ? std::string("it copies as several entities") : copies.error().message);
            continue;
        }
        const scene::Entity parent = scene.parent(root);
        auto taken = std::ranges::find(takenByParent, parent, &std::pair<scene::Entity, std::vector<std::string>>::first);
        if (taken == takenByParent.end())
        {
            takenByParent.emplace_back(parent, std::vector<std::string>{});
            taken = takenByParent.end() - 1;
        }
        // Right after the original.
        const scene::Entity next = scene.nextSibling(root);
        duplicates.push_back(copies->front().root);
        addCopy(scene, std::move(copies->front()), parent, next.isValid() ? scene.uuid(next) : core::Uuid{},
                "Duplicate", taken->second, commands);
    }
    if (duplicates.empty())
    {
        return;
    }
    state.pendingCommand = makeCompositeCommand(std::move(commands), countedDescription("Duplicate", duplicates.size()));
    state.selection.set(duplicates);
}

void selectAll(ToolsState& state, const scene::Scene& scene)
{
    std::vector<core::Uuid> entities;
    collectAll(scene, scene.firstRoot(), entities);
    state.selection.set(entities);
}

void startRename(ToolsState& state, core::Uuid entity)
{
    if (entity.isNil())
    {
        return;
    }
    state.renamedEntity = entity;
    state.focusRename = true;
    state.showHierarchy = true;
}

bool isHidden(const ToolsState& state, const scene::Scene& scene, scene::Entity entity)
{
    if (state.hiddenEntities.empty())
    {
        return false;
    }
    for (; entity.isValid(); entity = scene.parent(entity))
    {
        if (state.hiddenEntities.contains(scene.uuid(entity)))
        {
            return true;
        }
    }
    return false;
}

void toggleSelectionHidden(ToolsState& state, const scene::Scene& scene)
{
    std::vector<core::Uuid> selected;
    for (const core::Uuid entity : state.selection.entities())
    {
        if (scene.findEntity(entity).isValid())
        {
            selected.push_back(entity);
        }
    }
    const bool allHidden = std::ranges::all_of(selected, [&state](core::Uuid entity) {
        return state.hiddenEntities.contains(entity);
    });
    for (const core::Uuid entity : selected)
    {
        if (allHidden)
        {
            state.hiddenEntities.erase(entity);
        }
        else
        {
            state.hiddenEntities.insert(entity);
        }
    }
    // Kept with the project's editor settings right away.
    saveEditorSettings(state);
}

void addEntityEditEntries(ToolsState& state, scene::Scene& scene, std::vector<MenuEntry>& entries)
{
    const bool hasSelection = scene.findEntity(state.selection.active()).isValid();
    const bool deletable = canDeleteSelection(state, scene);
    entries.push_back({.icon = Icon::Scissors, .label = "Cut", .shortcut = "Ctrl+X", .enabled = deletable,
                       .action = [](ToolsState& tools, scene::Scene& edited) { cutSelection(tools, edited); }});
    entries.push_back({.icon = Icon::Copy, .label = "Copy", .shortcut = "Ctrl+C", .enabled = hasSelection,
                       .action = [](ToolsState& tools, scene::Scene& edited) { copySelection(tools, edited); }});
    entries.push_back({.icon = Icon::ClipboardPaste, .label = "Paste", .shortcut = "Ctrl+V",
                       .enabled = scene::isEntityCopy(state.platform.clipboardText()),
                       .action = [](ToolsState& tools, scene::Scene& edited) { pasteEntities(tools, edited); }});
    entries.push_back({.icon = Icon::CopyPlus, .label = "Duplicate", .shortcut = "Ctrl+D", .enabled = hasSelection,
                       .action = [](ToolsState& tools, scene::Scene& edited) { duplicateSelection(tools, edited); }});
    entries.push_back({.icon = Icon::Pencil, .label = "Rename", .shortcut = "F2", .enabled = hasSelection,
                       .action = [](ToolsState& tools, scene::Scene&) { startRename(tools, tools.selection.active()); }});
    entries.push_back({.label = "Select All", .shortcut = "Ctrl+A",
                       .action = [](ToolsState& tools, scene::Scene& edited) { selectAll(tools, edited); }});
    if (state.mode == ToolsMode::Editor)
    {
        const bool allHidden = hasSelection && std::ranges::all_of(state.selection.entities(), [&state](core::Uuid entity) {
            return state.hiddenEntities.contains(entity);
        });
        entries.push_back({.icon = allHidden ? Icon::Eye : Icon::EyeOff, .label = allHidden ? "Show in Viewport" : "Hide in Viewport",
                           .shortcut = "H", .enabled = hasSelection,
                           .action = [](ToolsState& tools, scene::Scene& edited) { toggleSelectionHidden(tools, edited); }});
        entries.push_back({.label = "Show All in Viewport", .enabled = !state.hiddenEntities.empty(),
                           .action = [](ToolsState& tools, scene::Scene&) {
                               tools.hiddenEntities.clear();
                               saveEditorSettings(tools);
                           }});
    }
    entries.push_back(MenuEntry::line());
    entries.push_back({.icon = Icon::Trash, .label = "Delete", .shortcut = "Delete", .enabled = deletable,
                       .action = [](ToolsState& tools, scene::Scene& edited) { deleteSelection(tools, edited); }});
}

void handleEntityShortcuts(ToolsState& state, scene::Scene& scene)
{
    // While the game runs, the viewport gives it the keyboard.
    const bool viewport = state.viewportFocused && state.playState == PlayState::Editing;
    if ((!state.hierarchyFocused && !viewport) || state.typing || state.flying || !state.renamedEntity.isNil())
    {
        return;
    }
    const auto pressed = [&state](KeyModifiers modifiers, auto key) { return state.input.chord(modifiers, key); };
    if (pressed(KeyModifiers{.ctrl = true}, 'c'))
    {
        copySelection(state, scene);
    }
    else if (pressed(KeyModifiers{.ctrl = true}, 'x'))
    {
        cutSelection(state, scene);
    }
    else if (pressed(KeyModifiers{.ctrl = true}, 'v'))
    {
        pasteEntities(state, scene);
    }
    else if (pressed(KeyModifiers{.ctrl = true}, 'd'))
    {
        duplicateSelection(state, scene);
    }
    else if (pressed(KeyModifiers{.ctrl = true}, 'a'))
    {
        selectAll(state, scene);
    }
    else if (pressed(KeyModifiers{}, platform::Key::F2))
    {
        startRename(state, state.selection.active());
    }
    else if (pressed(KeyModifiers{}, 'h'))
    {
        toggleSelectionHidden(state, scene);
    }
    else if (pressed(KeyModifiers{}, platform::Key::Delete))
    {
        deleteSelection(state, scene);
    }
}

} // namespace devex::tools::detail
