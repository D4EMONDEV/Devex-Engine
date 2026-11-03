#include "SettingsUi.hpp"

#include <algorithm>
#include <format>

namespace devex::tools::detail
{

using scene::Entity;
using scene::UiRect;

void ProjectSettingsUi::buildGlobals(EditorUiKit& kit)
{
    globalCard = sections.size();
    auto& card = pageCard(kit, 8, "Autoloads");
    note(&card,
         "Scenes loaded before the level. Their entities and scripts stay alive when the level "
         "changes.",
         "dim", 2);
    note(
        &card,
        "Attach your game manager, music or shared interface scripts to a scene, then add it here.",
        "dim", 2);
    const float tool = closeButtonSize();
    const float nameWidth = font * 10;
    const float tools = tool * 4 + gap * 4;
    const auto columns = formRow(card, "Order");
    text(columns.editor,
         UiRect{.anchorMin = {0, 0},
                .anchorMax = {0, 1},
                .offsetMin = {0, 0},
                .offsetMax = {nameWidth, 0}},
         "Name", "dim", true);
    text(columns.editor,
         UiRect{.anchorMin = {0, 0},
                .anchorMax = {1, 1},
                .offsetMin = {nameWidth + gap, 0},
                .offsetMax = {-tools, 0}},
         "Scene", "dim", true);
    text(columns.editor, rightButton(tool, tool * 3 + gap * 3), "On", "dim", true,
         scene::TextAlign::Center);
    globalRows.clear();
    for (std::size_t i = 0; i < project.globals.size(); ++i)
    {
        const auto row = formRow(card, std::format("{}", i + 1));
        GlobalRow made;
        made.name = field(row.editor,
                          UiRect{.anchorMin = {0, 0},
                                 .anchorMax = {0, 1},
                                 .offsetMin = {0, 0},
                                 .offsetMax = {nameWidth, 0}},
                          "", "GlobalName");
        made.path = choice(row.editor);
        auto& path = scene().get<UiRect>(made.path);
        path.offsetMin.x = nameWidth + gap;
        path.offsetMax.x = -tools;
        made.enabled =
            add(row.editor, "Enabled", rightButton(tool - 6, tool * 3 + gap * 3), "toggle");
        scene().add<scene::UiImage>(made.enabled);
        scene().add<scene::UiToggle>(made.enabled);
        scene().add<scene::UiButton>(made.enabled);
        tooltip(made.enabled, "Load this global when the game starts");
        made.up =
            toolButton(kit, row.editor, Icon::ChevronUp, rightButton(tool, tool * 2 + gap * 2));
        made.down = toolButton(kit, row.editor, Icon::ChevronDown, rightButton(tool, tool + gap));
        made.remove = toolButton(kit, row.editor, Icon::Trash, rightButton(tool, 0));
        tooltip(made.up.entity, "Load earlier");
        tooltip(made.down.entity, "Load later");
        tooltip(made.remove.entity, "Remove this autoload from the project");
        enable(made.up, i > 0);
        enable(made.down, i + 1 < project.globals.size());
        globalRows.push_back(made);
    }
    const auto row = formRow(card, "Add Global");
    globalNewName = field(row.editor,
                          UiRect{.anchorMin = {0, 0},
                                 .anchorMax = {0, 1},
                                 .offsetMin = {0, 0},
                                 .offsetMax = {nameWidth, 0}},
                          "", "GlobalName");
    globalNewScene = choice(row.editor);
    auto& path = scene().get<UiRect>(globalNewScene);
    path.offsetMin.x = nameWidth + gap;
    path.offsetMax.x = -font * 7 - gap;
    addGlobal = button(kit, row.editor, Icon::Plus, "Add", "primary", font * 7, line - 4);
    placeRight(scene(), addGlobal.entity, 0, font * 7);
    tooltip(globalNewName,
            "A unique name using letters, digits and underscores, such as GameState");
    globalWarning = note(&card, "", "warning", 2);
    note(&card, "Load order is top to bottom. Changes apply the next time you play.", "dim", 2);
}

void ProjectSettingsUi::syncGlobals()
{
    globalPaths.assign(startupScenes.begin() + (startupScenes.empty() ? 0 : 1),
                       startupScenes.end());
    for (std::size_t i = 0; i < globalRows.size() && i < project.globals.size(); ++i)
    {
        const auto& global = project.globals[i];
        const auto& row = globalRows[i];
        setText(row.name, global.name);
        const auto path = std::ranges::find(globalPaths, global.path);
        setChoice(
            row.path, globalPaths,
            path == globalPaths.end() ? -1 : static_cast<std::int32_t>(path - globalPaths.begin()));
        scene().get<scene::UiDropdown>(row.path).placeholder = global.path + " (missing)";
        setToggle(row.enabled, global.enabled);
    }
    const auto candidate = std::ranges::find(globalPaths, globalCandidate);
    setChoice(globalNewScene, globalPaths,
              candidate == globalPaths.end()
                  ? -1
                  : static_cast<std::int32_t>(candidate - globalPaths.begin()));
    scene().get<scene::UiDropdown>(globalNewScene).placeholder = "Choose a scene...";
    auto proposal = project.globals;
    proposal.push_back(
        {.name = scene().get<scene::UiText>(globalNewName).text, .path = globalCandidate});
    enable(addGlobal, asset::validateGlobals(proposal).has_value());
    const auto valid = asset::validateGlobals(project.globals);
    scene().get<scene::UiText>(globalWarning).text = valid ? std::string{} : valid.error().message;
    showLine(sections[globalCard], globalWarning, !valid);
}

void ProjectSettingsUi::answerGlobals()
{
    const auto& world = panel.world();
    for (std::size_t i = 0; i < globalRows.size() && i < project.globals.size(); ++i)
    {
        const auto& row = globalRows[i];
        auto& global = project.globals[i];
        if (endedField == row.name)
            global.name = scene().get<scene::UiText>(row.name).text;
        if (world.wasChanged(row.path))
        {
            const auto path = scene().get<scene::UiDropdown>(row.path).selected;
            if (path >= 0 && static_cast<std::size_t>(path) < globalPaths.size())
                global.path = globalPaths[static_cast<std::size_t>(path)];
        }
        if (world.wasChanged(row.enabled))
            global.enabled = scene().get<scene::UiToggle>(row.enabled).value;
        if (world.wasClicked(row.up.entity) && i > 0)
        {
            std::swap(project.globals[i], project.globals[i - 1]);
            filterDirty = true;
            break;
        }
        if (world.wasClicked(row.down.entity) && i + 1 < project.globals.size())
        {
            std::swap(project.globals[i], project.globals[i + 1]);
            filterDirty = true;
            break;
        }
        if (world.wasClicked(row.remove.entity))
        {
            project.globals.erase(project.globals.begin() + static_cast<std::ptrdiff_t>(i));
            return;
        }
    }
    if (world.wasChanged(globalNewScene))
    {
        const auto path = scene().get<scene::UiDropdown>(globalNewScene).selected;
        if (path >= 0 && static_cast<std::size_t>(path) < globalPaths.size())
            globalCandidate = globalPaths[static_cast<std::size_t>(path)];
    }
    if (world.wasClicked(addGlobal.entity))
    {
        auto proposal = project.globals;
        proposal.push_back(
            {.name = scene().get<scene::UiText>(globalNewName).text, .path = globalCandidate});
        if (asset::validateGlobals(proposal))
            project.globals = std::move(proposal);
    }
}

} // namespace devex::tools::detail
