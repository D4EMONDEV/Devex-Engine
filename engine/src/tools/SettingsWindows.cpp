// Editor Settings and Project Settings, made with the interface of the engine as Godot's are made with
// its controls: the sections at the left, a filter above that finds a setting by its name in all of
// them, and the cards of the section chosen, whose rows edit the settings as the inspector edits
// components. The theme applies as it is changed; the project is written once an edit ends.
#include "SettingsUi.hpp"

#include "EditorModal.hpp"
#include "FileSearch.hpp"

#include <devex/asset/TranslationData.hpp>
#include <devex/core/Profiler.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>
#include <devex/ui/Color.hpp>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <format>
#include <numbers>
#include <string>
#include <unordered_map>
#include <utility>

namespace devex::tools::detail {

using scene::Entity;
using scene::UiRect;
using namespace rects;
using Button = PanelButton;

namespace {

constexpr std::array<std::string_view, 1> single = singleNumber;
constexpr std::array interfaceScales{0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.5f};
constexpr std::array<std::string_view, 5> editorPages{"Theme", "Display", "Viewport",
                                                      "Script Editors", "Compilation"};
constexpr std::size_t viewportPage = 2;
constexpr std::size_t scriptsPage = 3;
constexpr std::size_t compilationPage = 4;
constexpr std::array<std::string_view, 9> projectPages{
    "Application", "Window",    "Physics",      "Collision Layers", "Sorting Layers",
    "Audio",       "Input Map", "Localization", "Globals"};

[[nodiscard]] math::Vec4 linearOf(math::Vec3 srgb)
{
    return ui::linearFromSrgb(math::Vec4{srgb.x, srgb.y, srgb.z, 1.0f});
}

[[nodiscard]] math::Vec3 srgbOf(math::Vec4 linear)
{
    const math::Vec4 color = ui::srgbFromLinear(math::Vec4{std::clamp(linear.x, 0.0f, 1.0f), std::clamp(linear.y, 0.0f, 1.0f),
                                                           std::clamp(linear.z, 0.0f, 1.0f), 1.0f});
    return math::Vec3{color.x, color.y, color.z};
}

// Whether the settings the window edits differ between two states of the project.
[[nodiscard]] bool differs(const asset::Project& first, const asset::Project& second)
{
    return first.name != second.name || first.startupScene != second.startupScene ||
           first.physics != second.physics || first.window != second.window ||
           first.audio != second.audio || first.input != second.input ||
           first.sorting != second.sorting || first.globals != second.globals;
}

} // namespace

void placeRight(scene::Scene& scene, Entity entity, float right, float width)
{
    UiRect& rect = scene.get<UiRect>(entity);
    rect.anchorMin = {1.0f, 0.0f};
    rect.anchorMax = {1.0f, 1.0f};
    rect.offsetMin = {-right - width, 0.0f};
    rect.offsetMax = {-right, 0.0f};
}

UiRect rightButton(float size, float right)
{
    return UiRect{.anchorMin = {1.0f, 0.5f}, .anchorMax = {1.0f, 0.5f}, .offsetMin = {-right - size, -size * 0.5f}, .offsetMax = {-right, size * 0.5f}};
}

void numberCursor(ToolsState& state, UiPanel& panel)
{
    const ui::UiWorld& world = panel.world();
    const Entity pointed = world.held().isValid() ? world.held() : world.hovered();
    if (pointed.isValid() && panel.scene().isAlive(pointed) && panel.scene().has<scene::UiNumberField>(pointed) &&
        world.editedField() != pointed)
    {
        state.input.cursor = platform::Cursor::ResizeHorizontal;
    }
}

EditorUiKit& editorUiKit(ToolsState& state)
{
    if (!state.uiKit)
    {
        state.uiKit = std::make_shared<EditorUiKit>(state.renderer, state.icons, state.platform.baseDirectory() / "resources" / "fonts");
        // Every panel can preview project images, even when the inspector has never been opened.
        state.uiKit->setAssetImages(
            [&state](asset::AssetId id) { return state.textures ? state.textures(id) : render::TextureHandle{}; },
            [&state](asset::AssetId id) {
                const math::Extent2D size = state.textureSizes ? state.textureSizes(id) : math::Extent2D{};
                return math::Vec2{static_cast<float>(size.width), static_cast<float>(size.height)};
            },
            [&state](asset::AssetId id) -> std::optional<ui::SpriteImage> {
                const asset::AssetInfo* const info = state.database != nullptr ? state.database->find(id) : nullptr;
                if (info == nullptr || info->type != asset::AssetType::Sprite || !state.sprites)
                {
                    return std::nullopt;
                }
                const std::shared_ptr<const asset::SpriteData> sprite = state.sprites(id);
                if (sprite == nullptr || !state.textures)
                {
                    return ui::SpriteImage{};
                }
                return ui::SpriteImage{.texture = state.textures(sprite->texture),
                                       .uv = sprite->uvRect(),
                                       .size = math::Vec2{static_cast<float>(sprite->width), static_cast<float>(sprite->height)}};
            });
    }
    return *state.uiKit;
}

bool beginFormWindow(ToolsState& state, const char* title, bool* open, float width, float height)
{
    if (takeModalClose(state, title))
    {
        *open = false;
    }
    if (!*open)
    {
        closeModal(state, title);
        return false;
    }
    // As the windows of settings of Godot: over the editor, which waits until they close.
    if (std::ranges::find(state.modals, title) == state.modals.end())
    {
        openModal(state, title);
    }
    const float line = themeMetrics().lineHeight;
    return beginModal(state, title, math::Vec2(line * width, line * height), title);
}

void endFormWindow(ToolsState& state)
{
    endModal(state);
}

// ---- The frame of the windows of settings ----

SettingsUi::SettingsUi(std::uint32_t surface)
    : FormUi(surface)
{
}

void SettingsUi::clearAll()
{
    ui::UiWorld& world = panel.world();
    if (built)
    {
        clearForm();
    }
    std::vector<Entity> children;
    for (Entity child = scene().firstChild(panel.canvas()); child.isValid(); child = scene().nextSibling(child))
    {
        children.push_back(child);
    }
    for (const Entity child : children)
    {
        if (scene().has<scene::UiPopup>(child))
        {
            world.closePopup(scene(), child);
        }
        scene().destroyEntity(child);
    }
    pages.clear();
    sections.clear();
    formRows.clear();
    grids.clear();
    pageEntities.clear();
    top = filter = list = scroll = content = sidebar = heading = noMatches = Entity{};
    tabs.clear();
    closeRequested = false;
    filterDirty = true;
    colorPopup = picker = hexField = channelRow = Entity{};
    channels = {};
    editingField = endedField = Entity{};
    built = false;
    signature.clear();
}

void SettingsUi::setFont(float size) noexcept
{
    FormUi::setFont(size);
    line = std::round(font * 2.0f);
    headerHeight = std::round(font * 2.25f);
}

void SettingsUi::buildFrame(EditorUiKit& kit, std::span<const std::string_view> names)
{
    built = true;
    builtFont = font;
    projectTabs = names.size() == projectPages.size();
    filterDirty = true;
    const float margin = std::round(font * 0.8f);
    const float listWidth = std::round(font * 14.0f);
    const float tabsHeight = projectTabs ? line + margin : 0;
    const float below = margin * 2 + line + tabsHeight;
    const float footerHeight = line + margin * 2;
    const Entity root = add({}, "Settings", whole());
    if (projectTabs)
    {
        const auto strip = add(root, "Project Sections",
                               UiRect{.anchorMin = {0, 0},
                                      .anchorMax = {1, 0},
                                      .offsetMin = {margin, margin},
                                      .offsetMax = {-margin, margin + line}});
        scene().add<scene::UiLayout>(strip, scene::UiLayout{.kind = scene::UiLayoutKind::Row,
                                                            .spacing = gap,
                                                            .align = scene::TextAlign::Left});
        static constexpr std::array titles{"General", "Input Map", "Localization", "Globals"};
        static constexpr std::array icons{Icon::Settings, Icon::Keyboard, Icon::Languages,
                                          Icon::Globe};
        tabs.clear();
        for (std::size_t i = 0; i < titles.size(); ++i)
            tabs.push_back(button(kit, strip, icons[i], titles[i], "flat", 0, line));
    }
    top = add(root, "Top",
              UiRect{.anchorMin = {0, 0},
                     .anchorMax = {1, 0},
                     .offsetMin = {margin, margin + tabsHeight},
                     .offsetMax = {-margin, margin + tabsHeight + line}});
    filter = searchField(kit, top,
                         UiRect{.anchorMin = {0, 0},
                                .anchorMax = {1, 1},
                                .offsetMin = {0, 0},
                                .offsetMax = {projectTabs ? 0 : -font * 16, 0}},
                         "", "Filter Settings");
    tooltip(filter, "Search every category by words in a setting or section name");
    clearSearch = toolButton(kit, filter, Icon::Close, rightButton(closeButtonSize(), 0));
    tooltip(clearSearch.entity, "Clear the settings filter");
    scene().get<scene::UiInput>(filter).padding.x = font * 1.9f;
    sidebar = add(root, "Sections Scroll",
                  UiRect{.anchorMin = {0, 0},
                         .anchorMax = {0, 1},
                         .offsetMin = {margin, below},
                         .offsetMax = {margin + listWidth, -footerHeight}},
                  "list");
    scene().add<scene::UiImage>(sidebar);
    scene().add<scene::UiScroll>(sidebar);
    list = add(sidebar, "Sections",
               UiRect{.anchorMin = {0, 0},
                      .anchorMax = {1, 0},
                      .offsetMin = {pad, pad},
                      .offsetMax = {-pad, pad + static_cast<float>(names.size()) * (line + gap)}});
    scene().add<scene::UiLayout>(list, scene::UiLayout{.kind = scene::UiLayoutKind::Column,
                                                       .spacing = gap,
                                                       .align = scene::TextAlign::Left});
    static constexpr std::array projectIcons{Icon::Box,      Icon::Monitor,     Icon::Shapes,
                                             Icon::Layers,   Icon::ArrowUpDown, Icon::Volume,
                                             Icon::Keyboard, Icon::Languages,   Icon::Globe};
    static constexpr std::array editorIcons{Icon::Palette, Icon::Monitor, Icon::Axis, Icon::Code,
                                            Icon::Hammer};
    for (std::size_t i = 0; i < names.size(); ++i)
        pages.push_back(
            Page{.name = std::string(names[i]),
                 .button = button(kit, list, projectTabs ? projectIcons[i] : editorIcons[i],
                                  names[i], "row", -1, line, scene::TextAlign::Left)});
    heading = text(root,
                   UiRect{.anchorMin = {0, 0},
                          .anchorMax = {1, 0},
                          .offsetMin = {margin + listWidth + margin, below},
                          .offsetMax = {-margin, below + line}},
                   "", "text", true);
    buildForm(root, UiRect{.anchorMin = {0, 0},
                           .anchorMax = {1, 1},
                           .offsetMin = {margin + listWidth + margin, below + line + gap},
                           .offsetMax = {-margin, -footerHeight}});
    noMatches = text(root,
                     UiRect{.anchorMin = {0, 0},
                            .anchorMax = {1, 1},
                            .offsetMin = {margin + listWidth + margin, below + line * 2},
                            .offsetMax = {-margin, -footerHeight}},
                     "No matching settings.", "dim", false, scene::TextAlign::Center);
    scene().get<UiRect>(noMatches).visible = false;
    const auto footer = add(root, "Footer",
                            UiRect{.anchorMin = {0, 1},
                                   .anchorMax = {1, 1},
                                   .offsetMin = {margin, -footerHeight + margin},
                                   .offsetMax = {-margin, -margin}});
    saveStatus = text(footer,
                      UiRect{.anchorMin = {0, 0},
                             .anchorMax = {1, 1},
                             .offsetMin = {0, 0},
                             .offsetMax = {-font * 9, 0}},
                      "Changes are saved automatically.", "dim");
    close = button(kit, footer, {}, "Close", "button", font * 8, line);
    placeRight(scene(), close.entity, 0, font * 8);
    buildColorPopup();
    panel.setKeyboardNavigation(false);
}

Section& SettingsUi::pageCard(EditorUiKit& kit, std::size_t page, std::string name)
{
    Section& section = card(kit, std::move(name));
    section.page = static_cast<int>(page);
    filterDirty = true;
    return section;
}

void SettingsUi::applyFilter()
{
    const std::string& wanted = scene().get<scene::UiText>(filter).text;
    const bool searching = wanted.find_first_not_of(" \t\r\n") != std::string::npos;
    revealFiltered = searching;
    scene().get<UiRect>(clearSearch.entity).visible = !wanted.empty();
    if (filterDirty || wanted != appliedFilter || selected != appliedPage)
    {
        if (wanted != appliedFilter || selected != appliedPage)
            scene().get<scene::UiScroll>(scroll).offset = math::Vec2{0};
        appliedFilter = wanted;
        appliedPage = selected;
        filterDirty = false;
        std::unordered_map<std::uint32_t, std::string> labels;
        for (const auto& row : formRows)
            labels[row.row.index] = scene().get<scene::UiText>(row.label).text;
        std::vector<bool> found(pages.size(), false);
        bool anyFound = false;
        for (auto& section : sections)
        {
            if (section.page < 0 || static_cast<std::size_t>(section.page) >= pages.size())
                continue;
            const auto page = static_cast<std::size_t>(section.page);
            const bool whole =
                !searching || matchesFileSearch(pages[page].name + " " + section.name, wanted);
            bool any = whole;
            std::vector<bool> groups(section.groups.size(), false);
            for (auto& entry : section.lines)
            {
                if (entry.heading)
                    continue;
                std::string terms = pages[page].name + " " + section.name;
                if (entry.group >= 0)
                    terms += " " + section.groups[static_cast<std::size_t>(entry.group)];
                if (auto row = labels.find(entry.entity.index); row != labels.end())
                    terms += " " + row->second;
                else if (auto* label = scene().tryGet<scene::UiText>(entry.entity))
                    terms += " " + label->text;
                entry.filtered = !whole && !matchesFileSearch(terms, wanted);
                if (!entry.filtered && entry.shown)
                {
                    any = true;
                    if (entry.group >= 0)
                        groups[static_cast<std::size_t>(entry.group)] = true;
                }
            }
            for (auto& entry : section.lines)
                if (entry.heading)
                    entry.filtered = !whole && !groups[static_cast<std::size_t>(entry.group)];
            section.hidden = searching ? !any : page != selected;
            found[page] = found[page] || any;
            anyFound = anyFound || !section.hidden;
        }
        scene().get<UiRect>(noMatches).visible = searching && !anyFound;
        for (std::size_t i = 0; i < pages.size(); ++i)
        {
            scene().get<UiRect>(pages[i].button.entity).style =
                !searching && i == selected ? "row_selected" : "row";
            scene().get<UiRect>(pages[i].button.label).opacity = !searching || found[i] ? 1 : 0.45f;
        }
    }
    const bool showSidebar = !projectTabs || searching || selected < 6;
    scene().get<UiRect>(sidebar).visible = showSidebar;
    for (std::size_t i = 0; i < pages.size(); ++i)
        scene().get<UiRect>(pages[i].button.entity).visible = !projectTabs || searching || i < 6;
    const float left = font * (showSidebar ? 15.6f : 0.8f);
    scene().get<UiRect>(scroll).offsetMin.x = left;
    scene().get<UiRect>(heading).offsetMin.x = left + font * 0.3f;
    scene().get<UiRect>(noMatches).offsetMin.x = left;
    scene().get<scene::UiText>(heading).text =
        searching ? "Search Results"
                  : (projectTabs && selected < 6 ? "General / " : "") + pages[selected].name;
    const std::size_t activeTab = selected < 6 ? 0 : selected - 5;
    for (std::size_t i = 0; i < tabs.size(); ++i)
        scene().get<UiRect>(tabs[i].entity).style =
            !searching && i == activeTab ? "row_selected" : "flat";
}

void SettingsUi::answerFrame()
{
    const ui::UiWorld& world = panel.world();
    const auto select = [&](std::size_t page)
    {
        selected = page;
        scene().get<scene::UiText>(filter).text.clear();
        filterDirty = true;
    };
    for (std::size_t i = 0; i < pages.size(); ++i)
        if (world.wasClicked(pages[i].button.entity))
            select(i);
    for (std::size_t i = 0; i < tabs.size(); ++i)
        if (world.wasClicked(tabs[i].entity))
            select(i == 0 ? 0 : i + 5);
    if (world.wasClicked(clearSearch.entity))
        scene().get<scene::UiText>(filter).text.clear();
    closeRequested = world.wasClicked(close.entity);
}

// ---- Editor Settings ----

// The theme of the editor and the sizes of its text, which apply as they change and are saved with
// the settings of the user once an edit ends.
struct EditorSettingsUi : SettingsUi
{
    EditorSettingsUi()
        : SettingsUi(editorSettingsSurface)
    {
    }

    Entity preset;
    Entity baseColor;
    Entity accentColor;
    Entity contrast;
    Entity scale;
    Entity fontSize;
    Entity codeFontSize;
    Entity updateContinuously;
    Button reset;
    struct EditorFields
    {
        std::size_t section = 0;
        Entity mode;
        Entity executableLabel;
        Entity executableRow;
        Entity executable;
        Entity argumentsLabel;
        Entity argumentsRow;
        Entity arguments;
        Entity pathRow;
        Entity path;
        Entity explanation;
        Button browse;
        bool custom = false;
    };
    std::array<EditorFields, 3> editors;
    std::vector<InstalledScriptEditor> installedEditors;
    bool detectedEditors = false;
    Button detectEditors;
    Entity detectionStatus;
    Entity automaticCompilation;
    std::array<Entity, viewportNumbers.size()> viewportNumberFields{};
    std::array<Entity, viewportColors.size()> viewportColorFields{};
    std::array<Entity, viewportToggles.size()> viewportToggleFields{};
    // The colour the picker edits: 0 the base, 1 the accent, then the viewport colours.
    std::optional<int> editedColor;

    void build(EditorUiKit& kit);
    void update(ToolsState& state, EditorUiKit& kit, core::Duration delta);
};

void EditorSettingsUi::build(EditorUiKit& kit)
{
    buildFrame(kit, editorPages);
    const float width = kit.textWidth(EditorUiKit::regularFont(), "Reset to Defaults", font) + font * 3.6f;
    reset = button(kit, top, Icon::Refresh, "Reset to Defaults", "button", width, line);
    placeRight(scene(), reset.entity, 0.0f, width);
    tooltip(reset.entity, "Resets the settings of the selected page");

    Section& theme = pageCard(kit, 0, "Theme");
    const FormRow presetRow = formRow(theme, "Preset");
    preset = choice(presetRow.editor);
    tooltip(presetRow.row, "Colours to start from: choosing one sets the base, the accent and the contrast");
    baseColor = swatch(kit, formRow(theme, "Base Color").editor, false);
    accentColor = swatch(kit, formRow(theme, "Accent Color").editor, false);
    const FormRow contrastRow = formRow(theme, "Contrast");
    contrast = numbers(contrastRow.editor, single, {.minValue = -0.5f, .maxValue = 1.0f, .dragSpeed = 0.005f, .decimals = 2}).front();
    tooltip(contrastRow.row, "How much darker the backgrounds around panels and fields are; below zero, lighter");

    Section& display = pageCard(kit, 1, "Display");
    const FormRow scaleRow = formRow(display, "Interface Scale");
    scale = choice(scaleRow.editor);
    tooltip(scaleRow.row, "The size of the whole interface; Auto follows the scale of the display");
    const scene::UiNumberField points{.minValue = 10.0f, .maxValue = 22.0f, .step = 1.0f, .dragSpeed = 0.05f, .decimals = 0, .format = "{} pt"};
    fontSize = numbers(formRow(display, "Font Size").editor, single, points).front();
    codeFontSize = numbers(formRow(display, "Code Font Size").editor, single, points).front();
    note(&display, "Code Font Size is that of the text editor and the output.", "dim");
    Section& performance = pageCard(kit, 1, "Performance");
    const FormRow updateRow = formRow(performance, "Update Continuously");
    updateContinuously = toggle(updateRow.editor);
    tooltip(updateRow.row, "Draws the editor at every frame, even when nothing changes: for measuring performance");
    note(&performance, "Off, the editor draws only while something moves or the user acts, and rests in between.", "dim", 2.0f);

    Section& handles = pageCard(kit, viewportPage, "Handles and Lines");
    for (std::size_t i = 0; i < 4; ++i)
    {
        const auto& field = viewportNumbers[i];
        viewportNumberFields[i] = numbers(formRow(handles, std::string(field.label)).editor, single,
                                          {.minValue = field.minimum,
                                           .maxValue = field.maximum,
                                           .dragSpeed = i == 0 || i == 3 ? 0.5f : 0.02f,
                                           .decimals = i == 0 || i == 3 ? 0 : 1,
                                           .format = "{} px"})
                                      .front();
    }
    note(&handles,
         "Sizes are viewport pixels. Handle Size also changes the area used to pick the handles.",
         "dim", 3.0f);
    Section& grid = pageCard(kit, viewportPage, "Grid");
    for (std::size_t i = 4; i < viewportNumbers.size(); ++i)
    {
        const auto& field = viewportNumbers[i];
        viewportNumberFields[i] = numbers(formRow(grid, std::string(field.label)).editor, single,
                                          {.minValue = field.minimum,
                                           .maxValue = field.maximum,
                                           .dragSpeed = 0.01f,
                                           .decimals = 2})
                                      .front();
    }
    note(&grid,
         "Grid Unit is in world units. The grid adapts its subdivisions to the zoom in 2D and 3D.",
         "dim", 3.0f);
    for (std::size_t i = 4; i < viewportColors.size(); ++i)
        viewportColorFields[i] =
            swatch(kit, formRow(grid, std::string(viewportColors[i].label)).editor, false);
    Section& axes = pageCard(kit, viewportPage, "Axes and Hover");
    for (std::size_t i = 0; i < 4; ++i)
        viewportColorFields[i] =
            swatch(kit, formRow(axes, std::string(viewportColors[i].label)).editor, false);
    Section& visibility = pageCard(kit, viewportPage, "Visibility");
    for (std::size_t i = 0; i < viewportToggles.size(); ++i)
        viewportToggleFields[i] =
            toggle(formRow(visibility, std::string(viewportToggles[i].label)).editor);
    note(&visibility,
         "Scene Icons is the master switch for cameras, lights and audio. Physics shapes otherwise "
         "show for the selection; All Collision Shapes shows every shape.",
         "dim", 5.0f);
    note(&visibility,
         "These preferences are saved for the editor and do not change the game's rendering or "
         "physics.",
         "dim", 3.0f);

    constexpr std::array<std::string_view, 3> categories{"C#", "C++", "Devex Files"};
    Section& detection = pageCard(kit, scriptsPage, "Installed Editors");
    detectEditors = action(kit, actions(&detection), Icon::Refresh, "Detect Editors");
    detectionStatus = note(&detection, "", "dim", 2.0f);
    for (std::size_t i = 0; i < editors.size(); ++i)
    {
        EditorFields& fields = editors[i];
        fields.section = sections.size();
        Section& section = pageCard(kit, scriptsPage, std::string(categories[i]));
        fields.mode = choice(formRow(section, "Open With").editor);
        fields.pathRow = actions(&section);
        scene().remove<scene::UiLayout>(fields.pathRow);
        scene().get<UiRect>(fields.pathRow).clipChildren = true;
        fields.path = text(fields.pathRow, whole(), "", "dim");
        fields.explanation = note(&section, "", "dim", 2.0f);
        fields.executableLabel = note(&section, "Executable", "dim");
        fields.executableRow = actions(&section);
        scene().remove<scene::UiLayout>(fields.executableRow);
        auto executableRect = whole();
        executableRect.offsetMax.x -= line + gap;
        const Entity executableClip = add(fields.executableRow, "Executable", executableRect);
        scene().get<UiRect>(executableClip).clipChildren = true;
        fields.executable = textField(executableClip, "Path to the editor executable");
        fields.browse = toolButton(kit, fields.executableRow, Icon::FolderOpen, rightButton(line, 0.0f));
        tooltip(fields.browse.entity, "Choose the external editor executable");
        fields.argumentsLabel = note(&section, "Arguments (optional)", "dim");
        fields.argumentsRow = actions(&section);
        scene().remove<scene::UiLayout>(fields.argumentsRow);
        scene().get<UiRect>(fields.argumentsRow).clipChildren = true;
        fields.arguments = textField(fields.argumentsRow);
        tooltip(fields.arguments, "{file}: full file path; {project}: Devex project folder; {project_file}: Game.csproj or CMakeLists.txt; {project_dir}: code folder. Default arguments load the project in recognized IDEs.");
    }
    Section& help = pageCard(kit, scriptsPage, "Opening Files");
    note(&help, "Double-click a script to open it with the chosen editor. Edit in Devex Script always uses the built-in editor.", "dim", 3.0f);
    note(&help, "Devex Files selects the text editor for .dvx files. Scenes and assets keep their visual editors.", "dim", 3.0f);

    Section& compilation = pageCard(kit, compilationPage, "Game Code");
    automaticCompilation = choice(formRow(compilation, "Compilation").editor);
    note(&compilation, "Automatic compiles saved C# and C++ changes, including saves made by external editors.", "dim", 3.0f);
    note(&compilation, "Manual keeps source changes pending until Project > Build Game Code (Ctrl+B). Saving in Devex Script also waits for Ctrl+B.", "dim", 4.0f);
    note(&compilation, "Missing or outdated code is still built when opening a project. Successful builds reload the code; failed builds keep the previous version.", "dim", 4.0f);
}

void EditorSettingsUi::update(ToolsState& state, EditorUiKit& kit, core::Duration delta)
{
    const ThemeColors& colors = themeColors();
    kit.refreshTheme(colors);
    ui::UiWorld& world = panel.world();
    // The window keeps the size of its text while one of its controls is held, the sizes of the text
    // among them: it grows with the rest of the editor, and is made again once let go.
    if (!world.held().isValid() && !world.isEditing())
    {
        setFont(state.theme.fontSize);
    }
    if (!built || builtFont != font)
    {
        clearAll();
        build(kit);
    }
    if (!detectedEditors)
    {
        installedEditors = detectScriptEditors();
        detectedEditors = true;
    }
    styleTooltips(colors);
    labelWidth = std::clamp(std::round((panel.size().x - font * 12.0f) * 0.42f), font * 7.0f, font * 12.0f);

    ThemeSettings theme = state.theme;
    ViewportSettings viewport = state.viewportSettings;
    std::vector<std::string> presets;
    for (const ThemePreset each : themePresets)
    {
        presets.emplace_back(displayName(each));
    }
    const auto chosenPreset = std::ranges::find(themePresets, theme.preset);
    setChoice(preset, std::move(presets), static_cast<std::int32_t>(chosenPreset - themePresets.begin()));
    setSwatch(baseColor, linearOf(theme.baseColor), false);
    setSwatch(accentColor, linearOf(theme.accentColor), false);
    setNumber(contrast, theme.contrast);
    std::vector<std::string> scales{std::format("Auto ({:.0f} %)", state.window.displayScale() * 100.0f)};
    for (const float each : interfaceScales)
    {
        scales.push_back(std::format("{:.0f} %", each * 100.0f));
    }
    const auto chosenScale = std::ranges::find(interfaceScales, theme.interfaceScale);
    setChoice(scale, std::move(scales),
              theme.interfaceScale <= 0.0f                 ? 0
              : chosenScale != interfaceScales.end() ? static_cast<std::int32_t>(chosenScale - interfaceScales.begin()) + 1
                                                           : -1);
    scene().get<scene::UiDropdown>(scale).placeholder = std::format("{:.0f} %", theme.interfaceScale * 100.0f);
    setNumber(fontSize, theme.fontSize);
    setNumber(codeFontSize, theme.codeFontSize);
    setToggle(updateContinuously, theme.updateContinuously);
    for (std::size_t i = 0; i < viewportNumbers.size(); ++i)
        setNumber(viewportNumberFields[i], viewport.*viewportNumbers[i].member);
    for (std::size_t i = 0; i < viewportColors.size(); ++i)
        setSwatch(viewportColorFields[i], linearOf(viewport.*viewportColors[i].member), false);
    for (std::size_t i = 0; i < viewportToggles.size(); ++i)
        setToggle(viewportToggleFields[i], viewport.*viewportToggles[i].member);
    for (std::size_t i = 0; i < editors.size(); ++i)
    {
        const EditorFields& fields = editors[i];
        const ScriptEditorChoice& editor = state.scripts.editors[i];
        std::vector<std::string> options{"Devex Script", "System Default"};
        std::int32_t selectedEditor = editor.editor == ScriptEditor::Custom ? static_cast<std::int32_t>(installedEditors.size() + 2) : static_cast<std::int32_t>(editor.editor);
        for (std::size_t index = 0; index < installedEditors.size(); ++index)
        {
            const auto& installed = installedEditors[index];
            options.push_back(installed.name);
            if (editor.editor == ScriptEditor::Custom && !fields.custom &&
                (editor.arguments.empty() || editor.arguments == "\"{file}\"" || editor.arguments == "{file}") &&
                sameEditorPath(core::pathFromUtf8(editor.executable), installed.executable))
            {
                selectedEditor = static_cast<std::int32_t>(index + 2);
            }
        }
        const bool custom = selectedEditor == static_cast<std::int32_t>(options.size());
        options.emplace_back("Custom External Editor...");
        setChoice(fields.mode, std::move(options), selectedEditor);
        setText(fields.executable, editor.executable);
        setText(fields.arguments, editor.arguments);
        for (const Entity row : {fields.executableLabel, fields.executableRow, fields.argumentsLabel, fields.argumentsRow})
        {
            showLine(sections[fields.section], row, custom);
        }
        showLine(sections[fields.section], fields.pathRow, editor.editor == ScriptEditor::Custom && !custom);
        // Keep the readable end of long paths; the tooltip retains the complete executable path.
        std::string path = core::toUtf8(core::pathFromUtf8(editor.executable));
        const float room = std::max(panel.size().x - font * 16.0f, font * 8.0f);
        while (path.size() > 4 && kit.textWidth(EditorUiKit::regularFont(), path, font) > room)
        {
            const auto slash = path.find('/', path.starts_with(".../") ? 4 : 0);
            if (slash == std::string::npos) { break; }
            path = ".../" + path.substr(slash + 1);
        }
        scene().get<scene::UiText>(fields.path).text = path;
        tooltip(fields.pathRow, editor.executable);
        scene().get<scene::UiText>(fields.explanation).text = editor.editor == ScriptEditor::Devex ? "Opens in the built-in Devex Script editor." :
            i == 0 ? "Opens scripts with Game.csproj so the IDE can resolve the Devex C# API." :
            i == 1 ? "Opens the code folder as a CMake project, together with the selected script." :
                     "Opens Devex text files with the selected editor.";
    }
    scene().get<scene::UiText>(detectionStatus).text = installedEditors.empty() ? "No supported IDE found. You can choose a custom executable below." :
        std::format("{} installed editors found. Choose one for each file type below.", installedEditors.size());
    setChoice(automaticCompilation, {"Automatic", "Manual (Ctrl+B)"}, state.scripts.automaticCompilation ? 0 : 1);
    if (editedColor && colorPopupOpen())
    {
        const math::Vec3 color =
            *editedColor == 0 ? theme.baseColor
            : *editedColor == 1
                ? theme.accentColor
                : viewport.*viewportColors[static_cast<std::size_t>(*editedColor - 2)].member;
        syncColorPopup(linearOf(color), false);
    }

    applyFilter();
    layoutCards();
    panel.update(kit, delta, UiPanel::zoomFor(font));
    answerForm();
    answerFrame();

    for (std::size_t i = 0; i < viewportNumbers.size(); ++i)
    {
        if (world.wasChanged(viewportNumberFields[i]))
            viewport.*viewportNumbers[i].member =
                scene().get<scene::UiNumberField>(viewportNumberFields[i]).value;
    }
    for (std::size_t i = 0; i < viewportToggles.size(); ++i)
    {
        if (world.wasChanged(viewportToggleFields[i]))
            viewport.*viewportToggles[i].member =
                scene().get<scene::UiToggle>(viewportToggleFields[i]).value;
    }

    ScriptSettings scripts = state.scripts;
    for (std::size_t i = 0; i < editors.size(); ++i)
    {
        EditorFields& fields = editors[i];
        ScriptEditorChoice& editor = scripts.editors[i];
        if (world.wasChanged(fields.mode))
        {
            const auto index = scene().get<scene::UiDropdown>(fields.mode).selected;
            fields.custom = index == static_cast<std::int32_t>(installedEditors.size() + 2);
            if (index >= 0 && index < 2)
            {
                editor.editor = static_cast<ScriptEditor>(index);
            }
            else if (index >= 2 && static_cast<std::size_t>(index - 2) < installedEditors.size())
            {
                editor = {ScriptEditor::Custom, core::toUtf8(installedEditors[static_cast<std::size_t>(index - 2)].executable)};
            }
            else if (fields.custom)
            {
                editor.editor = ScriptEditor::Custom;
            }
        }
        if (endedField == fields.executable)
        {
            editor.executable = scene().get<scene::UiText>(fields.executable).text;
        }
        if (endedField == fields.arguments)
        {
            editor.arguments = scene().get<scene::UiText>(fields.arguments).text;
        }
        if (world.wasClicked(fields.browse.entity))
        {
            const std::weak_ptr<DialogAnswers> answers = state.dialogAnswers;
            state.platform.showFileDialog(state.window,
                {.type = platform::FileDialogType::OpenFile,
                 .filters = {{"Executable", "exe"}, {"All files", "*"}},
                 .defaultLocation = core::pathFromUtf8(editor.executable)},
                [answers, i](std::optional<std::filesystem::path> chosen) {
                    if (const auto inbox = answers.lock(); inbox && chosen)
                    {
                        inbox->scriptEditor = std::pair{i, std::move(*chosen)};
                    }
                });
        }
    }
    if (world.wasClicked(detectEditors.entity))
    {
        installedEditors = detectScriptEditors();
    }
    if (world.wasChanged(automaticCompilation))
    {
        scripts.automaticCompilation = scene().get<scene::UiDropdown>(automaticCompilation).selected == 0;
    }

    if (world.wasChanged(preset))
    {
        const std::int32_t index = scene().get<scene::UiDropdown>(preset).selected;
        if (index >= 0 && static_cast<std::size_t>(index) < themePresets.size())
        {
            theme.applyPreset(themePresets[static_cast<std::size_t>(index)]);
        }
    }
    if (world.wasClicked(baseColor) || world.wasClicked(accentColor))
    {
        editedColor = world.wasClicked(baseColor) ? 0 : 1;
        openColorPopup(*editedColor == 0 ? baseColor : accentColor);
    }
    for (std::size_t i = 0; i < viewportColors.size(); ++i)
    {
        if (world.wasClicked(viewportColorFields[i]))
        {
            editedColor = static_cast<int>(i + 2);
            openColorPopup(viewportColorFields[i]);
        }
    }
    if (editedColor)
    {
        math::Vec3& edited =
            *editedColor == 0 ? theme.baseColor
            : *editedColor == 1
                ? theme.accentColor
                : viewport.*viewportColors[static_cast<std::size_t>(*editedColor - 2)].member;
        if (const std::optional<math::Vec4> chosen = answerColorPopup(linearOf(edited)))
        {
            edited = srgbOf(*chosen);
        }
        if (!colorPopupOpen())
        {
            editedColor.reset();
        }
    }
    if (world.wasChanged(contrast))
    {
        theme.contrast = scene().get<scene::UiNumberField>(contrast).value;
    }
    if (world.wasChanged(scale))
    {
        const std::int32_t index = scene().get<scene::UiDropdown>(scale).selected;
        theme.interfaceScale = index <= 0 ? 0.0f : interfaceScales[std::min(static_cast<std::size_t>(index - 1), interfaceScales.size() - 1)];
    }
    if (world.wasChanged(fontSize))
    {
        theme.fontSize = std::round(scene().get<scene::UiNumberField>(fontSize).value);
    }
    if (world.wasChanged(codeFontSize))
    {
        theme.codeFontSize = std::round(scene().get<scene::UiNumberField>(codeFontSize).value);
    }
    if (world.wasChanged(updateContinuously))
    {
        theme.updateContinuously = scene().get<scene::UiToggle>(updateContinuously).value;
    }
    if (world.wasClicked(reset.entity))
    {
        if (selected == viewportPage)
        {
            viewport = ViewportSettings{};
        }
        else if (selected == scriptsPage)
        {
            scripts.editors = ScriptSettings{}.editors;
        }
        else if (selected == compilationPage)
        {
            scripts.automaticCompilation = true;
        }
        else if (selected == 0)
        {
            const ThemeSettings defaults;
            theme.preset = defaults.preset;
            theme.baseColor = defaults.baseColor;
            theme.accentColor = defaults.accentColor;
            theme.contrast = defaults.contrast;
        }
        else if (selected == 1)
        {
            const ThemeSettings defaults;
            theme.interfaceScale = defaults.interfaceScale;
            theme.fontSize = defaults.fontSize;
            theme.codeFontSize = defaults.codeFontSize;
            theme.updateContinuously = defaults.updateContinuously;
        }
    }

    if (scripts != state.scripts)
    {
        state.scripts = std::move(scripts);
        state.scriptSettingsUnsaved = true;
    }

    if (theme != state.theme)
    {
        state.theme = theme;
        state.themeChanged = true;
        state.themeUnsaved = true;
    }
    if (viewport != state.viewportSettings)
    {
        state.viewportSettings = viewport;
        state.viewportSettingsUnsaved = true;
    }
    // Saved once a drag ends rather than at every step.
    if ((state.themeUnsaved || state.scriptSettingsUnsaved || state.viewportSettingsUnsaved) &&
        !world.held().isValid() && !world.isEditing())
    {
        state.themeUnsaved = false;
        state.scriptSettingsUnsaved = false;
        state.viewportSettingsUnsaved = false;
        saveUserSettings(state);
    }
    numberCursor(state, panel);
}

void drawSettingsWindow(ToolsState& state)
{
    DEVEX_PROFILE_SCOPE("Editor Settings");
    if (!state.showSettings)
    {
        return;
    }
    EditorUiKit& kit = editorUiKit(state);
    if (!state.editorSettingsUi)
    {
        state.editorSettingsUi = std::make_shared<EditorSettingsUi>();
    }
    if (beginFormWindow(state, settingsWindow, &state.showSettings, 64.0f, 39.0f))
    {
        state.editorSettingsUi->update(state, kit, core::Duration(state.input.delta()));
        if (state.editorSettingsUi->closeRequested)
            state.showSettings = false;
        endFormWindow(state);
    }
    if (!state.showSettings && (state.themeUnsaved || state.scriptSettingsUnsaved))
    {
        state.themeUnsaved = false;
        state.scriptSettingsUnsaved = false;
        saveUserSettings(state);
    }
}

// ---- Project Settings ----

ProjectSettingsUi::ProjectSettingsUi()
    : SettingsUi(projectSettingsSurface)
{
}

std::string ProjectSettingsUi::signatureOf() const
{
    std::string text = std::format("{}|{}|", font, project.sorting.layers.size());
    for (std::size_t index = 0; index < asset::physicsLayerCount; ++index)
    {
        text += index == 0 || !project.physics.layerNames[index].empty() ? '1' : '0';
    }
    text += inputMapSignature(project.input);
    text += std::format("globals:{}|", project.globals.size());
    return text;
}

void ProjectSettingsUi::buildCards(ToolsState& state, EditorUiKit& kit)
{
    static_cast<void>(state);
    const ThemeColors& colors = themeColors();
    const math::Vec4 accent = linearColor(colors.accent);
    const float tool = closeButtonSize();

    // Application.
    Section& application = pageCard(kit, 0, "Application");
    name = textField(formRow(application, "Name").editor, "Name of the game");
    const FormRow startupRow = formRow(application, "Startup Scene");
    startup = choice(startupRow.editor);
    tooltip(startupRow.row, "The scene the game opens with: without one, the first scene of the project");
    const FormRow iconRow = formRow(application, "Icon");
    icon = choice(iconRow.editor);
    scene().add<scene::UiDropTarget>(icon, scene::UiDropTarget{.accepts = {"asset:texture"}, .highlightColor = math::Vec4{accent.x, accent.y, accent.z, 0.35f}});
    tooltip(iconRow.row, "An image of the project, square and 256 pixels or more: the icon of the window and of the exported game");
    const FormRow desktopRow = formRow(application, "Linux Desktop Entry");
    desktopEntry = toggle(desktopRow.editor);
    tooltip(desktopRow.row, "On Linux, the exported game lists itself among the applications when it starts: the dock "
                            "shows its name and icon. The export also gives the entry to install by hand.");

    // Window.
    Section& window = pageCard(kit, 1, "Window");
    const FormRow sizeRow = formRow(window, "Size");
    const std::array<std::string_view, 2> sides{"w", "h"};
    size = numbers(sizeRow.editor, sides, {.minValue = 64.0f, .maxValue = 16384.0f, .step = 1.0f, .dragSpeed = 1.0f, .decimals = 0});
    tooltip(sizeRow.row, "The size of the window, in points: the system scales it on high-density displays");
    fullscreen = toggle(formRow(window, "Fullscreen").editor);
    const FormRow vsyncRow = formRow(window, "VSync");
    vsync = toggle(vsyncRow.editor);
    tooltip(vsyncRow.row, "Waits for the display refresh: no tearing, lower power");
    const FormRow rateRow = formRow(window, "Frame Rate Limit");
    frameRate = numbers(rateRow.editor, single, {.minValue = 0.0f, .maxValue = 1000.0f, .step = 1.0f, .dragSpeed = 0.5f, .decimals = 0}).front();
    tooltip(rateRow.row, "The most frames a second the game draws; 0 leaves it unlimited");
    note(&window, "Changes apply the next time the game starts.");

    // Physics.
    Section& physics = pageCard(kit, 2, "Physics");
    const std::array<std::string_view, 3> axes{"x", "y", "z"};
    gravity = numbers(formRow(physics, "Gravity").editor, axes, {.dragSpeed = 0.05f, .decimals = 2});
    note(&physics, "Changes apply the next time the game starts.");

    // Collision layers: their names, then which of the named ones touch, as a triangle.
    Section& names = pageCard(kit, 3, "Layer Names");
    note(&names, "Name the layers bodies use, then choose which ones touch.");
    for (std::size_t index = 0; index < asset::physicsLayerCount; ++index)
    {
        layerNames[index] = textField(formRow(names, std::format("Layer {}", index)).editor, index == 0 ? "Default" : "unused");
    }
    Section& collisions = pageCard(kit, 3, "Collisions");
    std::vector<std::uint32_t> named;
    for (std::uint32_t index = 0; index < asset::physicsLayerCount; ++index)
    {
        if (index == 0 || !project.physics.layerNames[index].empty())
        {
            named.push_back(index);
        }
    }
    const auto nameOf = [&](std::uint32_t layer) {
        return project.physics.layerNames[layer].empty() ? std::string("Default") : project.physics.layerNames[layer];
    };
    const float cellSize = std::round(font * 1.7f);
    const float box = std::round(font * 1.2f);
    const float labels = std::round(font * 8.0f);
    float longest = 0.0f;
    for (const std::uint32_t layer : named)
    {
        longest = std::max(longest, kit.textWidth(EditorUiKit::regularFont(), nameOf(layer), font));
    }
    const float header = std::round(longest * 0.71f + font * 1.4f);
    const std::size_t count = named.size();
    const Entity matrix = add(collisions.card, "Matrix",
                              UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 0.0f}, .offsetMin = {font * 0.35f, 0.0f},
                                     .offsetMax = {font * 0.35f + labels + static_cast<float>(count) * cellSize, header + static_cast<float>(count) * cellSize}});
    collisions.lines.push_back(Line{.entity = matrix});
    cells.clear();
    for (std::size_t column = 0; column < count; ++column)
    {
        // The names of the columns lean over them, as angled headers do.
        const std::string title = nameOf(named[count - 1 - column]);
        const float x = labels + static_cast<float>(column) * cellSize + cellSize * 0.5f;
        const float y = header - font * 0.5f;
        const float width = kit.textWidth(EditorUiKit::regularFont(), title, font) + 4.0f;
        text(matrix,
             UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 0.0f}, .offsetMin = {x, y - font * 0.7f}, .offsetMax = {x + width, y + font * 0.7f},
                    .pivot = {0.0f, 0.5f}, .rotation = -std::numbers::pi_v<float> * 0.25f},
             title, "label");
    }
    for (std::size_t row = 0; row < count; ++row)
    {
        const std::uint32_t layer = named[row];
        const float y = header + static_cast<float>(row) * cellSize;
        text(matrix, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 0.0f}, .offsetMin = {0.0f, y}, .offsetMax = {labels - font * 0.5f, y + cellSize}},
             nameOf(layer), "label", false, scene::TextAlign::Right);
        for (std::size_t column = 0; column + row < count; ++column)
        {
            const std::uint32_t other = named[count - 1 - column];
            const float x = labels + static_cast<float>(column) * cellSize + (cellSize - box) * 0.5f;
            const Entity toggle = add(matrix, "Collides", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 0.0f}, .offsetMin = {x, y + (cellSize - box) * 0.5f},
                                                                 .offsetMax = {x + box, y + (cellSize + box) * 0.5f}},
                                      "toggle");
            scene().add<scene::UiImage>(toggle);
            scene().add<scene::UiToggle>(toggle);
            scene().add<scene::UiButton>(toggle);
            tooltip(toggle, std::format("{} and {}", nameOf(layer), nameOf(other)));
            cells.push_back(Cell{.toggle = toggle, .layer = layer, .other = other});
        }
    }
    note(&collisions, "Changes apply the next time the game starts.");

    // Sorting layers, in the order they draw.
    Section& sorting = pageCard(kit, 4, "Sorting Layers");
    note(&sorting, "Sprites draw over the sprites of the layers above theirs. Blended surfaces and particles draw in Default.", "dim", 2.0f);
    sortingRows.clear();
    const float buttons = tool * 3.0f + gap * 4.0f;
    for (std::size_t index = 0; index < project.sorting.layers.size(); ++index)
    {
        const FormRow row = formRow(sorting, std::format("{}", index));
        SortingRow made;
        const bool fixed = project.sorting.layers[index] == "Default";
        const UiRect fill{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {0.0f, 0.0f}, .offsetMax = {-buttons, 0.0f}};
        made.name = fixed ? text(row.editor, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {font * 0.6f, 0.0f}, .offsetMax = {-buttons, 0.0f}},
                                 "Default", "text")
                          : field(row.editor, fill, "", "name");
        made.up = toolButton(kit, row.editor, Icon::ChevronUp, rightButton(tool, tool * 2.0f + gap * 2.0f));
        tooltip(made.up.entity, "Draw behind the layer above");
        made.down = toolButton(kit, row.editor, Icon::ChevronDown, rightButton(tool, tool + gap));
        tooltip(made.down.entity, "Draw in front of the layer below");
        made.remove = toolButton(kit, row.editor, Icon::Trash, rightButton(tool, 0.0f));
        tooltip(made.remove.entity, fixed ? "Default is always there" : "Remove the layer");
        enable(made.remove, !fixed);
        sortingRows.push_back(made);
    }
    addSortingLayer = action(kit, actions(&sorting), Icon::Plus, "Add Layer");
    note(&sorting, "Sprites name their layer: a sprite whose layer is renamed or removed draws in Default until it names a layer again.", "dim", 2.0f);

    // Audio.
    Section& master = pageCard(kit, 5, "Master");
    const FormRow masterRow = formRow(master, "Master Volume");
    masterVolume = numbers(masterRow.editor, single, {.minValue = 0.0f, .maxValue = 1.0f, .dragSpeed = 0.005f, .decimals = 2}).front();
    tooltip(masterRow.row, "The volume of every sound of the game");
    Section& groups = pageCard(kit, 5, "Groups");
    note(&groups, "AudioSource components and one-shot sounds play in a group, whose volume code can change.", "dim", 2.0f);
    const float volumeWidth = std::round(font * 6.0f);
    for (std::size_t index = 0; index < asset::audioGroupCount; ++index)
    {
        const FormRow row = formRow(groups, std::format("Group {}", index));
        groupNames[index] = field(row.editor, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {0.0f, 0.0f},
                                                      .offsetMax = {-volumeWidth - font * 0.4f, 0.0f}},
                                  "", "unused");
        groupVolumes[index] = numberBox(row.editor, "", colors.textDim, {.minValue = 0.0f, .maxValue = 1.0f, .dragSpeed = 0.005f, .decimals = 2});
        placeRight(scene(), groupVolumes[index], 0.0f, volumeWidth);
        tooltip(groupVolumes[index], "The volume of the group");
    }
    note(&groups, "Volumes apply at once, and each time the game starts.");

    buildInputMap(*this, kit, 6);

    buildGlobals(kit);

    Section& localization = pageCard(kit, 7, "Localization");
    const FormRow fallbackRow = formRow(localization, "Fallback Language");
    fallbackLanguage = choice(fallbackRow.editor);
    tooltip(fallbackRow.row, "What shows where the language of the player has no translation");
    const FormRow testRow = formRow(localization, "Test Language");
    testLanguage = choice(testRow.editor);
    tooltip(testRow.row, "The language the game plays in from the editor. Auto plays as the exported game does: the player's choice, "
                         "then the language of the system, then the fallback one");
    note(&localization, "The tables of translations are the .csv files of the project: Editor > Panels > Translations edits them. "
                        "The texts of the interfaces whose text is a key show its translation.",
         "dim", 3.0f);
}

void ProjectSettingsUi::sync(ToolsState& state, EditorUiKit& kit)
{
    static_cast<void>(kit);
    // Application.
    setText(name, project.name);
    std::vector<std::string> scenes{"(the first scene)"};
    startupScenes = {std::string{}};
    for (const asset::AssetInfo& info : state.database->assets(asset::AssetType::Scene))
    {
        if (const std::optional<asset::SourceFile> source = state.database->sourceOf(info.id))
        {
            scenes.push_back(source->path);
            startupScenes.push_back(source->path);
        }
    }
    const auto chosenScene = std::ranges::find(startupScenes, project.startupScene);
    setChoice(startup, std::move(scenes),
              chosenScene != startupScenes.end() ? static_cast<std::int32_t>(chosenScene - startupScenes.begin()) : -1);
    scene().get<scene::UiDropdown>(startup).placeholder = std::format("{} (missing)", project.startupScene);
    std::vector<std::string> textures{"(none)"};
    icons = {asset::AssetId{}};
    for (const asset::AssetInfo& info : state.database->assets(asset::AssetType::Texture))
    {
        textures.push_back(info.name);
        icons.push_back(info.id);
    }
    const auto chosenIcon = std::ranges::find(icons, project.window.icon);
    setChoice(icon, std::move(textures), chosenIcon != icons.end() ? static_cast<std::int32_t>(chosenIcon - icons.begin()) : -1);
    scene().get<scene::UiDropdown>(icon).placeholder = assetLabel(state, project.window.icon);
    setToggle(desktopEntry, project.window.desktopEntry);

    // Window and physics.
    setNumber(size[0], static_cast<float>(project.window.width));
    setNumber(size[1], static_cast<float>(project.window.height));
    setToggle(fullscreen, project.window.fullscreen);
    setToggle(vsync, project.window.vsync);
    setNumber(frameRate, static_cast<float>(project.window.maxFrameRate));
    scene::UiNumberField& rate = scene().get<scene::UiNumberField>(frameRate);
    rate.format = rate.value < 0.5f ? "Unlimited" : "{} fps";
    for (std::size_t axis = 0; axis < gravity.size(); ++axis)
    {
        setNumber(gravity[axis], project.physics.gravity[static_cast<math::Vec3::length_type>(axis)]);
    }
    for (std::size_t index = 0; index < layerNames.size(); ++index)
    {
        setText(layerNames[index], project.physics.layerNames[index]);
    }
    for (const Cell& cell : cells)
    {
        scene().get<scene::UiToggle>(cell.toggle).value = project.physics.collides(cell.layer, cell.other);
    }

    // Sorting and audio.
    const std::vector<std::string>& layers = project.sorting.layers;
    for (std::size_t index = 0; index < sortingRows.size() && index < layers.size(); ++index)
    {
        const SortingRow& row = sortingRows[index];
        if (scene().has<scene::UiInput>(row.name))
        {
            setText(row.name, layers[index]);
        }
        enable(row.up, index > 0);
        enable(row.down, index + 1 < layers.size());
    }
    setNumber(masterVolume, project.audio.masterVolume);

    // Localization: the languages of the tables, with those chosen even when no table has them.
    const std::vector<std::string> languages = projectLanguages(state);
    const auto listed = [&](std::vector<std::string>& codes, const std::string& chosen) {
        codes = languages;
        if (!chosen.empty() && std::ranges::find(codes, chosen) == codes.end())
        {
            codes.push_back(chosen);
            std::ranges::sort(codes);
        }
    };
    const auto named = [](const std::vector<std::string>& codes) {
        std::vector<std::string> names;
        for (const std::string& code : codes)
        {
            names.push_back(code.empty() ? std::string("Auto (the player's, or the system's)") : std::format("{} ({})", asset::languageName(code), code));
        }
        return names;
    };
    listed(fallbackCodes, project.localization.fallbackLanguage);
    const auto fallback = std::ranges::find(fallbackCodes, project.localization.fallbackLanguage);
    setChoice(fallbackLanguage, named(fallbackCodes), fallback != fallbackCodes.end() ? static_cast<std::int32_t>(fallback - fallbackCodes.begin()) : -1);
    listed(testCodes, project.localization.testLanguage);
    testCodes.insert(testCodes.begin(), std::string{});
    const auto test = std::ranges::find(testCodes, project.localization.testLanguage);
    setChoice(testLanguage, named(testCodes), test != testCodes.end() ? static_cast<std::int32_t>(test - testCodes.begin()) : 0);
    for (std::size_t index = 0; index < asset::audioGroupCount; ++index)
    {
        setText(groupNames[index], project.audio.groupNames[index]);
        setNumber(groupVolumes[index], project.audio.groupVolumes[index]);
    }
    syncInputMap(*this, state, kit);
}

void ProjectSettingsUi::answer(ToolsState& state, EditorUiKit& kit)
{
    const ui::UiWorld& world = panel.world();
    const auto number = [&](Entity entity) { return scene().get<scene::UiNumberField>(entity).value; };
    const auto typedText = [&](Entity entity) -> const std::string* {
        return endedField.isValid() && endedField == entity ? &scene().get<scene::UiText>(entity).text : nullptr;
    };
    const auto chosen = [&](Entity entity) -> std::optional<std::size_t> {
        if (!world.wasChanged(entity))
        {
            return std::nullopt;
        }
        const std::int32_t index = scene().get<scene::UiDropdown>(entity).selected;
        return index >= 0 ? std::optional(static_cast<std::size_t>(index)) : std::nullopt;
    };

    // Application.
    if (const std::string* const written = typedText(name))
    {
        project.name = *written;
    }
    if (const std::optional<std::size_t> index = chosen(startup); index && *index < startupScenes.size())
    {
        project.startupScene = startupScenes[*index];
    }
    if (const std::optional<std::size_t> index = chosen(icon); index && *index < icons.size())
    {
        project.window.icon = icons[*index];
    }
    if (world.wasDropped(icon) && world.dropped() != nullptr)
    {
        if (const std::optional<core::Uuid> uuid = core::Uuid::parse(world.dropped()->data))
        {
            project.window.icon = asset::AssetId{*uuid};
        }
    }
    if (world.wasChanged(desktopEntry))
    {
        project.window.desktopEntry = scene().get<scene::UiToggle>(desktopEntry).value;
    }

    // Window and physics.
    const auto whole = [](float value) { return static_cast<std::uint32_t>(std::max(std::lround(value), 0L)); };
    if (world.wasChanged(size[0]))
    {
        project.window.width = whole(number(size[0]));
    }
    if (world.wasChanged(size[1]))
    {
        project.window.height = whole(number(size[1]));
    }
    if (world.wasChanged(fullscreen))
    {
        project.window.fullscreen = scene().get<scene::UiToggle>(fullscreen).value;
    }
    if (world.wasChanged(vsync))
    {
        project.window.vsync = scene().get<scene::UiToggle>(vsync).value;
    }
    if (world.wasChanged(frameRate))
    {
        project.window.maxFrameRate = whole(number(frameRate));
    }
    for (std::size_t axis = 0; axis < gravity.size(); ++axis)
    {
        if (world.wasChanged(gravity[axis]))
        {
            project.physics.gravity[static_cast<math::Vec3::length_type>(axis)] = number(gravity[axis]);
        }
    }
    for (std::size_t index = 0; index < layerNames.size(); ++index)
    {
        if (const std::string* const written = typedText(layerNames[index]))
        {
            project.physics.layerNames[index] = *written;
        }
    }
    for (const Cell& cell : cells)
    {
        if (world.wasChanged(cell.toggle))
        {
            project.physics.setCollides(cell.layer, cell.other, scene().get<scene::UiToggle>(cell.toggle).value);
        }
    }

    // Sorting.
    std::vector<std::string>& layers = project.sorting.layers;
    std::optional<std::size_t> raised;
    std::optional<std::size_t> removed;
    for (std::size_t index = 0; index < sortingRows.size() && index < layers.size(); ++index)
    {
        const SortingRow& row = sortingRows[index];
        if (const std::string* const written = typedText(row.name))
        {
            layers[index] = *written;
        }
        if (world.wasClicked(row.up.entity) && index > 0)
        {
            raised = index;
        }
        else if (world.wasClicked(row.down.entity) && index + 1 < layers.size())
        {
            raised = index + 1;
        }
        else if (world.wasClicked(row.remove.entity) && layers[index] != "Default")
        {
            removed = index;
        }
    }
    if (raised)
    {
        std::swap(layers[*raised], layers[*raised - 1]);
    }
    if (removed)
    {
        layers.erase(layers.begin() + static_cast<std::ptrdiff_t>(*removed));
    }
    if (world.wasClicked(addSortingLayer.entity))
    {
        std::string layer = "Layer";
        for (int suffix = 2; std::ranges::find(layers, layer) != layers.end(); ++suffix)
        {
            layer = std::format("Layer {}", suffix);
        }
        layers.push_back(std::move(layer));
    }

    // Localization.
    if (const std::optional<std::size_t> index = chosen(fallbackLanguage); index && *index < fallbackCodes.size())
    {
        project.localization.fallbackLanguage = fallbackCodes[*index];
    }
    if (const std::optional<std::size_t> index = chosen(testLanguage); index && *index < testCodes.size())
    {
        project.localization.testLanguage = testCodes[*index];
    }

    // Audio.
    if (world.wasChanged(masterVolume))
    {
        project.audio.masterVolume = number(masterVolume);
    }
    for (std::size_t index = 0; index < asset::audioGroupCount; ++index)
    {
        if (const std::string* const written = typedText(groupNames[index]))
        {
            project.audio.groupNames[index] = *written;
        }
        if (world.wasChanged(groupVolumes[index]))
        {
            project.audio.groupVolumes[index] = number(groupVolumes[index]);
        }
    }
    answerInputMap(*this, state, kit);
    dirty = differs(project, state.database->project());
}

void ProjectSettingsUi::save(ToolsState& state)
{
    const ui::UiWorld& world = panel.world();
    if (!dirty || world.held().isValid() || world.isEditing())
    {
        return;
    }
    if (!asset::validateGlobals(project.globals))
        return;
    dirty = false;
    const asset::Project& saved = state.database->project();
    if (project.name.empty())
    {
        project.name = saved.name;
    }
    // Sorting layers have names, each its own, and Default among them.
    std::vector<std::string> kept;
    for (std::string& layer : project.sorting.layers)
    {
        if (!layer.empty() && std::ranges::find(kept, layer) == kept.end())
        {
            kept.push_back(std::move(layer));
        }
    }
    if (std::ranges::find(kept, "Default") == kept.end())
    {
        kept.insert(kept.begin(), "Default");
    }
    project.sorting.layers = std::move(kept);
    // What the window does not edit stays as it is.
    asset::Project written = saved;
    written.name = project.name;
    written.startupScene = project.startupScene;
    written.physics = project.physics;
    written.window = project.window;
    written.audio = project.audio;
    written.input = project.input;
    written.sorting = project.sorting;
    written.globals = project.globals;
    if (core::Result<void> updated = state.database->updateProject(written); !updated)
    {
        DEVEX_LOG_ERROR("Cannot save the project: {}", updated.error());
    }
}

void ProjectSettingsUi::update(ToolsState& state, EditorUiKit& kit, core::Duration delta)
{
    const ThemeColors& colors = themeColors();
    kit.refreshTheme(colors);
    setFont(state.theme.fontSize);
    if (!built || builtFont != font)
    {
        clearAll();
        buildFrame(kit, projectPages);
        buildEventDialog(*this, kit);
        // Textures come from FileSystem, to be the icon of the game.
        panel.setDragIn([](const EditorDrag& payload) -> std::optional<std::pair<std::string, std::string>> {
            if (payload.is(assetPayload, sizeof(AssetPayload)))
            {
                AssetPayload asset;
                std::memcpy(&asset, payload.payload.data(), sizeof(asset));
                return std::pair{std::format("asset:{}", asset::toString(asset.type)), uuidFromBytes(asset.uuid).toString()};
            }
            return std::nullopt;
        });
    }
    styleTooltips(colors);

    // The copy follows the project while nothing waits to be written, and starts again with another one.
    const asset::Project& saved = state.database->project();
    if (!dirty || project.file != saved.file)
    {
        project = saved;
        dirty = false;
    }
    if (std::string wanted = signatureOf(); wanted != signature)
    {
        clearForm();
        buildCards(state, kit);
        signature = std::move(wanted);
    }
    labelWidth = std::clamp(std::round((panel.size().x - font * 12.0f) * 0.34f), font * 7.0f, font * 12.0f);
    sync(state, kit);
    syncGlobals();
    applyFilter();
    filterInputMap(*this);
    measureTitles(kit);
    layoutCards();
    panel.update(kit, delta, UiPanel::zoomFor(font));
    answerForm();
    answerFrame();
    answer(state, kit);
    answerGlobals();
    dirty = dirty || project.globals != state.database->project().globals;
    save(state);
    scene().get<scene::UiText>(saveStatus).text =
        asset::validateGlobals(project.globals) ? "Changes are saved automatically."
                                                : "Fix the Globals warning to save your changes.";
    numberCursor(state, panel);
}

void drawProjectSettingsWindow(ToolsState& state)
{
    DEVEX_PROFILE_SCOPE("Project Settings");
    if (!state.showProjectSettings || state.database == nullptr)
    {
        return;
    }
    EditorUiKit& kit = editorUiKit(state);
    if (!state.projectSettingsUi)
    {
        state.projectSettingsUi = std::make_shared<ProjectSettingsUi>();
    }
    if (beginFormWindow(state, "Project Settings", &state.showProjectSettings, 72.0f, 43.0f))
    {
        state.projectSettingsUi->update(state, kit, core::Duration(state.input.delta()));
        if (state.projectSettingsUi->closeRequested)
            state.showProjectSettings = false;
        endFormWindow(state);
    }
}

void renderFormWindows(ToolsState& state, render::RenderWorld& world)
{
    DEVEX_PROFILE_SCOPE("Window images");
    if (!state.uiKit)
    {
        return;
    }
    const math::Vec4 background = linearColor(themeColors().panel);
    if (state.editorSettingsUi)
    {
        state.editorSettingsUi->panel.render(*state.uiKit, world, background);
    }
    if (state.projectSettingsUi)
    {
        state.projectSettingsUi->panel.render(*state.uiKit, world, background);
    }
    renderExportWindow(state, world);
    renderDebuggingWindow(state, world);
    renderEditorDialogs(state, world);
}

} // namespace devex::tools::detail
