#include "SettingsUi.hpp"

#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>

#include <algorithm>
#include <utility>

namespace devex::tools::detail
{

void ProjectSettingsUi::buildAddons(EditorUiKit& kit)
{
    auto& install = pageCard(kit, 9, "Local Addons");
    note(&install,
         "Install reusable content and editor tools from a folder containing addon.dvxaddon.",
         "dim", 2);
    const auto row = formRow(install, "Install");
    installAddon = button(kit, row.editor, Icon::FolderOpen, "Install from Folder...", "primary",
                          font * 18, line);
    refreshAddons = toolButton(kit, row.editor, Icon::Refresh, rightButton(closeButtonSize(), 0));
    tooltip(refreshAddons.entity, "Refresh installed addons");
    addonMessage = addonResult.empty() ? scene::Entity{} : note(&install, addonResult, "dim", 2);
    note(&install,
         "Content and scripts remain available in FileSystem. Enable an addon's editor tools below "
         "to expose its commands in Editor > Addon Tools.",
         "dim", 3);
    addonRows.clear();
    if (addonPackages.empty())
        note(&install, "No addons installed in this project.", "dim", 2);
    for (const auto& addon : addonPackages)
    {
        auto& card = pageCard(kit, 9, addon.name);
        note(&card, addon.id + "   " + addon.version, "dim", 1);
        if (!addon.author.empty())
            note(&card, "By " + addon.author, "dim", 1);
        if (!addon.description.empty())
            note(&card, addon.description, "text", 3);
        if (!addon.license.empty())
            note(&card, "License: " + addon.license, "dim", 1);
        AddonRow made;
        const auto tools = formRow(card, "Editor Tools");
        made.enabled = toggle(tools.editor);
        made.folder =
            button(kit, tools.editor, Icon::FolderOpen, "Open Folder", "flat", font * 12, line);
        placeRight(scene(), made.folder.entity, 0, font * 12);
        if (!addon.error.empty())
            note(&card, addon.error, "warning", 3);
        addonRows.push_back(made);
    }
}

void ProjectSettingsUi::syncAddons()
{
    if (addonMessage.isValid())
        scene().get<scene::UiText>(addonMessage).text = addonResult;
    for (std::size_t i = 0; i < addonRows.size() && i < addonPackages.size(); ++i)
    {
        setToggle(addonRows[i].enabled, addonPackages[i].error.empty() &&
                                            asset::addonEnabled(project, addonPackages[i].id));
        scene().get<scene::UiToggle>(addonRows[i].enabled).interactable =
            addonPackages[i].error.empty();
    }
}

void ProjectSettingsUi::answerAddons(ToolsState& state)
{
    const auto& world = panel.world();
    for (std::size_t i = 0; i < addonRows.size() && i < addonPackages.size(); ++i)
    {
        const auto& addon = addonPackages[i];
        if (world.wasChanged(addonRows[i].enabled) && addon.error.empty())
        {
            const bool enabled = scene().get<scene::UiToggle>(addonRows[i].enabled).value;
            const auto found =
                std::ranges::find(project.addons, addon.id, &asset::AddonSetting::id);
            if (found == project.addons.end())
                project.addons.push_back({addon.id, enabled});
            else
                found->enabled = enabled;
        }
        if (world.wasClicked(addonRows[i].folder.entity))
            if (auto opened = state.platform.openPath(addon.directory); !opened)
                DEVEX_LOG_ERROR("Cannot open the addon folder: {}", opened.error());
    }
    if (world.wasClicked(refreshAddons.entity))
        nextAddonScan = {};
    if (world.wasClicked(installAddon.entity))
    {
        const std::weak_ptr<DialogAnswers> answers = state.dialogAnswers;
        state.platform.showFileDialog(state.window, {.type = platform::FileDialogType::OpenFolder},
                                      [answers](std::optional<std::filesystem::path> chosen)
                                      {
                                          if (const auto inbox = answers.lock(); inbox && chosen)
                                              inbox->installAddon = std::move(*chosen);
                                      });
    }
    if (auto source = std::exchange(state.dialogAnswers->installAddon, {}))
    {
        if (auto installed = asset::installAddon(state.database->project(), *source))
        {
            addonResult = "Installed " + installed->name +
                          ". Its scripts compile with the project; enable its editor tools when "
                          "the code is ready.";
            DEVEX_LOG_INFO("{}", addonResult);
            state.database->refresh();
            nextAddonScan = {};
        }
        else
        {
            addonResult = installed.error().message;
            DEVEX_LOG_ERROR("Cannot install the addon: {}", installed.error());
        }
    }
}

} // namespace devex::tools::detail
