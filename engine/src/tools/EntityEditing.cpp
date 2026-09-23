#include "ToolsState.hpp"

#include <devex/core/Log.hpp>
#include <devex/scene/EntityCopy.hpp>
#include <devex/scene/Prefab.hpp>
#include <devex/tools/SceneCommands.hpp>

#include <imgui_internal.h>

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

void drawEntityEditMenuItems(ToolsState& state, scene::Scene& scene)
{
    const bool hasSelection = scene.findEntity(state.selection.active()).isValid();
    const bool deletable = canDeleteSelection(state, scene);
    if (ImGui::MenuItemEx("Cut", icons::Scissors.c_str(), "Ctrl+X", false, deletable))
    {
        cutSelection(state, scene);
    }
    if (ImGui::MenuItemEx("Copy", icons::Copy.c_str(), "Ctrl+C", false, hasSelection))
    {
        copySelection(state, scene);
    }
    if (ImGui::MenuItemEx("Paste", icons::ClipboardPaste.c_str(), "Ctrl+V", false,
                          scene::isEntityCopy(state.platform.clipboardText())))
    {
        pasteEntities(state, scene);
    }
    if (ImGui::MenuItemEx("Duplicate", icons::CopyPlus.c_str(), "Ctrl+D", false, hasSelection))
    {
        duplicateSelection(state, scene);
    }
    if (ImGui::MenuItemEx("Rename", icons::Pencil.c_str(), "F2", false, hasSelection))
    {
        startRename(state, state.selection.active());
    }
    if (ImGui::MenuItemEx("Select All", nullptr, "Ctrl+A"))
    {
        selectAll(state, scene);
    }
    if (state.mode == ToolsMode::Editor)
    {
        const bool allHidden = hasSelection && std::ranges::all_of(state.selection.entities(), [&state](core::Uuid entity) {
            return state.hiddenEntities.contains(entity);
        });
        if (ImGui::MenuItemEx(allHidden ? "Show in Viewport" : "Hide in Viewport",
                              (allHidden ? icons::Eye : icons::EyeOff).c_str(), "H", false, hasSelection))
        {
            toggleSelectionHidden(state, scene);
        }
        if (ImGui::MenuItemEx("Show All in Viewport", nullptr, nullptr, false, !state.hiddenEntities.empty()))
        {
            state.hiddenEntities.clear();
            saveEditorSettings(state);
        }
    }
    ImGui::Separator();
    if (ImGui::MenuItemEx("Delete", icons::Trash.c_str(), "Delete", false, deletable))
    {
        deleteSelection(state, scene);
    }
}

void handleEntityShortcuts(ToolsState& state, scene::Scene& scene)
{
    // While the game runs, the viewport gives it the keyboard.
    const bool viewport = state.viewportFocused && state.playState == PlayState::Editing;
    const ImGuiIO& io = ImGui::GetIO();
    if ((!state.hierarchyFocused && !viewport) || io.WantTextInput || state.flying || !state.renamedEntity.isNil())
    {
        return;
    }
    const auto pressed = [](ImGuiKeyChord chord) { return ImGui::IsKeyChordPressed(chord); };
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_C))
    {
        copySelection(state, scene);
    }
    else if (pressed(ImGuiMod_Ctrl | ImGuiKey_X))
    {
        cutSelection(state, scene);
    }
    else if (pressed(ImGuiMod_Ctrl | ImGuiKey_V))
    {
        pasteEntities(state, scene);
    }
    else if (pressed(ImGuiMod_Ctrl | ImGuiKey_D))
    {
        duplicateSelection(state, scene);
    }
    else if (pressed(ImGuiMod_Ctrl | ImGuiKey_A))
    {
        selectAll(state, scene);
    }
    else if (pressed(ImGuiKey_F2))
    {
        startRename(state, state.selection.active());
    }
    else if (pressed(ImGuiKey_H))
    {
        toggleSelectionHidden(state, scene);
    }
    else if (pressed(ImGuiKey_Delete))
    {
        deleteSelection(state, scene);
    }
}

} // namespace devex::tools::detail
