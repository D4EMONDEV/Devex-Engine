// The Export window, made with the interface of the engine: the game and where it goes, the scenes
// exported with the startup scene, the folders always included, and the export with its progress. Its
// settings are part of the project, written once an edit ends and always before an export starts.
#include "SettingsUi.hpp"
#include "EditorModal.hpp"

#include <devex/core/Profiler.hpp>
#include <devex/asset/Project.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>
#include <devex/platform/Process.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace devex::tools::detail {

using scene::Entity;
using scene::UiRect;
using namespace rects;
using Button = PanelButton;

namespace {

inline constexpr const char* exportWindow = "Export Game";

// The name of the executable, as the export makes it from the name of the game.
[[nodiscard]] std::string executableFileName(std::string_view gameName)
{
    std::string name;
    for (const char character : gameName)
    {
        const bool forbidden = static_cast<unsigned char>(character) < 0x20 || std::string_view("<>:\"/\\|?*").find(character) != std::string_view::npos;
        name.push_back(forbidden ? '_' : character);
    }
    while (!name.empty() && (name.back() == ' ' || name.back() == '.'))
    {
        name.pop_back();
    }
    while (!name.empty() && (name.front() == ' ' || name.front() == '.'))
    {
        name.erase(name.begin());
    }
    return (name.empty() ? std::string("Game") : name) + ".exe";
}

void showOutputFolderDialog(ToolsState& state)
{
    std::weak_ptr<DialogAnswers> answers = state.dialogAnswers;
    state.platform.showFileDialog(state.window,
                                  {.type = platform::FileDialogType::OpenFolder, .defaultLocation = state.database->project().root},
                                  [answers](std::optional<std::filesystem::path> chosen) {
                                      if (const std::shared_ptr<DialogAnswers> inbox = answers.lock(); inbox && chosen)
                                      {
                                          inbox->exportFolder = std::move(chosen);
                                      }
                                  });
}

// Whether the project has C# code, which does not run on Android yet.
[[nodiscard]] bool hasCSharp(const asset::Project& project)
{
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator entry(project.codeDirectory(), error), end; !error && entry != end; entry.increment(error))
    {
        if (entry->path().extension() == ".cs")
        {
            return true;
        }
    }
    return false;
}

// The folders of the project that hold assets, as res:// paths.
[[nodiscard]] std::vector<std::string> assetFolders(const asset::AssetDatabase& database)
{
    std::set<std::string> folders;
    for (const asset::SourceFile& source : database.sources())
    {
        std::string_view path = source.path;
        for (std::size_t slash = path.rfind('/'); slash != std::string_view::npos && path.substr(0, slash).size() > asset::resourceScheme.size();
             slash = path.rfind('/'))
        {
            path = path.substr(0, slash);
            folders.emplace(path);
        }
    }
    return {folders.begin(), folders.end()};
}

// The scenes of the project, as res:// paths.
[[nodiscard]] std::vector<std::string> scenePaths(const asset::AssetDatabase& database)
{
    std::vector<std::string> paths;
    for (const asset::AssetInfo& info : database.assets(asset::AssetType::Scene))
    {
        if (const std::optional<asset::SourceFile> source = database.sourceOf(info.id))
        {
            paths.push_back(source->path);
        }
    }
    return paths;
}

} // namespace

// The window and the entities the code reads and changes.
struct ExportUi : FormUi
{
    ExportUi()
        : FormUi(exportSurface)
    {
    }

    struct SceneRow
    {
        std::string path;
        Entity toggle;
        Entity label;
    };
    struct FolderRow
    {
        Entity label;
        Button remove;
    };

    bool built = false;
    float builtFont = 0.0f;
    std::string signature;
    // The settings as the window edits them, and whether they wait to be written.
    asset::ExportSettings settings;
    bool dirty = false;
    std::filesystem::path projectFile;

    // The key of an Android game, kept apart from the project.
    asset::AndroidSigning signing;
    Entity platform;
    Entity androidPackage;
    Entity androidVersionName;
    Entity androidVersionCode;
    Entity androidOrientation;
    Entity androidArm64;
    Entity androidX86;
    Entity androidFormat;
    Entity androidRelease;
    Entity androidKeystore;
    Entity androidAlias;
    Entity androidStorePassword;
    Entity androidKeyPassword;
    Entity androidInstall;
    Entity androidNote;
    Entity executable;
    Entity output;
    Button browse;
    Entity configuration;
    Entity buildNote;
    std::vector<SceneRow> scenes;
    std::vector<FolderRow> folders;
    Entity addFolder;
    std::vector<std::string> folderChoices;
    std::size_t exportCard = 0;
    Button exportButton;
    Button openFolder;
    Button runGame;
    Entity progressLine;
    Entity progress;
    Entity progressText;
    Entity statusLine;
    Entity statusIcon;
    Entity statusText;

    void buildCards(ToolsState& state, EditorUiKit& kit);
    void sync(ToolsState& state, EditorUiKit& kit);
    void answer(ToolsState& state);
    void save(ToolsState& state, bool force);
    void update(ToolsState& state, EditorUiKit& kit, core::Duration delta);
};

void ExportUi::buildCards(ToolsState& state, EditorUiKit& kit)
{
    const ThemeColors& colors = themeColors();
    const float tool = line - 6.0f;

    const bool android = settings.platform == "android";
    Section& game = card(kit, "Game");
    platform = choice(formRow(game, "Platform").editor, {"Desktop", "Android"});
    const FormRow executableRow = formRow(game, android ? "File" : "Executable");
    executable = text(executableRow.editor, whole(math::Vec4{font * 0.3f, 0.0f, 0.0f, 0.0f}), "", "text");
    tooltip(executableRow.row, "Named after the game: rename it in Project > Project Settings");
    const FormRow outputRow = formRow(game, "Output Folder");
    output = field(outputRow.editor, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {0.0f, 0.0f}, .offsetMax = {-tool - gap * 2.0f, 0.0f}},
                   "", asset::ExportSettings{}.output);
    tooltip(outputRow.row, "Relative to the project folder, or absolute. A previous export there is replaced.");
    browse = toolButton(kit, outputRow.editor, Icon::FolderOpen, rightButton(tool, 0.0f));
    tooltip(browse.entity, "Choose the folder");
    configuration = android ? Entity{} : choice(formRow(game, "Configuration").editor, {"Release", "Debug"});
    buildNote = note(&game, "", "dim", 2.0f);

    if (android)
    {
        Section& phone = card(kit, "Android");
        const auto password = [&](Entity field) {
            scene().get<scene::UiInput>(field).password = true;
            return field;
        };
        const FormRow packageRow = formRow(phone, "Package Name");
        androidPackage = textField(packageRow.editor, asset::androidPackageFromName(state.database->project().name));
        tooltip(packageRow.row, "The name Android knows the game by: it cannot change once the game is published");
        androidVersionName = textField(formRow(phone, "Version").editor);
        const FormRow codeRow = formRow(phone, "Version Code");
        androidVersionCode = textField(codeRow.editor);
        tooltip(codeRow.row, "A whole number that grows with each version sent to the Play Store");
        androidOrientation = choice(formRow(phone, "Orientation").editor, {"Landscape", "Portrait", "Sensor"});
        androidArm64 = toggle(formRow(phone, "Phones (arm64)").editor);
        androidX86 = toggle(formRow(phone, "Emulator (x86_64)").editor);
        androidFormat = choice(formRow(phone, "Format").editor, {"APK, to install directly", "AAB, for the Play Store"});
        const FormRow installRow = formRow(phone, "Install on Device");
        androidInstall = toggle(installRow.editor);
        tooltip(installRow.row, "Installs and starts the APK on the connected device or emulator after exporting it");
        const FormRow releaseRow = formRow(phone, "Release Key");
        androidRelease = toggle(releaseRow.editor);
        tooltip(releaseRow.row, "Signs with your key, as the Play Store asks. Without it, a debug key made by the tools signs the game.");
        if (settings.android.release)
        {
            androidKeystore = textField(formRow(phone, "Keystore File").editor, "The path of the .jks or .keystore file");
            androidAlias = textField(formRow(phone, "Key Alias").editor);
            androidStorePassword = password(textField(formRow(phone, "Keystore Password").editor));
            androidKeyPassword = password(textField(formRow(phone, "Key Password").editor));
            note(&phone, "These stay on this computer, in .devex/ of the project, in the clear, and out of version control.", "dim", 2.0f);
        }
        androidNote = note(&phone, "", "dim", 3.0f);
    }

    Section& list = card(kit, "Scenes");
    scenes.clear();
    const float box = std::round(font * 1.3f);
    for (std::string& path : scenePaths(*state.database))
    {
        const Entity row = add(list.card, "Scene", wide(line));
        SceneRow made;
        made.path = std::move(path);
        made.toggle = add(row, "Exported",
                          UiRect{.anchorMin = {0.0f, 0.5f}, .anchorMax = {0.0f, 0.5f}, .offsetMin = {font * 0.35f, -box * 0.5f},
                                 .offsetMax = {font * 0.35f + box, box * 0.5f}},
                          "toggle");
        scene().add<scene::UiImage>(made.toggle);
        scene().add<scene::UiToggle>(made.toggle);
        scene().add<scene::UiButton>(made.toggle);
        made.label = text(row, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {font * 0.85f + box, 0.0f}, .offsetMax = {0.0f, 0.0f}},
                          made.path, "text");
        list.lines.push_back(Line{.entity = row});
        scenes.push_back(std::move(made));
    }
    note(&list, "The scenes and prefabs that exported scenes refer to are exported with them.", "dim", 2.0f);

    Section& included = card(kit, "Always Included Folders");
    folders.clear();
    const float iconSize = std::round(font * 1.1f);
    for (std::size_t index = 0; index < settings.includeFolders.size(); ++index)
    {
        const Entity row = add(included.card, "Folder", wide(line));
        const Entity glyph = icon(kit, row,
                                  UiRect{.anchorMin = {0.0f, 0.5f}, .anchorMax = {0.0f, 0.5f}, .offsetMin = {font * 0.35f, -iconSize * 0.5f},
                                         .offsetMax = {font * 0.35f + iconSize, iconSize * 0.5f}},
                                  Icon::Folder, {});
        scene().get<scene::UiImage>(glyph).color = linearColor(colors.folder);
        FolderRow made;
        made.label = text(row, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {font * 0.85f + iconSize, 0.0f}, .offsetMax = {-tool - gap, 0.0f}},
                          "", "text");
        made.remove = toolButton(kit, row, Icon::Trash, rightButton(tool, 0.0f));
        tooltip(made.remove.entity, "Stop including the folder");
        included.lines.push_back(Line{.entity = row});
        folders.push_back(made);
    }
    const Entity adding = add(included.card, "Add", wide(line));
    addFolder = choice(add(adding, "Editor", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 1.0f}, .offsetMin = {font * 0.35f, 2.0f},
                                                    .offsetMax = {font * 17.0f, -2.0f}}));
    included.lines.push_back(Line{.entity = adding});
    note(&included, "Every asset of these folders is exported, for the assets that code loads by itself.", "dim", 2.0f);

    // The export, its progress and what came of it.
    exportCard = sections.size();
    Section& run = card(kit, "Export");
    const Entity buttons = actions(&run);
    exportButton = button(kit, buttons, Icon::Package, "Export", "primary", std::round(font * 9.0f), line - 4.0f);
    openFolder = action(kit, buttons, Icon::FolderOpen, "Open Folder");
    runGame = action(kit, buttons, Icon::Play, "Run Game");
    progressLine = add(run.card, "Progress", wide(line));
    progress = add(progressLine, "Bar", whole(math::Vec4{font * 0.35f, 3.0f, 0.0f, 3.0f}), "list");
    scene().add<scene::UiImage>(progress);
    scene().add<scene::UiSlider>(progress, scene::UiSlider{.handleSize = 0.0f, .interactable = false});
    progressText = text(progress, whole(), "", "text", false, scene::TextAlign::Center);
    run.lines.push_back(Line{.entity = progressLine});
    statusLine = add(run.card, "Status", wide(std::round(font * 3.2f)));
    statusIcon = icon(kit, statusLine,
                      UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 0.0f}, .offsetMin = {font * 0.35f, font * 0.3f},
                             .offsetMax = {font * 0.35f + iconSize, font * 0.3f + iconSize}},
                      Icon::CircleCheck, {});
    statusText = text(statusLine, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {font * 0.85f + iconSize, 0.0f}, .offsetMax = {0.0f, 0.0f}},
                      "", "text");
    scene().get<scene::UiText>(statusText).wrap = true;
    scene().get<scene::UiText>(statusText).verticalAlign = scene::TextVerticalAlign::Top;
    run.lines.push_back(Line{.entity = statusLine});
}

void ExportUi::sync(ToolsState& state, EditorUiKit& kit)
{
    const ThemeColors& colors = themeColors();
    const asset::Project& project = state.database->project();
    const bool android = settings.platform == "android";
    const std::string executableName = executableFileName(project.name);
    setChoice(platform, {"Desktop", "Android"}, android ? 1 : 0);
    scene().get<scene::UiText>(executable).text =
        android ? executableName.substr(0, executableName.size() - 4) + (settings.android.format == "aab" ? ".aab" : ".apk") : executableName;
    setText(output, android ? settings.android.output : settings.output);
    if (configuration.isValid())
    {
        setChoice(configuration, {"Release", "Debug"}, settings.configuration == "Debug" ? 1 : 0);
    }
    const auto build = std::ranges::find_if(state.engineBuilds, [&](const EngineBuildChoice& choice) { return choice.configuration == settings.configuration; });
    scene::UiText& buildText = scene().get<scene::UiText>(buildNote);
    bool exportable = build != state.engineBuilds.end() && !settings.output.empty();
    if (android)
    {
        const asset::AndroidExportSettings& phone = settings.android;
        setText(androidPackage, phone.package);
        setText(androidVersionName, phone.versionName);
        setText(androidVersionCode, std::to_string(phone.versionCode));
        setChoice(androidOrientation, {"Landscape", "Portrait", "Sensor"}, phone.orientation == "portrait" ? 1 : phone.orientation == "sensor" ? 2 : 0);
        setToggle(androidArm64, phone.arm64);
        setToggle(androidX86, phone.x86_64);
        setChoice(androidFormat, {"APK, to install directly", "AAB, for the Play Store"}, phone.format == "aab" ? 1 : 0);
        setToggle(androidInstall, phone.install);
        setToggle(androidRelease, phone.release);
        if (androidKeystore.isValid())
        {
            setText(androidKeystore, core::toUtf8(signing.keystore));
            setText(androidAlias, signing.alias);
            setText(androidStorePassword, signing.storePassword);
            setText(androidKeyPassword, signing.keyPassword);
        }
        const auto hasBuild = [&](std::string_view abi) { return std::ranges::contains(state.androidAbis, abi); };
        const bool available = (phone.arm64 && hasBuild("arm64-v8a")) || (phone.x86_64 && hasBuild("x86_64"));
        const std::string package = phone.package.empty() ? asset::androidPackageFromName(project.name) : phone.package;
        std::string problem;
        if (!state.androidProblem.empty())
        {
            problem = state.androidProblem + ".";
        }
        else if (!available)
        {
            problem = "There is no Android build of the engine for these architectures: build the preset android-arm64 or android-x86_64 first.";
        }
        else if (!asset::validAndroidPackage(package))
        {
            problem = package + " is not a valid package name: words joined by dots, such as com.studio.game.";
        }
        else if (phone.release && (signing.keystore.empty() || signing.alias.empty()))
        {
            problem = "A release needs its keystore file and key alias.";
        }
        else if (phone.install && phone.format == "aab")
        {
            problem = "An AAB does not install on a device: export an APK to try the game.";
        }
        exportable = problem.empty();
        const bool csharp = hasCSharp(project);
        scene().get<scene::UiText>(androidNote).text =
            !problem.empty() ? problem
            : csharp ? "This project has C# code, which does not run on Android yet: its components will do nothing, and only its C++ code runs."
                     : (phone.arm64 ? "Textures are converted to ASTC for phones, which takes a while the first time." : "Textures stay in BC, which the emulator reads.");
        scene().get<UiRect>(androidNote).style = !problem.empty() || csharp ? "warning" : "dim";
        buildText.text = "The first export downloads Gradle and what it needs, which takes some minutes.";
        scene().get<UiRect>(buildNote).style = "dim";
    }
    else if (build == state.engineBuilds.end())
    {
        buildText.text = std::format("There is no {} build of the engine next to the editor. Build it first, for instance with cmake --build --preset "
                                     "build-x64-{}.",
                                     settings.configuration, settings.configuration == "Debug" ? "debug" : "release");
        scene().get<UiRect>(buildNote).style = "warning";
    }
    else
    {
        buildText.text = std::format("With the engine build {}. {}", build->name,
                                     settings.configuration == "Debug" ? "Debug games run slower and need Visual Studio."
                                                                       : "Players need nothing else installed.");
        scene().get<UiRect>(buildNote).style = "dim";
    }

    // The startup scene is always exported.
    for (const SceneRow& row : scenes)
    {
        const bool startup = !project.startupScene.empty() && row.path == project.startupScene;
        const bool listed = std::ranges::find(settings.scenes, row.path) != settings.scenes.end();
        scene().get<scene::UiToggle>(row.toggle).value = startup || listed;
        scene().get<scene::UiToggle>(row.toggle).interactable = !startup;
        scene().get<scene::UiButton>(row.toggle).interactable = !startup;
        scene().get<scene::UiText>(row.label).text = startup ? std::format("{} (startup)", row.path) : row.path;
        scene().get<UiRect>(row.label).style = startup ? "dim" : "text";
    }
    for (std::size_t index = 0; index < folders.size() && index < settings.includeFolders.size(); ++index)
    {
        scene().get<scene::UiText>(folders[index].label).text = settings.includeFolders[index];
    }
    folderChoices.clear();
    for (std::string& folder : assetFolders(*state.database))
    {
        if (std::ranges::find(settings.includeFolders, folder) == settings.includeFolders.end())
        {
            folderChoices.push_back(std::move(folder));
        }
    }
    setChoice(addFolder, folderChoices, -1);
    scene::UiDropdown& adding = scene().get<scene::UiDropdown>(addFolder);
    adding.placeholder = folderChoices.empty() ? "Every folder is included" : "Add a folder...";
    adding.interactable = !folderChoices.empty();

    const ExportStatus& status = state.exportStatus;
    const bool running = status.state == ExportStatus::State::Running;
    relabel(kit, exportButton, running ? "Exporting..." : "Export");
    enable(exportButton, !running && exportable);
    Section& run = sections[exportCard];
    const bool succeeded = status.state == ExportStatus::State::Succeeded;
    const bool failed = status.state == ExportStatus::State::Failed;
    scene().get<UiRect>(openFolder.entity).visible = succeeded;
    scene().get<UiRect>(runGame.entity).visible = succeeded && !android;
    enable(runGame, !status.executable.empty());
    showLine(run, progressLine, running);
    showLine(run, statusLine, succeeded || failed);
    scene::UiSlider& bar = scene().get<scene::UiSlider>(progress);
    bar.value = std::clamp(status.fraction, 0.0f, 1.0f);
    bar.fillColor = linearColor(math::Vec4(colors.accent.x, colors.accent.y, colors.accent.z, 0.8f));
    scene().get<scene::UiText>(progressText).text = status.message;
    scene().get<scene::UiText>(statusText).text = status.message;
    scene().get<UiRect>(statusText).style = failed ? "error" : "text";
    scene().get<scene::UiImage>(statusIcon).texture = kit.icon(failed ? Icon::CircleX : Icon::CircleCheck);
    scene().get<scene::UiImage>(statusIcon).color = linearColor(failed ? colors.error : colors.success);
}

void ExportUi::answer(ToolsState& state)
{
    const ui::UiWorld& world = panel.world();
    const bool android = settings.platform == "android";
    const auto typed = [&](Entity field) -> const std::string& { return scene().get<scene::UiText>(field).text; };
    if (endedField.isValid() && endedField == output)
    {
        (android ? settings.android.output : settings.output) = typed(output);
    }
    if (world.wasChanged(platform))
    {
        settings.platform = scene().get<scene::UiDropdown>(platform).selected == 1 ? "android" : "desktop";
    }
    if (android)
    {
        asset::AndroidExportSettings& phone = settings.android;
        if (endedField.isValid())
        {
            if (endedField == androidPackage)
            {
                phone.package = typed(androidPackage);
            }
            else if (endedField == androidVersionName)
            {
                phone.versionName = typed(androidVersionName);
            }
            else if (endedField == androidVersionCode)
            {
                std::int32_t code = phone.versionCode;
                const std::string& text = typed(androidVersionCode);
                if (std::from_chars(text.data(), text.data() + text.size(), code).ec == std::errc{} && code > 0)
                {
                    phone.versionCode = code;
                }
            }
            else if (androidKeystore.isValid())
            {
                asset::AndroidSigning changed = signing;
                if (endedField == androidKeystore)
                {
                    changed.keystore = core::pathFromUtf8(typed(androidKeystore));
                }
                else if (endedField == androidAlias)
                {
                    changed.alias = typed(androidAlias);
                }
                else if (endedField == androidStorePassword)
                {
                    changed.storePassword = typed(androidStorePassword);
                }
                else if (endedField == androidKeyPassword)
                {
                    changed.keyPassword = typed(androidKeyPassword);
                }
                if (changed != signing)
                {
                    signing = std::move(changed);
                    if (core::Result<void> written = asset::writeAndroidSigning(state.database->project(), signing); !written)
                    {
                        DEVEX_LOG_ERROR("Cannot save the key of the game: {}", written.error());
                    }
                }
            }
        }
        if (world.wasChanged(androidOrientation))
        {
            const std::int32_t selected = scene().get<scene::UiDropdown>(androidOrientation).selected;
            phone.orientation = selected == 1 ? "portrait" : selected == 2 ? "sensor" : "landscape";
        }
        if (world.wasChanged(androidFormat))
        {
            phone.format = scene().get<scene::UiDropdown>(androidFormat).selected == 1 ? "aab" : "apk";
        }
        const auto answerToggle = [&](Entity toggleEntity, bool& value) {
            if (world.wasChanged(toggleEntity))
            {
                value = scene().get<scene::UiToggle>(toggleEntity).value;
            }
        };
        answerToggle(androidArm64, phone.arm64);
        answerToggle(androidX86, phone.x86_64);
        answerToggle(androidInstall, phone.install);
        answerToggle(androidRelease, phone.release);
    }
    if (world.wasClicked(browse.entity))
    {
        showOutputFolderDialog(state);
    }
    if (configuration.isValid() && world.wasChanged(configuration))
    {
        settings.configuration = scene().get<scene::UiDropdown>(configuration).selected == 1 ? "Debug" : "Release";
    }
    for (const SceneRow& row : scenes)
    {
        if (world.wasChanged(row.toggle))
        {
            std::erase(settings.scenes, row.path);
            if (scene().get<scene::UiToggle>(row.toggle).value)
            {
                settings.scenes.push_back(row.path);
            }
        }
    }
    std::optional<std::size_t> removed;
    for (std::size_t index = 0; index < folders.size(); ++index)
    {
        if (world.wasClicked(folders[index].remove.entity))
        {
            removed = index;
        }
    }
    if (removed && *removed < settings.includeFolders.size())
    {
        settings.includeFolders.erase(settings.includeFolders.begin() + static_cast<std::ptrdiff_t>(*removed));
    }
    if (world.wasChanged(addFolder))
    {
        const std::int32_t index = scene().get<scene::UiDropdown>(addFolder).selected;
        if (index >= 0 && static_cast<std::size_t>(index) < folderChoices.size())
        {
            settings.includeFolders.push_back(folderChoices[static_cast<std::size_t>(index)]);
        }
    }
    if (world.wasClicked(exportButton.entity) && state.exportStatus.state != ExportStatus::State::Running)
    {
        state.requests.exportGame = true;
    }
    const ExportStatus& status = state.exportStatus;
    if (world.wasClicked(openFolder.entity))
    {
        if (core::Result<void> opened = state.platform.openPath(status.output); !opened)
        {
            DEVEX_LOG_WARNING("{}", opened.error());
        }
    }
    if (world.wasClicked(runGame.entity) && !status.executable.empty())
    {
        const std::array<std::string, 1> arguments{core::toUtf8(status.executable)};
        if (core::Result<void> launched = platform::Process::launch(arguments, status.output); !launched)
        {
            DEVEX_LOG_ERROR("Cannot run the game: {}", launched.error());
        }
    }
    dirty = settings != state.database->project().exportSettings;
}

void ExportUi::save(ToolsState& state, bool force)
{
    const ui::UiWorld& world = panel.world();
    if (!dirty || (!force && (world.held().isValid() || world.isEditing())))
    {
        return;
    }
    dirty = false;
    asset::Project changed = state.database->project();
    changed.exportSettings = settings;
    if (core::Result<void> written = state.database->updateProject(changed); !written)
    {
        DEVEX_LOG_ERROR("Cannot save the project: {}", written.error());
    }
}

void ExportUi::update(ToolsState& state, EditorUiKit& kit, core::Duration delta)
{
    const ThemeColors& colors = themeColors();
    kit.refreshTheme(colors);
    setFont(state.theme.fontSize);
    if (!built || builtFont != font)
    {
        if (built)
        {
            clearForm();
            std::vector<Entity> children;
            for (Entity child = scene().firstChild(panel.canvas()); child.isValid(); child = scene().nextSibling(child))
            {
                children.push_back(child);
            }
            for (const Entity child : children)
            {
                if (scene().has<scene::UiPopup>(child))
                {
                    panel.world().closePopup(scene(), child);
                }
                scene().destroyEntity(child);
            }
        }
        built = true;
        builtFont = font;
        signature.clear();
        buildForm(add({}, "Export", whole()), whole(math::Vec4{font * 0.3f, font * 0.3f, 0.0f, 0.0f}));
        panel.setKeyboardNavigation(false);
    }
    styleTooltips(colors);

    const asset::Project& project = state.database->project();
    if (!dirty || projectFile != project.file)
    {
        if (projectFile != project.file)
        {
            signing = asset::readAndroidSigning(project);
        }
        settings = project.exportSettings;
        projectFile = project.file;
        dirty = false;
    }
    if (std::optional<std::filesystem::path> folder = std::exchange(state.dialogAnswers->exportFolder, std::nullopt))
    {
        // Folders inside the project are kept relative to it, so that the project can move.
        const std::string resource = project.resourcePath(*folder);
        (settings.platform == "android" ? settings.android.output : settings.output) =
            resource.empty() ? core::toUtf8(*folder) : resource.substr(asset::resourceScheme.size());
        dirty = true;
    }
    std::string wanted = std::format("{}|{}|{}|{}|", font, settings.includeFolders.size(), settings.platform, settings.android.release);
    for (const std::string& path : scenePaths(*state.database))
    {
        wanted += path;
        wanted += ',';
    }
    if (wanted != signature)
    {
        clearForm();
        buildCards(state, kit);
        signature = std::move(wanted);
    }
    labelWidth = std::clamp(std::round(panel.size().x * 0.3f), font * 7.0f, font * 11.0f);
    sync(state, kit);
    layoutCards();
    panel.update(kit, delta, UiPanel::zoomFor(font));
    answerForm();
    answer(state);
    // Written once an edit ends, and always before an export starts.
    save(state, state.requests.exportGame);
}

void drawExportWindow(ToolsState& state)
{
    DEVEX_PROFILE_SCOPE("Export window");
    if (!state.showExport || state.database == nullptr)
    {
        return;
    }
    EditorUiKit& kit = editorUiKit(state);
    if (!state.exportUi)
    {
        state.exportUi = std::make_shared<ExportUi>();
    }
    if (beginFormWindow(state, exportWindow, &state.showExport, 36.0f, 36.0f))
    {
        state.exportUi->update(state, kit, core::Duration(state.input.delta()));
        endFormWindow(state);
    }
}

void renderExportWindow(ToolsState& state, render::RenderWorld& world)
{
    if (state.exportUi && state.uiKit)
    {
        state.exportUi->panel.render(*state.uiKit, world, linearColor(themeColors().panel));
    }
}

} // namespace devex::tools::detail
