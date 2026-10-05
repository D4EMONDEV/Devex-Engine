// The dialogs of the editor, made with the interface of the engine: a card in the middle of the window
// over a veil, as the Create window is. Create New asks for a type, a name and a destination folder,
// Move To for the folder a file of FileSystem goes to, the unsaved changes dialog asks what to do with
// the scenes and files an action would drop, and About tells what the editor is made with.
#include "SettingsUi.hpp"
#include "EditorModal.hpp"

#include <devex/core/Profiler.hpp>
#include <devex/core/BuildInfo.hpp>
#include <devex/core/Path.hpp>
#include <devex/scene/ComponentRegistry.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace devex::tools::detail {

using scene::Entity;
using scene::UiRect;
using namespace rects;
using Button = PanelButton;

namespace {

constexpr const char* dialogPopup = "##editor dialog";

struct FileType
{
    Icon icon;
    std::string_view label;
    std::string_view defaultName;
    std::string_view extension;
    std::string_view description;
};
constexpr std::array<FileType, 6> fileTypes{{
    {Icon::FilePlus, "Script", "NewComponent", ".cs", "A game component written in C# or C++."},
    {Icon::Activity, "Curve", "Curve", ".dvxcurve", "A curve for tweens and animation."},
    {Icon::Clapperboard, "Sprite Frames", "Sprite Frames", ".dvxframes", "Named animations made from sprites."},
    {Icon::Grid, "Tileset", "Tileset", ".dvxtileset", "Tiles for painting a tilemap."},
    {Icon::Workflow, "Animator", "Animator", ".dvxanimator", "A state machine of animations."},
    {Icon::Folder, "Folder", "New Folder", "", "An empty folder for organizing assets or scripts."},
}};
constexpr std::size_t folderType = fileTypes.size() - 1;

// A component name is a C# or C++ identifier.
[[nodiscard]] bool isIdentifier(std::string_view name)
{
    return !name.empty() && (std::isalpha(static_cast<unsigned char>(name.front())) != 0 || name.front() == '_') &&
           std::ranges::all_of(name, [](char character) { return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_'; });
}

} // namespace

enum class DialogKind : std::uint8_t
{
    CreateNew,
    NewAsset,
    NewScript,
    UnsavedChanges,
    DeleteFile,
    MoveFile,
    About,
};

// What the unsaved changes dialog was answered.
enum class UnsavedChoice : std::uint8_t
{
    None,
    Save,
    Discard,
    Cancel,
};

struct EditorDialogsUi : FormUi
{
    EditorDialogsUi()
        : FormUi(dialogSurface)
    {
    }

    DialogKind kind = DialogKind::About;
    bool open = false;
    // The size of the card, in units.
    math::Vec2 size{400.0f, 300.0f};
    Entity root;
    Entity language;
    Entity scriptName;
    Entity folder;
    std::vector<std::string> folders;
    // Move To shows the folders as a tree, one line each, and the one chosen.
    std::vector<Entity> folderRows;
    std::size_t chosenFolder = 0;
    std::array<Button, fileTypes.size()> types;
    std::size_t selectedType = 0;
    bool fileSystemCreation = false;
    std::optional<DialogKind> next;
    Button back;
    std::string writeError;
    std::string validatedInput;
    Entity file;
    Entity status;
    Button confirm;
    Button discard;
    Button cancel;
    bool focusName = false;
    UnsavedChoice unsaved = UnsavedChoice::None;

    void start(DialogKind which, ToolsState& state, EditorUiKit& kit, scene::Scene& edited, bool creationFlow = false);
    void update(ToolsState& state, EditorUiKit& kit, scene::Scene& edited, core::Duration delta);
    void titleRow(EditorUiKit& kit, Icon glyph, math::Vec4 color, std::string title);
    // A row of a label and the editor at its right, returned.
    Entity row(const char* label);
    // Buttons at the bottom right of the card.
    Entity buttonLine();
    // The card is as tall as what it holds.
    void fit();
};

void EditorDialogsUi::titleRow(EditorUiKit& kit, Icon glyph, math::Vec4 color, std::string title)
{
    const float iconSize = std::round(font * 1.4f);
    const Entity top = add(root, "Title", wide(std::round(line * 1.1f)));
    const Entity mark = icon(kit, top,
                             UiRect{.anchorMin = {0.0f, 0.5f}, .anchorMax = {0.0f, 0.5f}, .offsetMin = {0.0f, -iconSize * 0.5f},
                                    .offsetMax = {iconSize, iconSize * 0.5f}},
                             glyph, {});
    scene().get<scene::UiImage>(mark).color = linearColor(color);
    text(top, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {iconSize + font * 0.6f, 0.0f}, .offsetMax = {0.0f, 0.0f}},
         std::move(title), "text", true, scene::TextAlign::Left, std::round(font * 1.15f));
}

Entity EditorDialogsUi::row(const char* label)
{
    const float labels = std::round(font * 6.5f);
    const Entity made = add(root, "Row", wide(line));
    text(made, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 1.0f}, .offsetMin = {0.0f, 0.0f}, .offsetMax = {labels, 0.0f}}, label, "label");
    return add(made, "Editor", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {labels, 2.0f}, .offsetMax = {0.0f, -2.0f}});
}

Entity EditorDialogsUi::buttonLine()
{
    const Entity made = add(root, "Buttons", wide(std::round(font * 2.2f)));
    scene().add<scene::UiLayout>(made, scene::UiLayout{.kind = scene::UiLayoutKind::Row, .spacing = font * 0.5f, .align = scene::TextAlign::Right});
    return made;
}

void EditorDialogsUi::fit()
{
    const scene::UiLayout& layout = scene().get<scene::UiLayout>(root);
    float height = layout.padding.y + layout.padding.w;
    std::size_t shown = 0;
    for (Entity child = scene().firstChild(root); child.isValid(); child = scene().nextSibling(child))
    {
        const UiRect& rect = scene().get<UiRect>(child);
        if (rect.visible)
        {
            height += rect.offsetMax.y - rect.offsetMin.y;
            ++shown;
        }
    }
    size.y = std::round(height + layout.spacing * static_cast<float>(shown > 0 ? shown - 1 : 0));
}

void EditorDialogsUi::start(DialogKind which, ToolsState& state, EditorUiKit& kit, scene::Scene& edited, bool creationFlow)
{
    const ThemeColors& colors = themeColors();
    kit.refreshTheme(colors);
    std::vector<Entity> children;
    for (Entity child = scene().firstChild(panel.canvas()); child.isValid(); child = scene().nextSibling(child))
    {
        children.push_back(child);
    }
    for (const Entity child : children)
    {
        scene().destroyEntity(child);
    }
    setFont(state.theme.fontSize);
    kind = which;
    fileSystemCreation = creationFlow;
    next.reset();
    writeError.clear();
    validatedInput.clear();
    open = true;
    unsaved = UnsavedChoice::None;
    focusName = false;
    language = scriptName = file = status = folder = Entity{};
    folderRows.clear();
    confirm = discard = cancel = back = Button{};
    types = {};
    root = add({}, "Dialog", whole(), "dialog");
    scene().add<scene::UiImage>(root);
    scene().add<scene::UiLayout>(root, scene::UiLayout{.kind = scene::UiLayoutKind::Column,
                                                       .spacing = std::round(font * 0.55f),
                                                       .padding = math::Vec4{std::round(font * 1.2f)},
                                                       .align = scene::TextAlign::Left});
    // The pieces of forms that go between the cards go into the card of the dialog.
    content = root;
    const float buttonWidth = std::round(font * 8.0f);
    const float buttonHeight = std::round(font * 2.0f);

    switch (which)
    {
    case DialogKind::CreateNew: {
        size.x = std::round(font * 34.0f);
        titleRow(kit, Icon::FilePlus, colors.accent, "Create New");
        note(nullptr, "Choose what to create, then its name and destination folder.", "dim", 2.0f);
        for (std::size_t index = 0; index < fileTypes.size(); ++index)
        {
            const auto& type = fileTypes[index];
            types[index] = button(kit, root, type.icon, type.label, "button", size.x - std::round(font * 2.4f), std::round(font * 2.5f));
            tooltip(types[index].entity, std::string(type.description));
        }
        cancel = button(kit, buttonLine(), Icon::Close, "Cancel", "button", buttonWidth, buttonHeight);
        break;
    }
    case DialogKind::DeleteFile: {
        size.x = std::round(font * 34.0f);
        titleRow(kit, Icon::Trash, colors.warning, "Delete permanently?");
        note(nullptr, state.fileToDelete, "text", 3.0f);
        note(nullptr, "This removes the file or folder, its contents and associated import metadata. It cannot be undone.", "warning", 3.0f);
        note(nullptr, "Affected scene and text tabs will close. Their unsaved changes will be lost.", "dim", 2.0f);
        status = note(nullptr, "", "warning", 3.0f);
        scene().get<UiRect>(status).visible = false;
        const Entity buttons = buttonLine();
        cancel = button(kit, buttons, Icon::Close, "Cancel", "button", buttonWidth, buttonHeight);
        confirm = button(kit, buttons, Icon::Trash, "Delete", "button", buttonWidth, buttonHeight);
        break;
    }
    case DialogKind::MoveFile: {
        size.x = std::round(font * 38.0f);
        titleRow(kit, Icon::Move, colors.accent, "Move To");
        note(nullptr, state.fileToMove, "text", 3.0f);
        // The folders of the same content root, outside what moves.
        const bool code = state.fileToMove.starts_with("res://code/");
        folders = state.database ? creationFolders(state.database->project(), code ? asset::ContentRoot::Code : asset::ContentRoot::Assets)
                                 : std::vector<std::string>{};
        std::erase_if(folders, [&](const std::string& candidate) {
            return candidate == state.fileToMove || candidate.starts_with(state.fileToMove + "/");
        });
        // The folders as a tree in a list that scrolls, as Godot's chooser of folders shows them,
        // the one that holds it chosen.
        const float tall = std::round(font * 1.8f);
        const float indent = std::round(font * 1.2f);
        const Entity box = add(root, "Folders", wide(std::round((tall + 1.0f) * 9.0f) + 4.0f), "list");
        scene().add<scene::UiImage>(box);
        const Entity scrolled = add(box, "List",
                                    UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {0.0f, 2.0f}, .offsetMax = {0.0f, -2.0f},
                                           .clipChildren = true},
                                    "scroll");
        scene().add<scene::UiScroll>(scrolled, scene::UiScroll{.speed = tall * 3.0f});
        const Entity lines = add(scrolled, "Rows", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 0.0f}, .offsetMin = {2.0f, 0.0f}, .offsetMax = {-10.0f, 1.0f}});
        scene().add<scene::UiLayout>(lines, scene::UiLayout{.kind = scene::UiLayoutKind::Column, .spacing = 1.0f, .align = scene::TextAlign::Left});
        const std::string_view base = code ? "res://code" : "res://assets";
        for (const std::string& path : folders)
        {
            const std::string_view inside = std::string_view(path).substr(std::min(base.size(), path.size()));
            const auto depth = static_cast<float>(std::ranges::count(inside, '/'));
            const std::string label = inside.empty() ? path : path.substr(path.rfind('/') + 1);
            const Button made = button(kit, lines, Icon::Folder, label, "row", -1.0f, tall, scene::TextAlign::Left);
            scene().get<scene::UiImage>(made.icon).color = linearColor(colors.folder);
            scene().get<UiRect>(made.icon).style = {};
            scene().get<UiRect>(made.label).style = "text";
            scene().get<scene::UiLayout>(made.entity).padding.x += depth * indent;
            tooltip(made.entity, path);
            folderRows.push_back(made.entity);
        }
        scene().get<UiRect>(lines).offsetMax.y = (tall + 1.0f) * static_cast<float>(folderRows.size());
        const auto found = std::ranges::find(folders, state.fileToMove.substr(0, state.fileToMove.rfind('/')));
        chosenFolder = found == folders.end() ? 0 : static_cast<std::size_t>(found - folders.begin());
        scene().get<scene::UiScroll>(scrolled).offset.y = std::max((static_cast<float>(chosenFolder) - 3.0f) * (tall + 1.0f), 0.0f);
        status = note(nullptr, "", "dim", 2.0f);
        const Entity buttons = buttonLine();
        confirm = button(kit, buttons, Icon::Move, "Move", "primary", buttonWidth, buttonHeight);
        cancel = button(kit, buttons, Icon::Close, "Cancel", "button", buttonWidth, buttonHeight);
        break;
    }
    case DialogKind::NewAsset:
    case DialogKind::NewScript: {
        const bool script = which == DialogKind::NewScript;
        const bool directory = !script && selectedType == folderType;
        const auto& type = fileTypes[script ? 0 : selectedType];
        size.x = std::round(font * 38.0f);
        titleRow(kit, type.icon, script ? colors.gameCode : colors.accent, std::format("New {}", type.label));
        note(nullptr, std::string(type.description), "dim", 2.0f);
        if (script)
        {
            language = choice(row("Language"), {"C#", "C++"});
            scene().get<scene::UiDropdown>(language).selected = state.newScriptCSharp ? 0 : 1;
        }
        scriptName = field(row("Name"), whole(), script ? state.newScriptName : state.newFileName,
                           script ? "Component name" : directory ? "Folder name" : "File name");
        folders = state.database ? creationFolders(state.database->project(), script ? asset::ContentRoot::Code : asset::ContentRoot::Assets)
                                 : std::vector<std::string>{script ? "res://code" : "res://assets"};
        if (directory && state.database)
        {
            const auto codeFolders = creationFolders(state.database->project(), asset::ContentRoot::Code);
            folders.insert(folders.end(), codeFolders.begin(), codeFolders.end());
        }
        folder = choice(row(directory ? "Parent Folder" : "Folder"), folders);
        const auto found = std::ranges::find(folders, state.newFileFolder);
        scene().get<scene::UiDropdown>(folder).selected = found == folders.end() ? 0 : static_cast<std::int32_t>(found - folders.begin());
        file = note(nullptr, "", "dim", 2.0f);
        scene().get<scene::UiText>(file).font = EditorUiKit::monoFont();
        status = note(nullptr, "", "dim", 2.0f);
        const Entity buttons = buttonLine();
        if (fileSystemCreation)
        {
            back = button(kit, buttons, std::nullopt, "Back", "button", buttonWidth, buttonHeight);
        }
        confirm = button(kit, buttons, Icon::FilePlus, "Create", "primary", buttonWidth, buttonHeight);
        cancel = button(kit, buttons, Icon::Close, "Cancel", "button", buttonWidth, buttonHeight);
        focusName = true;
        break;
    }
    case DialogKind::UnsavedChanges: {
        size.x = std::round(font * 30.0f);
        titleRow(kit, Icon::TriangleAlert, colors.warning, "Save changes before continuing?");
        const std::vector<std::size_t> tabs = tabsWithUnsavedChanges(state, edited);
        const std::vector<TextDocument*> texts = state.pendingAction ? affectedTextDocuments(state, *state.pendingAction) : std::vector<TextDocument*>{};
        const ActiveDocument live = activeDocument(state, edited);
        const float iconSize = std::round(font * 1.1f);
        const auto listed = [&](Icon glyph, math::Vec4 color, std::string name, bool bold) {
            const Entity entry = add(root, "File", wide(std::round(font * 1.7f)));
            const Entity mark = icon(kit, entry,
                                     UiRect{.anchorMin = {0.0f, 0.5f}, .anchorMax = {0.0f, 0.5f}, .offsetMin = {font * 1.6f, -iconSize * 0.5f},
                                            .offsetMax = {font * 1.6f + iconSize, iconSize * 0.5f}},
                                     glyph, {});
            scene().get<scene::UiImage>(mark).color = linearColor(color);
            text(entry, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {font * 2.2f + iconSize, 0.0f}, .offsetMax = {0.0f, 0.0f}},
                 std::move(name), "text", bold);
        };
        for (const std::size_t index : tabs)
        {
            listed(Icon::Clapperboard, colors.scene, tabName(state.tabs.path(index, live)), true);
        }
        for (const TextDocument* document : texts)
        {
            listed(Icon::FileText, colors.neutral, core::toUtf8(document->path.filename()), false);
        }
        note(nullptr, "Changes that are not saved are lost.", "dim");
        const Entity buttons = buttonLine();
        confirm = button(kit, buttons, Icon::Save, tabs.size() + texts.size() > 1 ? "Save All" : "Save", "primary", buttonWidth, buttonHeight);
        discard = button(kit, buttons, std::nullopt, "Don't Save", "button", buttonWidth, buttonHeight);
        cancel = button(kit, buttons, std::nullopt, "Cancel", "button", buttonWidth, buttonHeight);
        break;
    }
    case DialogKind::About: {
        constexpr std::array<std::pair<const char*, const char*>, 7> credits{{
            {"SDL 3", "zlib"},
            {"Vulkan, volk, Vulkan Memory Allocator", "Apache 2.0, MIT"},
            {"GLM, fastgltf, ufbx, Basis Universal", "MIT, MIT, MIT, Apache 2.0"},
            {"plutosvg", "MIT"},
            {"Noto Sans, JetBrains Mono", "SIL Open Font License 1.1"},
            {"Lucide icons", "ISC"},
            {"Slang", "Apache 2.0"},
        }};
        // The names in a column, their licenses in another, each as wide as its longest line.
        float names = 0.0f;
        float licenses = 0.0f;
        for (const auto& [name, license] : credits)
        {
            names = std::max(names, kit.textWidth(EditorUiKit::regularFont(), name, font));
            licenses = std::max(licenses, kit.textWidth(EditorUiKit::regularFont(), license, font));
        }
        const float indent = std::round(font * 0.6f);
        const float between = std::round(font * 1.5f);
        const float padding = scene().get<scene::UiLayout>(root).padding.x;
        size.x = std::max(std::round(font * 28.0f), std::round(padding * 2.0f + indent + names + between + licenses + font * 0.5f));
        const float logo = std::round(font * 3.6f);
        const Entity top = add(root, "Top", wide(std::round(font * 4.4f)));
        const Entity mark = icon(kit, top,
                                 UiRect{.anchorMin = {0.0f, 0.5f}, .anchorMax = {0.0f, 0.5f}, .offsetMin = {0.0f, -logo * 0.5f}, .offsetMax = {logo, logo * 0.5f}},
                                 Icon::Logo, {});
        scene().get<scene::UiImage>(mark).color = linearColor(colors.text);
        const float left = logo + font * 1.0f;
        const float middle = std::round(font * 4.4f) * 0.5f;
        text(top, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 0.0f}, .offsetMin = {left, middle - font * 2.1f}, .offsetMax = {0.0f, middle - font * 0.5f}},
             "Devex Engine", "text", true, scene::TextAlign::Left, std::round(font * 1.4f));
        text(top, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 0.0f}, .offsetMin = {left, middle - font * 0.4f}, .offsetMax = {0.0f, middle + font * 0.8f}},
             std::format("Version {}, {} build", core::version(), core::buildType()), "dim");
        text(top, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 0.0f}, .offsetMin = {left, middle + font * 0.8f}, .offsetMax = {0.0f, middle + font * 2.0f}},
             "Open source under the MIT license", "dim");
        text(root, wide(std::round(font * 1.8f)), "Built with", "text", true);
        for (const auto& [name, license] : credits)
        {
            const Entity entry = add(root, "Credit", wide(std::round(font * 1.5f)));
            text(entry, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 1.0f}, .offsetMin = {indent, 0.0f}, .offsetMax = {indent + names + 2.0f, 0.0f}},
                 name, "text");
            text(entry, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {indent + names + between, 0.0f}, .offsetMax = {0.0f, 0.0f}},
                 license, "dim");
        }
        const Entity buttons = buttonLine();
        confirm = button(kit, buttons, std::nullopt, "Close", "primary", buttonWidth, buttonHeight);
        break;
    }
    }
    fit();
}

void EditorDialogsUi::update(ToolsState& state, EditorUiKit& kit, scene::Scene& edited, core::Duration delta)
{
    const ThemeColors& colors = themeColors();
    kit.refreshTheme(colors);
    styleTooltips(colors);
    ui::UiWorld& world = panel.world();
    panel.update(kit, delta, UiPanel::zoomFor(font));
    answerForm();

    std::string name;
    bool valid = false;
    bool csharp = true;
    if (kind == DialogKind::NewScript || kind == DialogKind::NewAsset)
    {
        const bool script = kind == DialogKind::NewScript;
        const bool directory = !script && selectedType == folderType;
        name = scene().get<scene::UiText>(scriptName).text;
        csharp = !script || scene().get<scene::UiDropdown>(language).selected != 1;
        const auto index = scene().get<scene::UiDropdown>(folder).selected;
        state.newFileFolder = index >= 0 && static_cast<std::size_t>(index) < folders.size() ? folders[index] : std::string{};
        const bool inCode = state.newFileFolder == "res://code" || state.newFileFolder.starts_with("res://code/");
        const std::string fileName = name + std::string(script ? (csharp ? ".cs" : ".cpp") : fileTypes[selectedType].extension);
        scene().get<scene::UiText>(file).text = state.newFileFolder + "/" + fileName;
        if (const std::string input = state.newFileFolder + "/" + fileName; input != validatedInput)
        {
            validatedInput = input;
            writeError.clear();
        }
        std::string error;
        if (!state.database)
        {
            error = "No project is open.";
        }
        else if (name.empty())
        {
            error = "Enter a name.";
        }
        else if (script && !isIdentifier(name))
        {
            error = "Use letters, digits and _, not starting with a digit.";
        }
        else if (script && scene::componentRegistry().find(name))
        {
            error = "A component has this name already.";
        }
        else if (directory && inCode && (name == "bin" || name == "obj"))
        {
            error = "This folder name is reserved for build outputs.";
        }
        else if (const auto path = state.database->project().newFilePath(state.newFileFolder, fileName,
                         script || (directory && inCode) ? asset::ContentRoot::Code : asset::ContentRoot::Assets); !path)
        {
            error = path.error().message;
        }
        valid = error.empty();
        const std::string hint = script ? (state.newScriptTarget.isNil() ? "The script will open in the Text Editor. Scripts stay inside res://code."
            : "The component will be added to the selected entity after compilation.")
            : directory ? "The empty folder will appear in FileSystem once created."
                        : "The asset will appear in FileSystem once created.";
        scene().get<scene::UiText>(status).text = !error.empty() ? error : !writeError.empty() ? writeError : hint;
        scene().get<UiRect>(status).style = valid && writeError.empty() ? "dim" : "warning";
        enable(confirm, valid);
    }
    // Where Move To sends the file, and whether it can go there.
    std::string destination;
    const std::string moved = state.fileToMove.substr(state.fileToMove.rfind('/') + 1);
    bool chosenTwice = false;
    if (kind == DialogKind::MoveFile)
    {
        for (std::size_t index = 0; index < folderRows.size(); ++index)
        {
            if (world.wasClicked(folderRows[index]) || world.wasDoubleClicked(folderRows[index]))
            {
                chosenFolder = index;
                chosenTwice = world.wasDoubleClicked(folderRows[index]);
            }
        }
        for (std::size_t index = 0; index < folderRows.size(); ++index)
        {
            scene().get<UiRect>(folderRows[index]).style = index == chosenFolder ? "row_selected" : "row";
        }
        destination = chosenFolder < folders.size() ? folders[chosenFolder] : std::string{};
        if (destination != validatedInput)
        {
            validatedInput = destination;
            writeError.clear();
        }
        std::string error;
        if (!state.database)
        {
            error = "No project is open.";
        }
        else if (destination.empty())
        {
            error = "Choose a folder.";
        }
        else if (destination == state.fileToMove.substr(0, state.fileToMove.rfind('/')))
        {
            error = "It is in this folder already.";
        }
        else if (const auto path = state.database->project().newFilePath(
                     destination, moved, state.fileToMove.starts_with("res://code/") ? asset::ContentRoot::Code : asset::ContentRoot::Assets);
                 !path)
        {
            error = path.error().message;
        }
        valid = error.empty();
        scene().get<scene::UiText>(status).text = !error.empty() ? error
                                                  : !writeError.empty() ? writeError
                                                                        : "The scenes and texts open from it, and the paths of the project, follow it.";
        scene().get<UiRect>(status).style = valid && writeError.empty() ? "dim" : "warning";
        enable(confirm, valid);
    }
    if (focusName && scriptName.isValid())
    {
        focusName = false;
        world.startEditing(scene(), scriptName, true);
    }

    const bool escape = panel.input().cancelPressed || (panel.focused() && state.input.pressed(platform::Key::Escape, false));
    switch (kind)
    {
    case DialogKind::CreateNew:
        if (world.wasClicked(cancel.entity) || escape)
        {
            open = false;
        }
        for (std::size_t index = 0; index < types.size(); ++index)
        {
            if (world.wasClicked(types[index].entity))
            {
                selectedType = index;
                state.newFileName = std::string(fileTypes[index].defaultName);
                state.newScriptTarget = {};
                next = index == 0 ? DialogKind::NewScript : DialogKind::NewAsset;
            }
        }
        break;
    case DialogKind::NewAsset:
        state.newFileName = name;
        if (world.wasClicked(cancel.entity) || escape)
        {
            open = false;
        }
        else if (world.wasClicked(back.entity))
        {
            next = DialogKind::CreateNew;
        }
        else if (valid && (world.wasClicked(confirm.entity) || world.wasSubmitted(scriptName)))
        {
            const auto created = [&]() -> core::Result<std::filesystem::path> {
                switch (selectedType)
                {
                case 1: return createCurveFile(state, state.newFileFolder, name);
                case 2: return createSpriteFramesFile(state, state.newFileFolder, {}, name);
                case 3: return createTilesetFile(state, state.newFileFolder, {}, name);
                case folderType: return createContentFolder(state, state.newFileFolder, name);
                default: return createAnimatorFile(state, state.newFileFolder, name);
                }
            }();
            if (created)
            {
                open = false;
            }
            else
            {
                writeError = created.error().message;
            }
        }
        break;
    case DialogKind::DeleteFile:
        if (world.wasClicked(cancel.entity) || escape)
        {
            open = false;
            state.fileToDelete.clear();
        }
        else if (world.wasClicked(confirm.entity))
        {
            if (auto result = deleteFileSystemPath(state, edited, state.fileToDelete); result)
            {
                open = false;
                state.fileToDelete.clear();
            }
            else
            {
                scene().get<scene::UiText>(status).text = result.error().message;
                scene().get<UiRect>(status).visible = true;
                fit();
            }
        }
        break;
    case DialogKind::MoveFile:
        if (world.wasClicked(cancel.entity) || escape)
        {
            open = false;
            state.fileToMove.clear();
        }
        else if (valid && (world.wasClicked(confirm.entity) || chosenTwice))
        {
            if (core::Result<std::string> result = moveFileSystemPath(state, edited, state.fileToMove, destination, moved); result)
            {
                open = false;
                state.fileToMove.clear();
            }
            else
            {
                writeError = result.error().message;
            }
        }
        break;
    case DialogKind::NewScript:
        state.newScriptName = name;
        state.newScriptCSharp = csharp;
        if (valid && (world.wasClicked(confirm.entity) || world.wasSubmitted(scriptName)))
        {
            state.requests.newScript = NewScript{.name = name, .csharp = csharp, .folder = state.newFileFolder};
            state.pendingScript = name;
            state.pendingScriptEntity = state.newScriptTarget;
            open = false;
        }
        else if (world.wasClicked(cancel.entity) || escape)
        {
            open = false;
        }
        else if (world.wasClicked(back.entity))
        {
            next = DialogKind::CreateNew;
        }
        break;
    case DialogKind::UnsavedChanges:
        if (world.wasClicked(confirm.entity))
        {
            unsaved = UnsavedChoice::Save;
        }
        else if (world.wasClicked(discard.entity))
        {
            unsaved = UnsavedChoice::Discard;
        }
        else if (world.wasClicked(cancel.entity) || escape)
        {
            unsaved = UnsavedChoice::Cancel;
        }
        open = unsaved == UnsavedChoice::None;
        break;
    case DialogKind::About:
        if (world.wasClicked(confirm.entity) || escape ||
            (panel.focused() && (state.input.pressed(platform::Key::Enter, false) || state.input.pressed(platform::Key::KeypadEnter, false))))
        {
            open = false;
        }
        break;
    }
}

void drawEditorPopups(ToolsState& state, scene::Scene& scene)
{
    DEVEX_PROFILE_SCOPE("Dialogs");
    std::optional<DialogKind> requested;
    bool creationFlow = false;
    if (state.dialogsUi && state.dialogsUi->next)
    {
        requested = std::exchange(state.dialogsUi->next, std::nullopt);
        creationFlow = state.dialogsUi->fileSystemCreation;
    }
    if (std::exchange(state.openCreateFilePopup, false))
    {
        requested = DialogKind::CreateNew;
        creationFlow = true;
    }
    if (std::exchange(state.openDeleteFilePopup, false))
    {
        requested = DialogKind::DeleteFile;
    }
    if (std::exchange(state.openMoveFilePopup, false))
    {
        requested = DialogKind::MoveFile;
    }
    if (std::exchange(state.openNewScriptPopup, false))
    {
        requested = DialogKind::NewScript;
        creationFlow = false;
    }
    if (std::exchange(state.openAboutPopup, false))
    {
        requested = DialogKind::About;
    }
    if (std::exchange(state.openUnsavedChangesPopup, false))
    {
        requested = DialogKind::UnsavedChanges;
    }
    EditorUiKit& kit = editorUiKit(state);
    if (requested)
    {
        if (!state.dialogsUi)
        {
            state.dialogsUi = std::make_shared<EditorDialogsUi>();
        }
        state.dialogsUi->start(*requested, state, kit, scene, creationFlow);
        openModal(state, dialogPopup);
    }
    EditorDialogsUi* const ui = state.dialogsUi.get();
    if (ui == nullptr || !ui->open)
    {
        closeModal(state, dialogPopup);
        return;
    }

    // In the middle of the window, over the veil of a modal, the card at the size of what it holds;
    // under another modal, it waits.
    const float unit = themeMetrics().lineHeight / std::max(regularFontPixels(ui->font), 1.0f);
    if (!beginModal(state, dialogPopup, math::Vec2(ui->size.x * unit, ui->size.y * unit)))
    {
        return;
    }
    ui->update(state, kit, scene, core::Duration(state.input.delta()));
    endModal(state);
    if (!ui->open)
    {
        closeModal(state, dialogPopup);
    }

    // What the unsaved changes dialog was answered, once it is closed: saving may open the dialog
    // that names an untitled scene, and continuing may ask again.
    switch (std::exchange(ui->unsaved, UnsavedChoice::None))
    {
    case UnsavedChoice::Save:
        if (saveForPendingAction(state, scene))
        {
            continuePendingAction(state, scene);
        }
        break;
    case UnsavedChoice::Discard:
        discardPendingAction(state, scene);
        break;
    case UnsavedChoice::Cancel:
        cancelPendingAction(state);
        break;
    case UnsavedChoice::None:
        break;
    }
}

void renderEditorDialogs(ToolsState& state, render::RenderWorld& world)
{
    if (state.dialogsUi && state.uiKit)
    {
        // Clear around the rounded card, which the veil shows through.
        state.dialogsUi->panel.render(*state.uiKit, world, math::Vec4{0.0f});
    }
}

} // namespace devex::tools::detail
