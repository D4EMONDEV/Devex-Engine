#pragma once

// The Inspector, made with the interface of the engine as Godot's is made with its controls. It shows
// the entities selected, in InspectorUi.cpp, or a page for what else is chosen: an asset of the
// FileSystem, a code file, or an element of the Animator panel, each page in the file of its kind.
// Pages are made of the same pieces as the entities, those of the forms of the editor: cards folded
// from their header, and rows of a label and its controls.
#include "FormUi.hpp"
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
    // The layers of a mask, as numbered toggles.
    Bits,
};

// How many layers of a mask the inspector shows; the others are kept as they are.
inline constexpr std::size_t shownBits = 8;

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
    // The layers of a mask.
    std::array<PanelButton, shownBits> bits{};
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
// A table of translations: its languages and what each misses, and the panel that edits it.
[[nodiscard]] std::unique_ptr<InspectorPage> makeTranslationPage();

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
struct InspectorUi : FormUi
{
    InspectorUi();

    bool built = false;
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

    std::vector<PropertyRow> rows;
    PanelButton addComponent;

    // The menu of a component, the menu that reverts a value to its prefab's, and the row the colour
    // picker edits.
    scene::Entity componentMenu;
    PanelButton removeComponent;
    std::optional<std::size_t> menuSection;
    scene::Entity revertMenu;
    PanelButton revert;
    std::optional<std::size_t> revertRow;
    bool revertName = false;
    std::optional<std::size_t> colorRow;

    // The page shown in place of the entities, and what it is for.
    std::unique_ptr<InspectorPage> page;
    std::string pageKind;
    std::string pageTarget;

    void build(ToolsState& state, EditorUiKit& kit);
    [[nodiscard]] std::string signatureOf(const scene::Scene& edited, std::span<const scene::Entity> inspected) const;
    // Forgets what the panel shows, the edits under way ending as they stand.
    void clear(ToolsState& state, const scene::Scene& edited);
    void rebuild(ToolsState& state, EditorUiKit& kit, scene::Scene& edited, std::span<const scene::Entity> inspected);
    void buildTop(EditorUiKit& kit, const scene::Scene& edited, std::span<const scene::Entity> inspected);
    // The card of a component, with its menu.
    Section& addComponentSection(EditorUiKit& kit, std::string name, const EntityIcon& look, const scene::ComponentType* type);
    void addProperty(EditorUiKit& kit, Section& section, const scene::ComponentType& type, const reflection::FieldInfo& field,
                     std::optional<std::size_t> element, int group, bool shared);
    void addExtras(EditorUiKit& kit, Section& section, int group);

    void sync(ToolsState& state, EditorUiKit& kit, scene::Scene& edited, std::span<const scene::Entity> inspected);
    void syncRow(const ToolsState& state, const scene::Scene& edited, std::span<const scene::Entity> inspected, PropertyRow& row,
                 const ui::ElementStyle& style, const scene::Scene* base, scene::Entity prefabEntity, bool editing);
    void fillChoices(const ToolsState& state, const scene::Scene& edited, PropertyRow& row, const void* address, bool mixed);
    void syncExtras(ToolsState& state, EditorUiKit& kit, scene::Scene& edited, scene::Entity active, Section& section,
                    const ui::ElementStyle& style);
    void syncEntityColor(const scene::Scene& edited, std::span<const scene::Entity> inspected);

    void answer(ToolsState& state, EditorUiKit& kit, scene::Scene& edited, std::span<const scene::Entity> inspected);
    void answerRow(ToolsState& state, scene::Scene& edited, std::span<const scene::Entity> inspected, std::size_t index);
    void answerEntityColor(scene::Scene& edited, std::span<const scene::Entity> inspected);
    void answerMenus(ToolsState& state, EditorUiKit& kit, scene::Scene& edited, std::span<const scene::Entity> inspected);
    void beginEdit(const scene::Scene& edited, std::span<const scene::Entity> inspected, PropertyRow& row);
    void commit(ToolsState& state, const scene::Scene& edited, PropertyRow& row);
    [[nodiscard]] bool isActive(std::size_t index);

    void update(ToolsState& state, EditorUiKit& kit, scene::Scene& edited, core::Duration delta);
    void updatePage(ToolsState& state, EditorUiKit& kit, const scene::Scene& edited, std::string_view kind, core::Duration delta);

    // Writes into the value every inspected entity has, knowing which one is the active entity.
    template <typename Write>
    void each(const scene::Scene& edited, std::span<const scene::Entity> inspected, const PropertyRow& row, const Write& write);
};

} // namespace devex::tools::detail
