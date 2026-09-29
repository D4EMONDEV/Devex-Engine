#pragma once

// The Inspector, made with the interface of the engine as Godot's is made with its controls. It shows
// the entities selected, in InspectorUi.cpp, or a page for what else is chosen: an asset of the
// FileSystem, a code file, or an element of the Animator panel, each page in the file of its kind.
// Pages are made of the same pieces as the entities: cards folded from their header, and rows of a
// label and its controls.
#include "EditorUi.hpp"
#include "ToolsState.hpp"

#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/UiComponents.hpp>
#include <devex/ui/Theme.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace devex::tools::detail {

// What a value shows when the selected entities do not share it.
inline constexpr std::string_view mixedDash = "—";

enum class ControlKind : std::uint8_t
{
    Toggle,
    Numbers,
    Color,
    Text,
    Choice,
    ReadOnly,
    ListHeader,
};

// A property of a component: its row, and what shows and changes its value.
struct PropertyRow
{
    const scene::ComponentType* type = nullptr;
    const reflection::FieldInfo* field = nullptr;
    // The element of a list the row edits; the whole field otherwise.
    std::optional<std::size_t> element;
    ControlKind kind = ControlKind::ReadOnly;
    scene::Entity row;
    scene::Entity labelBox;
    scene::Entity label;
    scene::Entity mark;
    scene::Entity editor;
    // The numbers of a value: one, the axes of a vector, or the angles of a rotation.
    std::array<scene::Entity, 4> numbers{};
    std::size_t count = 0;
    // The toggle, the field, the list of choices, the colour, or the text that is only read.
    scene::Entity control;
    // The word beside a toggle, or the hexadecimal of a colour, and the opacity under a colour.
    scene::Entity detail;
    scene::Entity opacity;
    // Adds an element to a list, or removes this one.
    PanelButton button;
    // What the choices stand for, in the order of the options.
    std::vector<asset::AssetId> assets;
    std::vector<scene::EntityRef> references;
    std::vector<std::uint32_t> indices;
    std::vector<std::string> names;
    // An edit under way: the value of the field of each entity when it began.
    bool editing = false;
    std::vector<std::pair<core::Uuid, serialization::TextValue>> starts;
    // Whether the field was typed into at the last update.
    bool typing = false;
    // Whether the style of the element writes the field, which then cannot be changed here.
    bool themed = false;
    // The angles a rotation shows, kept while it is edited so that they do not jump.
    math::Vec3 degrees{0.0f};
    // The value of the prefab, when the entity's differs from it.
    std::optional<serialization::TextValue> prefabValue;
};

struct TilePainterUi;

// A line of a card under its header: a property, the header of a group, or what a card adds.
struct Line
{
    scene::Entity entity;
    // The group of the card it belongs to; -1 before the first group.
    int group = -1;
    bool heading = false;
    // Whether it has something to show, for the lines that come and go.
    bool shown = true;
};

// A component, or a part of a page, as a card.
struct Section
{
    const scene::ComponentType* type = nullptr;
    std::string name;
    scene::Entity card;
    scene::Entity header;
    scene::Entity icon;
    scene::Entity title;
    scene::Entity added;
    PanelButton menu;
    // A component whose code is not loaded: only its header shows.
    bool locked = false;
    bool fromPrefab = false;
    std::vector<Line> lines;
    std::vector<std::string> groups;
    // What some components show under their properties: the style of an element, the controls of
    // an emitter, the navigation mesh of a surface, and the painting of a tilemap.
    std::optional<std::size_t> styleLine;
    scene::Entity styleIcon;
    scene::Entity styleText;
    PanelButton styleOpen;
    std::optional<std::filesystem::path> themeFile;
    PanelButton restart;
    PanelButton stop;
    scene::Entity particles;
    std::optional<std::size_t> navWarningLine;
    std::optional<std::size_t> navStatusLine;
    scene::Entity navInfo;
    scene::Entity navWarning;
    scene::Entity navStatus;
    PanelButton bake;
    PanelButton clear;
    std::shared_ptr<TilePainterUi> painter;
};

// A row of a page: a label at the left, and the editor that holds its controls at the right.
struct FormRow
{
    scene::Entity row;
    scene::Entity label;
    // The mark of a value that is not applied yet, or that differs from what it comes from.
    scene::Entity mark;
    scene::Entity editor;
};

// Squares that each show a sprite, which wrap with the width of the panel: frames, tiles.
struct SpriteGrid
{
    scene::Entity grid;
    float size = 40.0f;
    // Each square, its image, and the square that takes what is dropped at the end.
    std::vector<scene::Entity> cells;
    std::vector<scene::Entity> images;
    scene::Entity drop;
};

struct InspectorUi;

// The import options of the asset a page shows, changed and kept aside until Reimport applies them
// all at once, as Godot's Import dock does, or Revert forgets them.
class ImportSettings
{
public:
    // Forgets what waits when the page shows another asset.
    void show(asset::AssetId asset);
    // The value waiting, else the one of the .dvxmeta.
    [[nodiscard]] std::optional<serialization::TextValue> value(const ToolsState& state, std::string_view key) const;
    [[nodiscard]] bool boolean(const ToolsState& state, std::string_view key, bool fallback) const;
    [[nodiscard]] double number(const ToolsState& state, std::string_view key, double fallback) const;
    [[nodiscard]] std::string text(const ToolsState& state, std::string_view key, std::string_view fallback) const;
    [[nodiscard]] math::Vec4 vector(const ToolsState& state, std::string_view key, math::Vec4 fallback) const;
    // Waits for Reimport, unless it is the value of the file already.
    void set(const ToolsState& state, std::string_view key, serialization::TextValue value);
    [[nodiscard]] bool waits(std::string_view key) const;
    [[nodiscard]] std::size_t waiting() const noexcept;
    // Writes what waits and imports the file again, once; imports it again when nothing waits.
    void apply(ToolsState& state);
    void revert() noexcept;

private:
    asset::AssetId m_asset;
    std::vector<serialization::TextProperty> m_waiting;
};

// A vector of an import option: vec2(...) to vec4(...).
[[nodiscard]] serialization::TextValue vectorValue(const float* values, int count);

// A page of the inspector: what it shows of an asset, a code file or an element, made of entities
// under the content of the panel.
class InspectorPage
{
public:
    virtual ~InspectorPage() = default;

    // What the page shows: the panel makes its entities again when this changes.
    [[nodiscard]] virtual std::string signature(ToolsState& state) = 0;
    virtual void build(InspectorUi& ui, ToolsState& state, EditorUiKit& kit) = 0;
    // Before the interface answers: the entities take the values they show.
    virtual void sync(InspectorUi& ui, ToolsState& state, EditorUiKit& kit) = 0;
    // After: what was clicked, typed, dragged and dropped.
    virtual void answer(InspectorUi& ui, ToolsState& state, EditorUiKit& kit) = 0;
};

[[nodiscard]] std::unique_ptr<InspectorPage> makeEmptyPage();
// Any asset without a page of its own: its name, its file, and a way to import it again.
[[nodiscard]] std::unique_ptr<InspectorPage> makeAssetPage();
[[nodiscard]] std::unique_ptr<InspectorPage> makeCodePage();
[[nodiscard]] std::unique_ptr<InspectorPage> makeTexturePage();
[[nodiscard]] std::unique_ptr<InspectorPage> makeModelPage();
[[nodiscard]] std::unique_ptr<InspectorPage> makeAudioPage();
[[nodiscard]] std::unique_ptr<InspectorPage> makeCurvePage();
[[nodiscard]] std::unique_ptr<InspectorPage> makeSpriteFramesPage();
[[nodiscard]] std::unique_ptr<InspectorPage> makeTilesetPage();
[[nodiscard]] std::unique_ptr<InspectorPage> makeAnimatorPage();
[[nodiscard]] std::unique_ptr<InspectorPage> makeAnimatorElementPage();

// The painting of the tilemap of the inspected entity, under its card: in TilePainter.cpp.
void addTilePainter(InspectorUi& ui, EditorUiKit& kit, Section& section);
void syncTilePainter(InspectorUi& ui, ToolsState& state, EditorUiKit& kit, scene::Scene& edited, scene::Entity entity,
                     Section& section);
void answerTilePainter(InspectorUi& ui, ToolsState& state, Section& section);

// The end of the card of the import options: what waits, and the buttons that apply it or forget it.
struct ImportFooter
{
    scene::Entity note;
    PanelButton reimport;
    PanelButton revert;
};
[[nodiscard]] ImportFooter importFooter(InspectorUi& ui, EditorUiKit& kit, Section& section);
void syncImportFooter(InspectorUi& ui, EditorUiKit& kit, const ImportFooter& footer, const ImportSettings& settings);
void answerImportFooter(InspectorUi& ui, ToolsState& state, const ImportFooter& footer, ImportSettings& settings);

// Where a sprite dropped from FileSystem goes: a sprite, or all the sprites of a texture.
[[nodiscard]] std::vector<std::string> spriteDrops();
[[nodiscard]] std::vector<asset::AssetId> droppedSprites(const ToolsState& state, const ui::Drop& dropped);

// The panel and the entities the code reads and changes.
struct InspectorUi : PanelBuilder
{
    InspectorUi();

    bool built = false;
    float line = 28.0f;
    float pad = 6.0f;
    float gap = 2.0f;
    float headerHeight = 30.0f;
    float labelWidth = 120.0f;

    scene::Entity scroll;
    scene::Entity content;
    std::string signature;
    // The registry the rows were made from: once game code reloads, their types are gone.
    std::uint64_t generation = 0;
    std::vector<core::Uuid> shownEntities;

    // Above the components: the name and the UUID of an entity, or how many are selected.
    scene::Entity nameIcon;
    scene::Entity nameField;
    scene::Entity nameMark;
    bool naming = false;
    scene::Entity uuidText;
    scene::Entity countText;
    // The name the entity has in its prefab, when it differs.
    std::optional<std::string> prefabName;
    // The prefab the entity comes from, when it does.
    scene::Entity prefabIcon;
    scene::Entity prefabLead;
    scene::Entity prefabTitle;
    scene::Entity prefabTail;
    PanelButton openPrefab;
    PanelButton revertPrefab;
    PanelButton makeLocal;

    std::vector<Section> sections;
    std::vector<PropertyRow> rows;
    PanelButton addComponent;
    // The cards and the groups folded, by name.
    std::unordered_set<std::string> folded;

    // The menu of a component, the menu that reverts a value to its prefab's, and the colour picker.
    scene::Entity componentMenu;
    PanelButton removeComponent;
    std::optional<std::size_t> menuSection;
    scene::Entity revertMenu;
    PanelButton revert;
    std::optional<std::size_t> revertRow;
    bool revertName = false;
    scene::Entity colorPopup;
    scene::Entity picker;
    scene::Entity hexField;
    scene::Entity channelRow;
    std::array<scene::Entity, 4> channels{};
    std::optional<std::size_t> colorRow;
    bool hexTyping = false;

    // The page shown in place of the entities, what it is for, and what it made outside the content,
    // such as its menus.
    std::unique_ptr<InspectorPage> page;
    std::string pageKind;
    std::string pageTarget;
    std::vector<FormRow> formRows;
    std::vector<scene::Entity> pageEntities;
    std::vector<SpriteGrid*> grids;

    void build(ToolsState& state, EditorUiKit& kit);
    [[nodiscard]] std::string signatureOf(const scene::Scene& edited, std::span<const scene::Entity> inspected) const;
    // Forgets what the panel shows, the edits under way ending as they stand.
    void clear(ToolsState& state, const scene::Scene& edited);
    void rebuild(ToolsState& state, EditorUiKit& kit, scene::Scene& edited, std::span<const scene::Entity> inspected);
    void buildTop(EditorUiKit& kit, const scene::Scene& edited, std::span<const scene::Entity> inspected);
    Section& addSection(EditorUiKit& kit, std::string name, const EntityIcon& look, const scene::ComponentType* type);
    void addProperty(EditorUiKit& kit, Section& section, const scene::ComponentType& type, const reflection::FieldInfo& field,
                     std::optional<std::size_t> element, int group, bool shared);
    void addExtras(EditorUiKit& kit, Section& section, int group);
    scene::Entity numberBox(scene::Entity parent, std::string_view letter, ImVec4 letterColor, const scene::UiNumberField& settings);
    PanelButton toolButton(EditorUiKit& kit, scene::Entity parent, Icon glyph, scene::UiRect rect);
    void fitText(EditorUiKit& kit, scene::Entity entity, std::string value, bool bold = false);

    void sync(ToolsState& state, EditorUiKit& kit, scene::Scene& edited, std::span<const scene::Entity> inspected);
    void syncRow(const ToolsState& state, const scene::Scene& edited, std::span<const scene::Entity> inspected, PropertyRow& row,
                 const ui::ElementStyle& style, const scene::Scene* base, scene::Entity prefabEntity, bool editing);
    void fillChoices(const ToolsState& state, const scene::Scene& edited, PropertyRow& row, const void* address, bool mixed);
    void syncExtras(ToolsState& state, EditorUiKit& kit, scene::Scene& edited, scene::Entity active, Section& section,
                    const ui::ElementStyle& style);
    void syncColorPopup(const scene::Scene& edited, std::span<const scene::Entity> inspected);
    // Which lines of the cards show, and how tall each card is; the form rows and the grids follow
    // the width of the panel.
    void layoutCards();

    void answer(ToolsState& state, EditorUiKit& kit, scene::Scene& edited, std::span<const scene::Entity> inspected);
    void answerRow(ToolsState& state, scene::Scene& edited, std::span<const scene::Entity> inspected, std::size_t index);
    void answerColorPopup(scene::Scene& edited, std::span<const scene::Entity> inspected);
    void answerMenus(ToolsState& state, EditorUiKit& kit, scene::Scene& edited, std::span<const scene::Entity> inspected);
    void beginEdit(const scene::Scene& edited, std::span<const scene::Entity> inspected, PropertyRow& row);
    void commit(ToolsState& state, const scene::Scene& edited, PropertyRow& row);
    [[nodiscard]] bool isActive(std::size_t index);

    void update(ToolsState& state, EditorUiKit& kit, scene::Scene& edited, core::Duration delta);
    void updatePage(ToolsState& state, EditorUiKit& kit, const scene::Scene& edited, std::string_view kind, core::Duration delta);

    // Writes into the value every inspected entity has, knowing which one is the active entity.
    template <typename Write>
    void each(const scene::Scene& edited, std::span<const scene::Entity> inspected, const PropertyRow& row, const Write& write);

    // The pieces of the pages.
    struct Heading
    {
        scene::Entity icon;
        scene::Entity title;
        scene::Entity subtitle;
    };
    // The icon and the name of what the page shows, and its file under them.
    Heading heading(EditorUiKit& kit, IconText glyph, ImVec4 color, std::string title, std::string subtitle);
    // A card folded from its header, which the rows of the page go into.
    Section& card(EditorUiKit& kit, std::string name, std::optional<EntityIcon> look = std::nullopt);
    FormRow formRow(Section& section, std::string label, float height = 0.0f);
    scene::Entity toggle(scene::Entity editor);
    // Turns a toggle of a page on or off, and the word beside it.
    void setToggle(scene::Entity toggle, bool value);
    scene::Entity choice(scene::Entity editor, std::vector<std::string> options = {});
    // Numbers side by side in an editor, each named by its letter when there are several.
    std::vector<scene::Entity> numbers(scene::Entity editor, std::span<const std::string_view> letters,
                                       const scene::UiNumberField& settings);
    scene::Entity textField(scene::Entity editor, std::string placeholder = {});
    // A text on a line of its own, in a card or between the cards; taller lines wrap it.
    scene::Entity note(Section* section, std::string value, std::string_view style = "dim", float lines = 1.0f);
    // A line where buttons follow each other from the left.
    scene::Entity actions(Section* section);
    PanelButton action(EditorUiKit& kit, scene::Entity actions, std::optional<Icon> glyph, std::string_view label,
                       std::string_view style = "button");
    // The line of a card that holds an entity, to show it or not.
    void showLine(Section& section, scene::Entity entity, bool shown);
    SpriteGrid& spriteGrid(EditorUiKit& kit, Section& section, std::unique_ptr<SpriteGrid>& grid, std::size_t count, float size,
                           bool dropBox);
    // Shows as many squares in a grid as asked, making those it lacks; `drops` makes them take sprites.
    void gridCells(SpriteGrid& grid, std::size_t count, bool drops);
    // A menu of the page, gone with it.
    scene::Entity pageMenu(const char* name, float width);
    // Shows the sprite, or nothing, in an image of a grid.
    void showSprite(scene::Entity image, asset::AssetId sprite);
};

} // namespace devex::tools::detail
