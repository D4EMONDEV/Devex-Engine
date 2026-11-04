#include "SettingsUi.hpp"

#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>

#include <algorithm>
#include <format>
#include <utility>

namespace devex::tools::detail
{

using namespace rects;

namespace
{

constexpr const char* addonGuideUrl =
    "https://github.com/D4EMONDEV/Devex-Engine/blob/main/docs/addons.md";
constexpr const char* createAction = "create addon";
// What the window can show of what stops a package.
constexpr std::size_t shownConflicts = 4;

} // namespace

void ProjectSettingsUi::buildAddons(EditorUiKit& kit)
{
    const auto buttonRow = [&](scene::Entity parent)
    {
        const scene::Entity made = add(parent, "Buttons", whole());
        scene().add<scene::UiLayout>(made, scene::UiLayout{.kind = scene::UiLayoutKind::Row,
                                                           .spacing = font * 0.4f,
                                                           .align = scene::TextAlign::Left});
        return made;
    };

    auto& install = pageCard(kit, addonsPage, "Addons");
    note(&install,
         "An addon is a folder of reusable content and code, with a manifest that names it. Create "
         "one to work on it here and export it to share it, or install one from a folder.",
         "dim", 2);
    const auto row = formRow(install, "Add");
    const scene::Entity adding = buttonRow(row.editor);
    createAddonButton = button(kit, adding, Icon::Plus, "Create Addon...", "primary", font * 11, line);
    installAddon = button(kit, adding, Icon::FolderOpen, "Install from Folder...", "flat", font * 13, line);
    addonGuide = button(kit, adding, Icon::BookOpen, "Guide", "flat", font * 7, line);
    tooltip(addonGuide.entity, "How to make an addon and its commands, in the documentation");
    refreshAddons = toolButton(kit, row.editor, Icon::Refresh, rightButton(closeButtonSize(), 0));
    tooltip(refreshAddons.entity, "Refresh installed addons");
    addonMessage = addonResult.empty() ? scene::Entity{} : note(&install, addonResult, "dim", 2);
    addonRows.clear();
    if (addonPackages.empty())
        note(&install, "No addons installed in this project.", "dim", 2);
    for (const auto& addon : addonPackages)
    {
        auto& card = pageCard(kit, addonsPage, addon.name);
        note(&card, addon.id + "   " + addon.version, "dim", 1);
        if (!addon.author.empty())
            note(&card, "By " + addon.author, "dim", 1);
        if (!addon.description.empty())
            note(&card, addon.description, "text", 3);
        if (!addon.license.empty())
            note(&card, "License: " + addon.license, "dim", 1);
        AddonRow made;
        std::string commands;
        for (const auto& command : addonCommands)
            if (command.addon == addon.id)
                commands += (commands.empty() ? "" : ", ") + command.name;
        if (!addon.error.empty())
            note(&card, addon.error, "warning", 3);
        else if (commands.empty())
            note(&card,
                 std::format("No editor commands yet. Once its code compiles, a public static method "
                             "marked [EditorCommand(\"{}\", \"Name\")] adds one.",
                             addon.id),
                 "dim", 2);
        else
        {
            const auto tools = formRow(card, "Editor Tools");
            made.enabled = toggle(tools.editor);
            tooltip(tools.row, "Shows its commands in Editor > Addon Tools");
            note(&card, "Commands: " + commands, "dim", 2);
        }
        const scene::Entity actions = buttonRow(formRow(card, "Package").editor);
        made.folder = button(kit, actions, Icon::FolderOpen, "Open Folder", "flat", font * 9, line);
        made.exported = button(kit, actions, Icon::Package, "Export to Folder...", "flat", font * 11.5f, line);
        tooltip(made.exported.entity, "Writes a package, which Install from Folder takes in another project");
        made.update = button(kit, actions, Icon::Refresh, "Update from Folder...", "flat", font * 12.5f, line);
        tooltip(made.update.entity, "Replaces its files by those of a newer package");
        made.remove = button(kit, actions, Icon::Trash, "Uninstall...", "flat", font * 8.5f, line);
        addonRows.push_back(made);
    }

    addonDialog = dialog("Addon dialog", {font * 38.0f, font * 14.0f});
    pageEntities.push_back(addonDialog);
    addonDialogTitle = text(addonDialog, wide(font * 1.6f), "", "text", true, scene::TextAlign::Left, font * 1.15f);
    addonDialogText = text(addonDialog, wide(font * 4.4f), "", "text");
    scene().get<scene::UiText>(addonDialogText).wrap = true;
    addonDialogWarning = text(addonDialog, wide(font * 4.4f), "", "warning");
    scene().get<scene::UiText>(addonDialogWarning).wrap = true;
    std::tie(addonConfirm, addonCancel) =
        dialogButtons(kit, addonDialog, Icon::Check, "Install", font * 8.0f);

    addonCreateDialog = dialog("Create addon dialog", {font * 38.0f, font * 25.0f});
    pageEntities.push_back(addonCreateDialog);
    text(addonCreateDialog, wide(font * 1.6f), "Create Addon", "text", true, scene::TextAlign::Left, font * 1.15f);
    const auto fieldRow = [&](const char* label, std::string value, std::string placeholder)
    {
        const scene::Entity line_ = add(addonCreateDialog, "Field", wide(line));
        scene().add<scene::UiLayout>(line_, scene::UiLayout{.kind = scene::UiLayoutKind::Row,
                                                            .spacing = font * 0.5f,
                                                            .align = scene::TextAlign::Left});
        text(line_, middle({font * 6.5f, line}), label, "text");
        return field(line_, grow(line), std::move(value), std::move(placeholder), createAction);
    };
    draftName = fieldRow("Name", "", "My Tools");
    draftId = fieldRow("Id", "", "Taken from the name");
    draftVersion = fieldRow("Version", "0.1.0", "");
    draftAuthor = fieldRow("Author", "", "Optional");
    draftDescription = fieldRow("Description", "", "Optional");
    draftLicense = fieldRow("License", "", "Optional, such as MIT");
    const scene::Entity exampleLine = add(addonCreateDialog, "Example", wide(font * 1.6f));
    scene().add<scene::UiLayout>(exampleLine, scene::UiLayout{.kind = scene::UiLayoutKind::Row,
                                                              .spacing = font * 0.5f,
                                                              .align = scene::TextAlign::Left});
    draftExample = add(exampleLine, "Example command", middle({font * 1.2f, font * 1.2f}), "toggle");
    scene().add<scene::UiImage>(draftExample);
    scene().add<scene::UiToggle>(draftExample, scene::UiToggle{.value = true});
    text(exampleLine, grow(font * 1.6f), "Start with an example editor command, in C#", "text");
    draftStatus = text(addonCreateDialog, wide(font * 3.0f), "", "dim");
    scene().get<scene::UiText>(draftStatus).wrap = true;
    std::tie(draftConfirm, draftCancel) =
        dialogButtons(kit, addonCreateDialog, Icon::Plus, "Create", font * 8.0f);
}

void ProjectSettingsUi::syncAddons()
{
    if (addonMessage.isValid())
        scene().get<scene::UiText>(addonMessage).text = addonResult;
    for (std::size_t i = 0; i < addonRows.size() && i < addonPackages.size(); ++i)
        if (addonRows[i].enabled.isValid())
            setToggle(addonRows[i].enabled, asset::addonEnabled(project, addonPackages[i].id));
}

bool ProjectSettingsUi::addonDialogOpen()
{
    return panel.world().isPopupOpen(scene(), addonDialog) ||
           panel.world().isPopupOpen(scene(), addonCreateDialog);
}

void ProjectSettingsUi::addonFinished(ToolsState& state, std::string message)
{
    addonResult = std::move(message);
    DEVEX_LOG_INFO("{}", addonResult);
    state.database->refresh();
    nextAddonScan = {};
}

void ProjectSettingsUi::addonFailed(std::string_view what, const core::Error& error)
{
    addonResult = error.message;
    DEVEX_LOG_ERROR("{}: {}", what, error);
}

void ProjectSettingsUi::askAboutAddon(EditorUiKit& kit, AddonAction action, std::string title, std::string text,
                                      std::string warning, std::string_view confirm)
{
    addonAction = action;
    scene().get<scene::UiText>(addonDialogTitle).text = std::move(title);
    scene().get<scene::UiText>(addonDialogText).text = std::move(text);
    scene().get<scene::UiText>(addonDialogWarning).text = std::move(warning);
    scene().get<scene::UiRect>(addonConfirm.entity).visible = !confirm.empty();
    if (!confirm.empty())
        relabel(kit, addonConfirm, confirm);
    panel.world().openPopup(scene(), addonDialog);
}

void ProjectSettingsUi::askAboutPackage(ToolsState& state, EditorUiKit& kit, const std::filesystem::path& source)
{
    const auto preview = asset::previewAddon(state.database->project(), source);
    if (!preview)
    {
        addonFailed("Cannot read the addon", preview.error());
        return;
    }
    const asset::AddonInfo& addon = preview->addon;
    if (!updatingAddon.empty() && addon.id != updatingAddon)
    {
        addonFailed("Cannot update the addon",
                    core::Error{core::ErrorCode::InvalidArgument,
                                std::format("This folder holds the addon {}, not {}", addon.id, updatingAddon)});
        return;
    }
    addonSource = source;
    if (!preview->conflicts.empty())
    {
        std::string conflicts;
        for (std::size_t i = 0; i < preview->conflicts.size() && i < shownConflicts; ++i)
            conflicts += preview->conflicts[i] + "\n";
        std::string more;
        if (preview->conflicts.size() > shownConflicts)
            more = std::format("And {} more.", preview->conflicts.size() - shownConflicts);
        askAboutAddon(kit, AddonAction::None, "Cannot install " + addon.name, std::move(conflicts), std::move(more), {});
        return;
    }
    std::string about = addon.author.empty() ? std::string{} : "By " + addon.author + ". ";
    if (!addon.license.empty())
        about += "License: " + addon.license + ". ";
    about += addon.description;
    std::string warning;
    if (preview->installedVersion)
        warning = "Its installed files are replaced: changes made to them in this project are lost. ";
    if (preview->scripts > 0)
        warning += std::format("It holds {} code file{}: its code compiles with the project and runs in the editor as "
                               "soon as it is installed, even with its Editor Tools off. Install only addons you trust.",
                               preview->scripts, preview->scripts == 1 ? "" : "s");
    if (preview->installedVersion)
        askAboutAddon(kit, AddonAction::Update,
                      *preview->installedVersion == addon.version
                          ? std::format("Reinstall {} {}?", addon.name, addon.version)
                          : std::format("Update {} from {} to {}?", addon.name, *preview->installedVersion, addon.version),
                      std::move(about), std::move(warning), "Update");
    else
        askAboutAddon(kit, AddonAction::Install, std::format("Install {} {}?", addon.name, addon.version),
                      std::move(about), std::move(warning), "Install");
}

void ProjectSettingsUi::uninstallAddon(ToolsState& state, scene::Scene& edited)
{
    for (const std::string& path : asset::addonResourcePaths(state.database->project(), addonTarget))
    {
        if (auto removed = deleteFileSystemPath(state, edited, path); !removed)
        {
            addonFailed("Cannot uninstall the addon", removed.error());
            return;
        }
    }
    std::erase_if(project.addons, [&](const asset::AddonSetting& each) { return each.id == addonTarget; });
    addonFinished(state, "Uninstalled " + addonTarget + ".");
}

void ProjectSettingsUi::answerAddonDialogs(ToolsState& state, scene::Scene& edited)
{
    ui::UiWorld& world = panel.world();
    const bool escaped = state.input.pressed(platform::Key::Escape, false) && !state.typing;
    // Escape answers the window asking, not the settings behind it.
    if (addonDialogOpen())
        state.modalToClose.clear();
    if (world.isPopupOpen(scene(), addonDialog))
    {
        if (world.wasClicked(addonCancel.entity) || world.wasCancelled() || escaped)
        {
            world.closePopup(scene(), addonDialog);
        }
        else if (world.wasClicked(addonConfirm.entity))
        {
            world.closePopup(scene(), addonDialog);
            const asset::Project& saved = state.database->project();
            if (addonAction == AddonAction::Uninstall)
            {
                uninstallAddon(state, edited);
            }
            else if (addonAction == AddonAction::Install)
            {
                if (auto installed = asset::installAddon(saved, addonSource))
                    addonFinished(state, "Installed " + installed->name +
                                             ". Its scripts compile with the project; enable its editor "
                                             "tools when the code is ready.");
                else
                    addonFailed("Cannot install the addon", installed.error());
            }
            else if (addonAction == AddonAction::Update)
            {
                if (auto updated = asset::updateAddon(saved, addonSource))
                    addonFinished(state, "Updated " + updated->name + " to " + updated->version + ".");
                else
                    addonFailed("Cannot update the addon", updated.error());
            }
        }
    }
    if (world.isPopupOpen(scene(), addonCreateDialog))
    {
        const auto typed = [&](scene::Entity field) { return scene().get<scene::UiText>(field).text; };
        const asset::AddonDraft draft{.id = typed(draftId),
                                      .name = typed(draftName),
                                      .version = typed(draftVersion),
                                      .author = typed(draftAuthor),
                                      .description = typed(draftDescription),
                                      .license = typed(draftLicense),
                                      .exampleCommand = scene().get<scene::UiToggle>(draftExample).value};
        const asset::Project& saved = state.database->project();
        const auto problem = asset::addonDraftProblem(saved, draft);
        const std::string id = draft.id.empty() ? asset::addonIdFromName(draft.name) : draft.id;
        scene().get<scene::UiText>(draftStatus).text =
            problem ? *problem
                    : std::format("Makes res://assets/addons/{0}{1}.", id,
                                  draft.exampleCommand ? std::format(" and res://code/addons/{}", id) : "");
        scene().get<scene::UiRect>(draftStatus).style = problem ? "warning" : "dim";
        if (world.wasClicked(draftCancel.entity) || world.wasCancelled() || escaped)
        {
            world.closePopup(scene(), addonCreateDialog);
        }
        else if ((world.wasClicked(draftConfirm.entity) || world.wasSubmitted(createAction)) && !problem)
        {
            world.closePopup(scene(), addonCreateDialog);
            if (auto made = asset::createAddon(saved, draft))
            {
                // It is being worked on: its commands show as soon as they compile.
                project.addons.push_back({made->id, true});
                addonFinished(state, "Created " + made->name + ". Its files are in FileSystem, under addons.");
                if (draft.exampleCommand)
                {
                    openTextFile(state, saved.codeDirectory() / "addons" / core::pathFromUtf8(made->id) /
                                            asset::addonExampleScript);
                    state.showProjectSettings = false;
                }
            }
            else
            {
                addonFailed("Cannot create the addon", made.error());
            }
        }
    }
}

void ProjectSettingsUi::answerAddons(ToolsState& state, EditorUiKit& kit, scene::Scene& edited)
{
    ui::UiWorld& world = panel.world();
    for (std::size_t i = 0; i < addonRows.size() && i < addonPackages.size(); ++i)
    {
        const auto& addon = addonPackages[i];
        if (addonRows[i].enabled.isValid() && world.wasChanged(addonRows[i].enabled))
        {
            const bool enabled = scene().get<scene::UiToggle>(addonRows[i].enabled).value;
            const auto found = std::ranges::find(project.addons, addon.id, &asset::AddonSetting::id);
            if (found == project.addons.end())
                project.addons.push_back({addon.id, enabled});
            else
                found->enabled = enabled;
        }
        if (world.wasClicked(addonRows[i].folder.entity))
            if (auto opened = state.platform.openPath(addon.directory); !opened)
                DEVEX_LOG_ERROR("Cannot open the addon folder: {}", opened.error());
        if (world.wasClicked(addonRows[i].exported.entity))
        {
            addonTarget = addon.id;
            showFolderDialog(state, &DialogAnswers::exportAddon);
        }
        if (world.wasClicked(addonRows[i].update.entity))
        {
            updatingAddon = addon.id;
            showFolderDialog(state, &DialogAnswers::installAddon);
        }
        if (world.wasClicked(addonRows[i].remove.entity))
        {
            addonTarget = addon.id;
            std::string folders;
            for (const std::string& path : asset::addonResourcePaths(state.database->project(), addon.id))
                folders += path + "\n";
            askAboutAddon(kit, AddonAction::Uninstall, "Uninstall " + addon.name + "?", std::move(folders),
                          "These folders are deleted permanently, and tabs showing their files close, with "
                          "their unsaved changes. Scenes using its assets or components keep them, unresolved.",
                          "Uninstall");
        }
    }
    if (world.wasClicked(refreshAddons.entity))
        nextAddonScan = {};
    if (world.wasClicked(addonGuide.entity))
        if (auto opened = state.platform.openUrl(addonGuideUrl); !opened)
            DEVEX_LOG_ERROR("Cannot open the addon guide: {}", opened.error());
    if (world.wasClicked(installAddon.entity))
    {
        updatingAddon.clear();
        showFolderDialog(state, &DialogAnswers::installAddon);
    }
    if (world.wasClicked(createAddonButton.entity))
    {
        for (const scene::Entity field : {draftName, draftId, draftAuthor, draftDescription, draftLicense})
            scene().get<scene::UiText>(field).text.clear();
        scene().get<scene::UiText>(draftVersion).text = "0.1.0";
        world.openPopup(scene(), addonCreateDialog);
    }
    if (auto source = std::exchange(state.dialogAnswers->installAddon, {}))
        askAboutPackage(state, kit, *source);
    if (auto parent = std::exchange(state.dialogAnswers->exportAddon, {}))
    {
        if (auto exported = asset::exportAddon(state.database->project(), addonTarget, *parent))
            addonFinished(state, "Exported " + addonTarget + " to " + core::toUtf8(*exported) + ".");
        else
            addonFailed("Cannot export the addon", exported.error());
    }
    answerAddonDialogs(state, edited);
}

} // namespace devex::tools::detail
