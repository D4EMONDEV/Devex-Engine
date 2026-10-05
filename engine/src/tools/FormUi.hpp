#pragma once

// The pieces the forms of the editor are made of, as the inspector shows them: cards folded from their
// header, rows of a label and its controls, notes, lines of buttons, grids of sprites and a colour
// picker. The inspector, the windows of settings, the export and the dialogs are made of them, as
// Godot builds its docks and dialogs with the same controls.
#include "EditorUi.hpp"

#include <devex/scene/UiComponents.hpp>

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

namespace devex::scene {
struct ComponentType;
}

namespace devex::tools::detail {

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
    // Whether a filter of a window of settings leaves it out.
    bool filtered = false;
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
    // The page of a window of settings the card stands in, -1 outside them; a card another page or a
    // filter leaves out is hidden.
    int page = -1;
    bool hidden = false;
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

// The key of a card, or of a group of a card, among those folded.
[[nodiscard]] std::string foldKey(const Section& section, int group);

// The hexadecimal of a colour as the eye sees it; a colour brighter than white shows its hue.
[[nodiscard]] std::string hexOf(math::Vec4 color, bool alpha);

// A panel made of cards, with a list that scrolls and the pieces of forms.
struct FormUi : PanelBuilder
{
    explicit FormUi(std::uint32_t surface);

    float line = 28.0f;
    float pad = 6.0f;
    float gap = 2.0f;
    float headerHeight = 30.0f;
    float labelWidth = 120.0f;

    scene::Entity scroll;
    scene::Entity content;
    std::vector<Section> sections;
    std::vector<FormRow> formRows;
    std::vector<SpriteGrid*> grids;
    // The cards and the groups folded, by name.
    std::unordered_set<std::string> folded;
    // What a page made outside the content, such as its menus, gone with it.
    std::vector<scene::Entity> pageEntities;

    // The colour picker: the square and its bars, the hexadecimal the eye sees, and the numbers of
    // the value, which may go past 1 for a light brighter than white.
    scene::Entity colorPopup;
    scene::Entity picker;
    scene::Entity hexField;
    scene::Entity channelRow;
    std::array<scene::Entity, 4> channels{};
    bool hexTyping = false;

    // The field being typed into at the last update, and the one whose edit ended then, with Enter or
    // a click elsewhere; Escape drops what was typed, and ends nothing.
    scene::Entity editingField;
    scene::Entity endedField;

    // The sizes of the pieces follow the size of the text.
    void setFont(float size) noexcept;
    // The list that scrolls and the column of cards in it, in a parent.
    void buildForm(scene::Entity parent, scene::UiRect rect);
    void buildColorPopup();
    // Forgets the cards and what the pages made, the colour picker closed.
    void clearForm();

    scene::Entity numberBox(scene::Entity parent, std::string_view letter, math::Vec4 letterColor, const scene::UiNumberField& settings);
    PanelButton toolButton(EditorUiKit& kit, scene::Entity parent, Icon glyph, scene::UiRect rect);
    // Sets a text and makes its rectangle as wide as it.
    void fitText(EditorUiKit& kit, scene::Entity entity, std::string value, bool bold = false);
    Section& addSection(EditorUiKit& kit, std::string name, const EntityIcon& look, const scene::ComponentType* type);

    struct Heading
    {
        scene::Entity icon;
        scene::Entity title;
        scene::Entity subtitle;
    };
    // The icon and the name of what the page shows, and its file under them.
    Heading heading(EditorUiKit& kit, IconText glyph, math::Vec4 color, std::string title, std::string subtitle);
    // A card folded from its header, which the rows of the page go into.
    Section& card(EditorUiKit& kit, std::string name, std::optional<EntityIcon> look = std::nullopt);
    FormRow formRow(Section& section, std::string label, float height = 0.0f);
    scene::Entity toggle(scene::Entity editor);
    // Turns a toggle of a page on or off, and the word beside it.
    void setToggle(scene::Entity toggle, bool value);
    scene::Entity choice(scene::Entity editor, std::vector<std::string> options = {});
    // Sets the options of a choice and the one chosen, keeping the list when it is the same.
    void setChoice(scene::Entity choice, std::vector<std::string> options, std::int32_t selected);
    // Numbers side by side in an editor, each named by its letter when there are several.
    std::vector<scene::Entity> numbers(scene::Entity editor, std::span<const std::string_view> letters,
                                       const scene::UiNumberField& settings);
    // Shows a value in a number, unless the pointer drags it or it is being typed.
    void setNumber(scene::Entity number, float value);
    scene::Entity textField(scene::Entity editor, std::string placeholder = {});
    // Shows a text in a field, unless it is being typed.
    void setText(scene::Entity field, std::string value);
    // A swatch that opens the colour picker, with the hexadecimal of its colour at its right.
    scene::Entity swatch(EditorUiKit& kit, scene::Entity editor, bool alpha);
    void setSwatch(scene::Entity swatch, math::Vec4 color, bool alpha);
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

    // Which lines of the cards show, and how tall each card is; the form rows and the grids follow
    // the width of the panel.
    void layoutCards();
    // After the update of the panel: the cards and groups folded from their headers, and the field
    // whose edit ended.
    void answerForm();

    // The colour picker, opened under a swatch for a colour.
    void openColorPopup(scene::Entity swatch);
    [[nodiscard]] bool colorPopupOpen();
    void syncColorPopup(math::Vec4 color, bool alpha);
    // The colour chosen in the picker during the last update, from the one it had.
    [[nodiscard]] std::optional<math::Vec4> answerColorPopup(math::Vec4 current);
};

} // namespace devex::tools::detail
