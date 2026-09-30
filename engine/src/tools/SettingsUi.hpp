#pragma once

// The windows of the editor that float over the others, made of the pieces of forms: Editor Settings
// and Project Settings as Godot shows them, with their sections at the left and a filter above, the
// Export window, and the C# Debugging window.
#include "FormUi.hpp"
#include "ToolsState.hpp"

#include <devex/asset/Project.hpp>
#include <devex/platform/InputSource.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace devex::tools::detail {

// The images the windows are drawn into, among the interface surfaces of the editor.
inline constexpr std::uint32_t editorSettingsSurface = 7;
inline constexpr std::uint32_t projectSettingsSurface = 8;
inline constexpr std::uint32_t exportSurface = 9;
inline constexpr std::uint32_t debuggingSurface = 10;
inline constexpr std::uint32_t dialogSurface = 11;

// Opens a window that floats over the others, in the middle of the editor when it appears, its
// content filling it without a margin: false while it is folded away, and then nothing is to draw.
// Opens a window of settings as a modal, `width` by `height` lines of text, with its title and a
// cross that clears `open`; false while it is closed or under another modal. endFormWindow follows
// a true only.
[[nodiscard]] bool beginFormWindow(ToolsState& state, const char* title, bool* open, float width, float height);
void endFormWindow(ToolsState& state);
// The fonts, icons and theme the panels share, made the first time a panel asks.
EditorUiKit& editorUiKit(ToolsState& state);
// Places an element at a distance from the right of its parent, as wide as given and as tall as it.
void placeRight(scene::Scene& scene, scene::Entity entity, float right, float width);
// A square button in the middle of the height of its parent, at a distance from its right.
[[nodiscard]] scene::UiRect rightButton(float size, float right);
// What the pointer shows over the numbers of a panel: that they are dragged sideways.
void numberCursor(ToolsState& state, UiPanel& panel);
// A number without a letter, as the rows of settings show one.
inline constexpr std::array<std::string_view, 1> singleNumber{""};

// A window of settings: its sections at the left, a filter above them that finds a setting by its name
// in all of them, and the cards of the section chosen at the right.
struct SettingsUi : FormUi
{
    explicit SettingsUi(std::uint32_t surface);

    struct Page
    {
        std::string name;
        PanelButton button;
    };

    bool built = false;
    float builtFont = 0.0f;
    // What the cards were made for: they are made again when it changes.
    std::string signature;
    std::vector<Page> pages;
    std::size_t selected = 0;
    scene::Entity top;
    scene::Entity filter;
    scene::Entity list;

    // Forgets everything, to make the window again at another size of text.
    void clearAll();
    // The filter, the list of sections and the room of the cards, made once for a size of text.
    void buildFrame(EditorUiKit& kit, std::span<const std::string_view> names);
    // A card of a section.
    Section& pageCard(EditorUiKit& kit, std::size_t page, std::string name);
    // Which cards and lines show: those of the section chosen, or those whose name holds what the
    // filter holds, in every section.
    void applyFilter();
    // A section chosen from the list, which empties the filter.
    void answerFrame();
};

// A binding of an input action as the event window edits it, and where it goes.
struct EventTarget
{
    std::size_t action = 0;
    // The binding changed, or the end of the list for one to add.
    std::size_t binding = 0;
};

// The Input Map section of the project settings: the contexts, and a card per action with its
// bindings, each chosen in the event window.
struct InputMapUi
{
    struct Context
    {
        scene::Entity name;
        scene::Entity active;
        PanelButton remove;
    };
    struct Binding
    {
        // The label of the row, which names the device.
        scene::Entity device;
        PanelButton input;
        scene::Entity direction;
        PanelButton remove;
    };
    struct Action
    {
        std::size_t card = 0;
        scene::Entity name;
        scene::Entity warning;
        scene::Entity kind;
        scene::Entity context;
        scene::Entity deadZone;
        std::vector<Binding> bindings;
        PanelButton add;
        PanelButton remove;
    };

    std::vector<Context> contexts;
    PanelButton addContext;
    std::vector<Action> actions;
    PanelButton addAction;

    // The event window: what it listens for and shows, the list it chooses from, and the direction.
    scene::Entity dialog;
    scene::Entity dialogTitle;
    scene::Entity listen;
    scene::Entity listenText;
    scene::Entity search;
    scene::Entity sourceScroll;
    scene::Entity sourceList;
    std::vector<std::pair<platform::InputSource, PanelButton>> sources;
    std::vector<scene::Entity> groupTitles;
    scene::Entity directionRow;
    scene::Entity direction;
    PanelButton confirm;
    PanelButton cancel;
    std::optional<EventTarget> target;
    std::optional<platform::InputSource> chosen;
    std::vector<asset::InputDirection> directions;
    bool listening = false;
    std::string lastSearch;
};

// The project settings, edited on a copy of the project written once an edit ends.
struct ProjectSettingsUi : SettingsUi
{
    ProjectSettingsUi();

    asset::Project project;
    // The copy differs from the project and waits to be written.
    bool dirty = false;

    // Application.
    scene::Entity name;
    scene::Entity startup;
    std::vector<std::string> startupScenes;
    scene::Entity icon;
    std::vector<asset::AssetId> icons;
    // Window.
    std::vector<scene::Entity> size;
    scene::Entity fullscreen;
    scene::Entity vsync;
    scene::Entity frameRate;
    // Physics.
    std::vector<scene::Entity> gravity;
    std::array<scene::Entity, asset::physicsLayerCount> layerNames{};
    struct Cell
    {
        scene::Entity toggle;
        std::uint32_t layer = 0;
        std::uint32_t other = 0;
    };
    std::vector<Cell> cells;
    // Sorting.
    struct SortingRow
    {
        scene::Entity name;
        PanelButton up;
        PanelButton down;
        PanelButton remove;
    };
    std::vector<SortingRow> sortingRows;
    PanelButton addSortingLayer;
    // Audio.
    scene::Entity masterVolume;
    std::array<scene::Entity, asset::audioGroupCount> groupNames{};
    std::array<scene::Entity, asset::audioGroupCount> groupVolumes{};
    InputMapUi input;

    void update(ToolsState& state, EditorUiKit& kit, core::Duration delta);
    [[nodiscard]] std::string signatureOf() const;
    void buildCards(ToolsState& state, EditorUiKit& kit);
    void sync(ToolsState& state, EditorUiKit& kit);
    void answer(ToolsState& state, EditorUiKit& kit);
    // Writes the copy once nothing is being dragged or typed, with names that make sense.
    void save(ToolsState& state);
};

// The Input Map section, in InputSettings.cpp.
void buildInputMap(ProjectSettingsUi& ui, EditorUiKit& kit, std::size_t page);
void buildEventDialog(ProjectSettingsUi& ui, EditorUiKit& kit);
void syncInputMap(ProjectSettingsUi& ui, ToolsState& state, EditorUiKit& kit);
void answerInputMap(ProjectSettingsUi& ui, ToolsState& state, EditorUiKit& kit);
// What the Input Map adds to the signature of the cards.
[[nodiscard]] std::string inputMapSignature(const asset::InputSettings& input);

} // namespace devex::tools::detail
