// The dialogs of the editor, made with the interface of the engine: a card in the middle of the window
// over a veil, as the Create window is. New Script asks for the name and the language of a component,
// the unsaved changes dialog asks what to do with the scenes and files an action would drop, and About
// tells what the editor is made with.
#include "SettingsUi.hpp"

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

// A component name is a C# or C++ identifier.
[[nodiscard]] bool isIdentifier(std::string_view name)
{
    return !name.empty() && (std::isalpha(static_cast<unsigned char>(name.front())) != 0 || name.front() == '_') &&
           std::ranges::all_of(name, [](char character) { return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_'; });
}

} // namespace

enum class DialogKind : std::uint8_t
{
    NewScript,
    UnsavedChanges,
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
    Entity file;
    Entity status;
    Button confirm;
    Button discard;
    Button cancel;
    bool focusName = false;
    UnsavedChoice unsaved = UnsavedChoice::None;

    void start(DialogKind which, ToolsState& state, EditorUiKit& kit, scene::Scene& edited);
    void update(ToolsState& state, EditorUiKit& kit, core::Duration delta);
    void titleRow(EditorUiKit& kit, Icon glyph, ImVec4 color, std::string title);
    // A row of a label and the editor at its right, returned.
    Entity row(const char* label);
    // Buttons at the bottom right of the card.
    Entity buttonLine();
    // The card is as tall as what it holds.
    void fit();
};

void EditorDialogsUi::titleRow(EditorUiKit& kit, Icon glyph, ImVec4 color, std::string title)
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

void EditorDialogsUi::start(DialogKind which, ToolsState& state, EditorUiKit& kit, scene::Scene& edited)
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
    open = true;
    unsaved = UnsavedChoice::None;
    focusName = false;
    language = scriptName = file = status = Entity{};
    confirm = discard = cancel = Button{};
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
    case DialogKind::NewScript: {
        size.x = std::round(font * 30.0f);
        titleRow(kit, Icon::FilePlus, colors.gameCode, "New Script");
        note(nullptr, "A component with fields shown in the inspector, written to the code folder of the project.", "dim", 2.0f);
        language = choice(row("Language"), {"C#", "C++"});
        scene().get<scene::UiDropdown>(language).selected = state.newScriptCSharp ? 0 : 1;
        scriptName = field(row("Name"), whole(), state.newScriptName, "Component name");
        file = text(row("File"), whole(math::Vec4{font * 0.3f, 0.0f, 0.0f, 0.0f}), "", "dim", false, scene::TextAlign::Left, std::round(font * 0.9f));
        scene().get<scene::UiText>(file).font = EditorUiKit::monoFont();
        status = note(nullptr, "", "dim", 2.0f);
        const Entity buttons = buttonLine();
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
        const auto listed = [&](Icon glyph, ImVec4 color, std::string name, bool bold) {
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
        constexpr std::array<std::pair<const char*, const char*>, 8> credits{{
            {"Dear ImGui", "MIT"},
            {"SDL 3", "zlib"},
            {"Vulkan, volk, Vulkan Memory Allocator", "Apache 2.0, MIT"},
            {"GLM, fastgltf, ufbx, Basis Universal", "MIT, MIT, MIT, Apache 2.0"},
            {"FreeType, plutosvg", "FreeType License, MIT"},
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

void EditorDialogsUi::update(ToolsState& state, EditorUiKit& kit, core::Duration delta)
{
    const ThemeColors& colors = themeColors();
    kit.refreshTheme(colors);
    styleTooltips(colors);
    ui::UiWorld& world = panel.world();

    std::string name;
    bool valid = false;
    bool csharp = true;
    if (kind == DialogKind::NewScript)
    {
        // Checked as it is typed.
        name = scene().get<scene::UiText>(scriptName).text;
        csharp = scene().get<scene::UiDropdown>(language).selected != 1;
        const std::string fileName = std::format("{}{}", name.empty() ? std::string("Name") : name, csharp ? ".cs" : ".cpp");
        std::error_code error;
        const bool exists = state.database != nullptr && isIdentifier(name) &&
                            std::filesystem::exists(state.database->project().codeDirectory() / core::pathFromUtf8(fileName), error);
        valid = isIdentifier(name) && scene::componentRegistry().find(name) == nullptr && !exists;
        scene().get<scene::UiText>(file).text = std::format("res://code/{}", fileName);
        scene().get<scene::UiText>(status).text =
            name.empty()                                        ? "The component needs a name."
            : !isIdentifier(name)                               ? "Use letters, digits and _, not starting with a digit."
            : scene::componentRegistry().find(name) != nullptr ? "A component has this name already."
            : exists                                            ? "A file of this name is in the code folder already."
            : state.selection.active().isNil()                 ? "The file opens in the Text Editor once written."
                                                                : "The component is added to the selected entity once its code is compiled.";
        scene().get<UiRect>(status).style = valid ? "dim" : "warning";
        enable(confirm, valid);
    }

    panel.update(kit, delta, UiPanel::zoomFor(font));
    answerForm();
    if (focusName && scriptName.isValid())
    {
        focusName = false;
        world.startEditing(scene(), scriptName, true);
    }

    const bool escape = panel.input().cancelPressed || (panel.focused() && ImGui::IsKeyPressed(ImGuiKey_Escape, false));
    switch (kind)
    {
    case DialogKind::NewScript:
        state.newScriptName = name;
        state.newScriptCSharp = csharp;
        if (valid && (world.wasClicked(confirm.entity) || world.wasSubmitted(scriptName)))
        {
            state.requests.newScript = NewScript{.name = name, .csharp = csharp};
            state.pendingScript = name;
            state.pendingScriptEntity = state.selection.active();
            open = false;
        }
        else if (world.wasClicked(cancel.entity) || escape)
        {
            open = false;
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
            (panel.focused() && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))))
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
    if (std::exchange(state.openNewScriptPopup, false))
    {
        requested = DialogKind::NewScript;
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
        state.dialogsUi->start(*requested, state, kit, scene);
        ImGui::OpenPopup(dialogPopup);
    }
    EditorDialogsUi* const ui = state.dialogsUi.get();
    if (ui == nullptr || !ui->open)
    {
        return;
    }

    // In the middle of the window, over the veil of a modal, the card at the size of what it holds.
    const ImGuiViewport* const viewport = ImGui::GetMainViewport();
    const float unit = ImGui::GetFontSize() / std::max(regularFontPixels(ui->font), 1.0f);
    ImGui::SetNextWindowPos(viewport->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(ui->size.x * unit, ui->size.y * unit), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    const bool visible = ImGui::BeginPopupModal(dialogPopup, nullptr, flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    if (!visible)
    {
        ui->open = false;
        return;
    }
    ui->update(state, kit, core::Duration(ImGui::GetIO().DeltaTime));
    if (!ui->open)
    {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();

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
