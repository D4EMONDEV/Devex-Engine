// The pieces of the forms of the editor: cards, rows, toggles, choices, numbers, fields, swatches,
// notes, lines of buttons, grids of sprites, and the colour picker they open.
#include "FormUi.hpp"

#include <devex/ui/Color.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <string>
#include <utility>

namespace devex::tools::detail {

using scene::Entity;
using scene::UiRect;
using namespace rects;
using Button = PanelButton;

namespace {

constexpr std::array<std::string_view, 4> channelLetters{"r", "g", "b", "a"};

} // namespace

std::string foldKey(const Section& section, int group)
{
    return group < 0 ? section.name : std::format("{}/{}", section.name, section.groups[static_cast<std::size_t>(group)]);
}

std::string hexOf(math::Vec4 color, bool alpha)
{
    const float intensity = std::max({1.0f, color.x, color.y, color.z});
    return "#" + ui::hexFromColor(ui::srgbFromLinear(math::Vec4{color.x / intensity, color.y / intensity, color.z / intensity, color.w}),
                                  alpha);
}

FormUi::FormUi(std::uint32_t surface)
    : PanelBuilder(surface)
{
}

void FormUi::setFont(float size) noexcept
{
    font = size;
    line = std::round(font * 1.95f);
    pad = std::round(font * 0.4f);
    gap = std::max(std::round(font * 0.15f), 1.0f);
    headerHeight = std::round(font * 2.1f);
}

void FormUi::buildForm(Entity parent, UiRect rect)
{
    scroll = add(parent, "Scroll", rect, "scroll");
    scene().add<scene::UiScroll>(scroll, scene::UiScroll{});
    content = add(scroll, "Content",
                  UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 0.0f}, .offsetMin = {font * 0.3f, font * 0.3f},
                         .offsetMax = {-font * 0.3f - 8.0f, 0.0f}});
    scene().add<scene::UiLayout>(content, scene::UiLayout{.kind = scene::UiLayoutKind::Column,
                                                          .spacing = font * 0.45f,
                                                          .align = scene::TextAlign::Left});
}

void FormUi::buildColorPopup()
{
    const ThemeColors& colors = themeColors();
    colorPopup = menu("Color", font * 17.0f);
    scene::UiLayout& layout = scene().get<scene::UiLayout>(colorPopup);
    layout.spacing = font * 0.45f;
    layout.padding = math::Vec4{font * 0.6f};
    picker = add(colorPopup, "Picker", wide(font * 11.0f));
    scene().add<scene::UiColorPicker>(picker, scene::UiColorPicker{.barSize = std::round(font * 1.1f), .spacing = std::round(font * 0.5f)});
    const Entity hexRow = add(colorPopup, "Hex", wide(line));
    text(hexRow, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 1.0f}, .offsetMin = {0.0f, 0.0f}, .offsetMax = {font * 2.6f, 0.0f}},
         "Hex", "dim");
    hexField = field(hexRow,
                     UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {font * 2.8f, 2.0f}, .offsetMax = {0.0f, -2.0f}},
                     "", "");
    scene().get<scene::UiText>(hexField).font = EditorUiKit::monoFont();
    channelRow = add(colorPopup, "Channels", wide(line - 4.0f));
    scene().add<scene::UiLayout>(channelRow, scene::UiLayout{.kind = scene::UiLayoutKind::Row,
                                                             .spacing = font * 0.3f,
                                                             .equalSize = true,
                                                             .align = scene::TextAlign::Left});
    const std::array<ImVec4, 4> channelColors{colors.axisX, colors.axisY, colors.axisZ, colors.textDim};
    for (std::size_t index = 0; index < channels.size(); ++index)
    {
        channels[index] = numberBox(channelRow, channelLetters[index], channelColors[index], {.dragSpeed = 0.005f, .decimals = 3});
    }
}

void FormUi::clearForm()
{
    ui::UiWorld& world = panel.world();
    if (colorPopup.isValid())
    {
        world.closePopup(scene(), colorPopup);
    }
    for (const Entity made : pageEntities)
    {
        if (scene().isAlive(made))
        {
            world.closePopup(scene(), made);
            scene().destroyEntity(made);
        }
    }
    pageEntities.clear();
    formRows.clear();
    grids.clear();
    hexTyping = false;
    std::vector<Entity> children;
    for (Entity child = scene().firstChild(content); child.isValid(); child = scene().nextSibling(child))
    {
        children.push_back(child);
    }
    for (const Entity child : children)
    {
        scene().destroyEntity(child);
    }
    sections.clear();
}

Entity FormUi::numberBox(Entity parent, std::string_view letter, ImVec4 letterColor, const scene::UiNumberField& settings)
{
    const Entity box = add(parent, "Number", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 1.0f}, .offsetMin = {0.0f, 0.0f}, .offsetMax = {0.0f, 0.0f}},
                           "number");
    scene().add<scene::UiImage>(box);
    scene().add<scene::UiText>(box, scene::UiText{.text = "",
                                                  .font = EditorUiKit::regularFont(),
                                                  .size = font,
                                                  .verticalAlign = scene::TextVerticalAlign::Middle,
                                                  .wrap = false});
    scene().add<scene::UiInput>(box, scene::UiInput{.padding = {letter.empty() ? font * 0.55f : font * 1.55f, 0.0f}});
    scene().add<scene::UiNumberField>(box, settings);
    // For the light the pointer gives it.
    scene().add<scene::UiButton>(box);
    if (!letter.empty())
    {
        const Entity mark = text(box, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 1.0f}, .offsetMin = {font * 0.45f, 0.0f},
                                             .offsetMax = {font * 1.4f, 0.0f}},
                                 std::string(letter), {}, true);
        scene().get<scene::UiText>(mark).color = linearColor(letterColor);
    }
    return box;
}

Button FormUi::toolButton(EditorUiKit& kit, Entity parent, Icon glyph, UiRect rect)
{
    Button made;
    made.entity = add(parent, "Button", rect, "tool");
    scene().add<scene::UiImage>(made.entity);
    scene().add<scene::UiButton>(made.entity);
    const float inset = std::round((rect.offsetMax.y - rect.offsetMin.y) * 0.2f);
    made.icon = icon(kit, made.entity, whole(math::Vec4{inset}), glyph, "icon_dim");
    return made;
}

void FormUi::fitText(EditorUiKit& kit, Entity entity, std::string value, bool bold)
{
    scene::UiText& shown = scene().get<scene::UiText>(entity);
    if (shown.text == value)
    {
        return;
    }
    UiRect& rect = scene().get<UiRect>(entity);
    rect.offsetMax.x = rect.offsetMin.x + kit.textWidth(bold ? EditorUiKit::boldFont() : EditorUiKit::regularFont(), value, shown.size) + 2.0f;
    shown.text = std::move(value);
}

Section& FormUi::addSection(EditorUiKit& kit, std::string name, const EntityIcon& look, const scene::ComponentType* type)
{
    Section& section = sections.emplace_back();
    section.type = type;
    section.name = std::move(name);
    section.card = add(content, "Component", wide(headerHeight + pad * 2.0f), "card");
    scene().add<scene::UiImage>(section.card);
    scene().add<scene::UiLayout>(section.card, scene::UiLayout{.kind = scene::UiLayoutKind::Column,
                                                               .spacing = gap,
                                                               .padding = math::Vec4{pad},
                                                               .align = scene::TextAlign::Left});
    section.header = add(section.card, "Header", wide(headerHeight), "card_header");
    scene().add<scene::UiImage>(section.header);
    scene().add<scene::UiButton>(section.header);
    scene().add<scene::UiFoldout>(section.header);
    // The arrow of the foldout stands in the first square of the header.
    const float arrow = std::min(headerHeight, 28.0f);
    const float iconSize = std::round(font * 1.15f);
    section.icon = icon(kit, section.header, UiRect{.anchorMin = {0.0f, 0.5f}, .anchorMax = {0.0f, 0.5f}, .offsetMin = {arrow, -iconSize * 0.5f},
                                                    .offsetMax = {arrow + iconSize, iconSize * 0.5f}},
                        iconOf(look.icon), {});
    scene().get<scene::UiImage>(section.icon).color = linearColor(look.color);
    section.title = text(section.header, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {arrow + iconSize + font * 0.5f, 0.0f},
                                                .offsetMax = {-headerHeight, 0.0f}},
                         section.name, "text", true);
    section.added = add(section.header, "Added", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 1.0f}, .offsetMin = {0.0f, 4.0f},
                                                        .offsetMax = {3.0f, -4.0f}, .visible = false},
                        "mark");
    scene().add<scene::UiImage>(section.added, scene::UiImage{.raycastTarget = false});
    return section;
}

FormUi::Heading FormUi::heading(EditorUiKit& kit, IconText glyph, ImVec4 color, std::string title, std::string subtitle)
{
    const float iconSize = std::round(font * 1.3f);
    const Entity top = add(content, "Top", wide(std::round(line * 1.05f)));
    Heading made;
    made.icon = icon(kit, top,
                     UiRect{.anchorMin = {0.0f, 0.5f}, .anchorMax = {0.0f, 0.5f}, .offsetMin = {font * 0.2f, -iconSize * 0.5f},
                            .offsetMax = {font * 0.2f + iconSize, iconSize * 0.5f}},
                     iconOf(glyph), {});
    scene().get<scene::UiImage>(made.icon).color = linearColor(color);
    made.title = text(top,
                      UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {font * 0.7f + iconSize, 0.0f},
                             .offsetMax = {0.0f, 0.0f}},
                      std::move(title), "text", true, scene::TextAlign::Left, std::round(font * 1.1f));
    made.subtitle = text(content, wide(std::round(font * 1.2f)), std::move(subtitle), "dim", false, scene::TextAlign::Left,
                         std::round(font * 0.85f));
    scene().get<scene::UiText>(made.subtitle).font = EditorUiKit::monoFont();
    return made;
}

Section& FormUi::card(EditorUiKit& kit, std::string name, std::optional<EntityIcon> look)
{
    Section& section = addSection(kit, std::move(name), look.value_or(EntityIcon{icons::Box, themeColors().textDim}), nullptr);
    if (!look)
    {
        // Without an icon, the title stands after the arrow.
        scene().get<UiRect>(section.icon).visible = false;
        scene().get<UiRect>(section.title).offsetMin.x = std::min(headerHeight, 28.0f);
    }
    return section;
}

FormRow FormUi::formRow(Section& section, std::string label, float height)
{
    FormRow made;
    made.row = add(section.card, "Property", wide(height > 0.0f ? height : line));
    const Entity box = add(made.row, "Label",
                           UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 1.0f}, .offsetMin = {font * 0.35f, 0.0f},
                                  .offsetMax = {labelWidth, 0.0f}, .clipChildren = true});
    made.label = text(box, whole(), std::move(label), "label");
    made.mark = add(made.row, "Changed",
                    UiRect{.anchorMin = {0.0f, 0.2f}, .anchorMax = {0.0f, 0.8f}, .offsetMin = {0.0f, 0.0f}, .offsetMax = {2.0f, 0.0f},
                           .visible = false},
                    "mark");
    scene().add<scene::UiImage>(made.mark, scene::UiImage{.raycastTarget = false});
    made.editor = add(made.row, "Editor",
                      UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {labelWidth, 2.0f}, .offsetMax = {0.0f, -2.0f}});
    section.lines.push_back(Line{.entity = made.row});
    formRows.push_back(made);
    return made;
}

Entity FormUi::toggle(Entity editor)
{
    const float size = std::round(font * 1.3f);
    const Entity box = add(editor, "Toggle",
                           UiRect{.anchorMin = {0.0f, 0.5f}, .anchorMax = {0.0f, 0.5f}, .offsetMin = {0.0f, -size * 0.5f},
                                  .offsetMax = {size, size * 0.5f}},
                           "toggle");
    scene().add<scene::UiImage>(box);
    scene().add<scene::UiToggle>(box);
    scene().add<scene::UiButton>(box);
    text(editor, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {size + font * 0.5f, 0.0f}, .offsetMax = {0.0f, 0.0f}},
         "Off", "dim");
    return box;
}

void FormUi::setToggle(Entity toggle, bool value)
{
    scene().get<scene::UiToggle>(toggle).value = value;
    if (const Entity word = scene().nextSibling(toggle); word.isValid() && scene().has<scene::UiText>(word))
    {
        scene().get<scene::UiText>(word).text = value ? "On" : "Off";
    }
}

Entity FormUi::choice(Entity editor, std::vector<std::string> options)
{
    const Entity made = add(editor, "Choice", whole(), "dropdown");
    scene().add<scene::UiImage>(made);
    scene().add<scene::UiText>(made, scene::UiText{.text = "",
                                                   .font = EditorUiKit::regularFont(),
                                                   .size = font,
                                                   .verticalAlign = scene::TextVerticalAlign::Middle,
                                                   .wrap = false});
    scene().add<scene::UiDropdown>(made, scene::UiDropdown{.options = std::move(options)});
    return made;
}

void FormUi::setChoice(Entity choice, std::vector<std::string> options, std::int32_t selected)
{
    scene::UiDropdown& dropdown = scene().get<scene::UiDropdown>(choice);
    // The list stays as it is while it is open.
    if (dropdown.options != options && panel.world().listedDropdown() != choice)
    {
        dropdown.options = std::move(options);
    }
    dropdown.selected = selected;
}

std::vector<Entity> FormUi::numbers(Entity editor, std::span<const std::string_view> letters, const scene::UiNumberField& settings)
{
    scene().add<scene::UiLayout>(editor, scene::UiLayout{.kind = scene::UiLayoutKind::Row,
                                                         .spacing = font * 0.3f,
                                                         .equalSize = true,
                                                         .align = scene::TextAlign::Left});
    const ThemeColors& colors = themeColors();
    const std::array<ImVec4, 4> tints{colors.axisX, colors.axisY, colors.axisZ, colors.textDim};
    std::vector<Entity> made;
    for (std::size_t index = 0; index < letters.size(); ++index)
    {
        // Axes take their colours; other letters, such as the sides of a border, stay dim.
        const std::string_view letter = letters.size() > 1 ? letters[index] : std::string_view{};
        const bool axis = letter == "x" || letter == "y" || letter == "z";
        made.push_back(numberBox(editor, letter, axis ? tints[index] : colors.textDim, settings));
    }
    return made;
}

void FormUi::setNumber(Entity number, float value)
{
    const ui::UiWorld& world = panel.world();
    if (world.held() != number && world.editedField() != number)
    {
        scene().get<scene::UiNumberField>(number).value = value;
    }
}

Entity FormUi::textField(Entity editor, std::string placeholder)
{
    return field(editor, whole(), "", std::move(placeholder));
}

void FormUi::setText(Entity field, std::string value)
{
    if (panel.world().editedField() != field)
    {
        scene::UiText& shown = scene().get<scene::UiText>(field);
        if (shown.text != value)
        {
            shown.text = std::move(value);
        }
    }
}

Entity FormUi::swatch(EditorUiKit& kit, Entity editor, bool alpha)
{
    const float hexSize = std::round(font * 0.9f);
    const float hexWidth = kit.textWidth(EditorUiKit::monoFont(), alpha ? "#FFFFFFFF" : "#FFFFFF", hexSize) + font * 0.4f;
    const Entity made = add(editor, "Swatch", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {0.0f, 0.0f},
                                                     .offsetMax = {-hexWidth - font * 0.3f, 0.0f}},
                            "swatch");
    scene().add<scene::UiImage>(made);
    scene().add<scene::UiButton>(made);
    tooltip(made, "Choose the colour");
    const Entity hex = text(editor, UiRect{.anchorMin = {1.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {-hexWidth, 0.0f}, .offsetMax = {0.0f, 0.0f}},
                            "", "dim", false, scene::TextAlign::Right, hexSize);
    scene().get<scene::UiText>(hex).font = EditorUiKit::monoFont();
    return made;
}

void FormUi::setSwatch(Entity swatch, math::Vec4 color, bool alpha)
{
    scene().get<scene::UiImage>(swatch).color = math::Vec4{color.x, color.y, color.z, 1.0f};
    if (const Entity hex = scene().nextSibling(swatch); hex.isValid() && scene().has<scene::UiText>(hex))
    {
        scene().get<scene::UiText>(hex).text = hexOf(color, alpha);
    }
}

Entity FormUi::note(Section* section, std::string value, std::string_view style, float lines)
{
    const float height = lines > 1.0f ? std::round(font * 1.45f * lines + font * 0.3f) : std::round(font * 1.7f);
    const Entity made = text(section != nullptr ? section->card : content, wide(height), std::move(value), style);
    if (lines > 1.0f)
    {
        scene().get<scene::UiText>(made).wrap = true;
    }
    if (section != nullptr)
    {
        scene().get<UiRect>(made).offsetMin.x = font * 0.35f;
        section->lines.push_back(Line{.entity = made});
    }
    return made;
}

Entity FormUi::actions(Section* section)
{
    const Entity made = add(section != nullptr ? section->card : content, "Actions", wide(line));
    scene().add<scene::UiLayout>(made, scene::UiLayout{.kind = scene::UiLayoutKind::Row,
                                                       .spacing = font * 0.4f,
                                                       .padding = {section != nullptr ? font * 0.35f : 0.0f, 0.0f, 0.0f, 0.0f},
                                                       .align = scene::TextAlign::Left});
    if (section != nullptr)
    {
        section->lines.push_back(Line{.entity = made});
    }
    return made;
}

Button FormUi::action(EditorUiKit& kit, Entity actions, std::optional<Icon> glyph, std::string_view label, std::string_view style)
{
    return button(kit, actions, glyph, label, style, 0.0f, line - 4.0f);
}

void FormUi::showLine(Section& section, Entity entity, bool shown)
{
    for (Line& entry : section.lines)
    {
        if (entry.entity == entity)
        {
            entry.shown = shown;
        }
    }
}

SpriteGrid& FormUi::spriteGrid(EditorUiKit& kit, Section& section, std::unique_ptr<SpriteGrid>& grid, std::size_t count, float size,
                               bool dropBox)
{
    grid = std::make_unique<SpriteGrid>();
    grid->size = size;
    grid->grid = add(section.card, "Grid",
                     UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 0.0f}, .offsetMin = {font * 0.35f, 0.0f}, .offsetMax = {size, size}});
    scene().add<scene::UiLayout>(grid->grid, scene::UiLayout{.kind = scene::UiLayoutKind::Grid,
                                                             .spacing = std::round(font * 0.35f),
                                                             .columns = 1,
                                                             .equalSize = true,
                                                             .align = scene::TextAlign::Left});
    gridCells(*grid, count, true);
    if (dropBox)
    {
        grid->drop = add(grid->grid, "Drop", fixed({size, size}), "row");
        scene().add<scene::UiImage>(grid->drop);
        const math::Vec4 accent = linearColor(themeColors().accent);
        scene().add<scene::UiDropTarget>(grid->drop, scene::UiDropTarget{.accepts = {"asset:sprite", "asset:texture"},
                                                                         .highlightColor = math::Vec4{accent.x, accent.y, accent.z, 0.35f}});
        const float inset = std::round(size * 0.32f);
        icon(kit, grid->drop, whole(math::Vec4{inset}), Icon::Plus, "icon_dim");
    }
    section.lines.push_back(Line{.entity = grid->grid});
    grids.push_back(grid.get());
    return *grid;
}

void FormUi::gridCells(SpriteGrid& grid, std::size_t count, bool drops)
{
    const math::Vec4 accent = linearColor(themeColors().accent);
    while (grid.cells.size() < count)
    {
        const Entity cell = add(grid.grid, "Cell", fixed({grid.size, grid.size}), "row");
        scene().add<scene::UiImage>(cell);
        scene().add<scene::UiButton>(cell);
        scene().add<scene::UiContextMenu>(cell);
        if (drops)
        {
            scene().add<scene::UiDropTarget>(cell, scene::UiDropTarget{.accepts = {"asset:sprite", "asset:texture"},
                                                                       .highlightColor = math::Vec4{accent.x, accent.y, accent.z, 0.35f}});
        }
        const Entity image = add(cell, "Sprite", whole(math::Vec4{std::round(grid.size * 0.08f)}));
        scene().add<scene::UiImage>(image, scene::UiImage{.raycastTarget = false, .preserveAspect = true});
        grid.cells.push_back(cell);
        grid.images.push_back(image);
    }
    for (std::size_t index = 0; index < grid.cells.size(); ++index)
    {
        scene().get<UiRect>(grid.cells[index]).visible = index < count;
    }
}

Entity FormUi::pageMenu(const char* name, float width)
{
    const Entity made = menu(name, width);
    pageEntities.push_back(made);
    return made;
}

void FormUi::showSprite(Entity image, asset::AssetId sprite)
{
    scene().get<scene::UiImage>(image).texture = sprite;
    scene().get<UiRect>(image).visible = sprite.isValid();
}

void FormUi::layoutCards()
{
    // The rows of the pages follow the width of the panel, as those of the components do.
    for (const FormRow& row : formRows)
    {
        scene().get<UiRect>(scene().parent(row.label)).offsetMax.x = labelWidth - font * 0.4f;
        scene().get<UiRect>(row.editor).offsetMin.x = labelWidth;
    }
    // Grids of sprites wrap at the width of their card: as many columns as fit, as many rows as needed.
    const float room = std::max(panel.size().x - font * 0.6f - 8.0f - pad * 2.0f - font * 0.35f, 1.0f);
    for (SpriteGrid* grid : grids)
    {
        const float spacing = scene().get<scene::UiLayout>(grid->grid).spacing;
        const auto shownCells = static_cast<std::size_t>(
            std::ranges::count_if(grid->cells, [&](Entity cell) { return scene().get<UiRect>(cell).visible; }));
        const std::size_t count = shownCells + (grid->drop.isValid() ? 1 : 0);
        const auto columns = static_cast<std::size_t>(std::max(std::floor((room + spacing) / (grid->size + spacing)), 1.0f));
        const std::size_t rowsNeeded = std::max<std::size_t>((count + columns - 1) / columns, 1);
        const std::size_t shownColumns = std::min(columns, std::max<std::size_t>(count, 1));
        scene().get<scene::UiLayout>(grid->grid).columns = static_cast<std::uint32_t>(shownColumns);
        UiRect& rect = scene().get<UiRect>(grid->grid);
        rect.offsetMax = rect.offsetMin + math::Vec2{static_cast<float>(shownColumns) * (grid->size + spacing) - spacing,
                                                     static_cast<float>(rowsNeeded) * (grid->size + spacing) - spacing};
    }

    // The layout skips hidden lines and cards: a card is as tall as its header and the lines it shows.
    for (Section& section : sections)
    {
        UiRect& card = scene().get<UiRect>(section.card);
        card.visible = !section.hidden;
        if (section.hidden)
        {
            continue;
        }
        const bool open = !section.locked && !folded.contains(section.name);
        scene().get<scene::UiFoldout>(section.header).expanded = open;
        scene().get<scene::UiFoldout>(section.header).interactable = !section.locked;
        float height = pad * 2.0f + headerHeight;
        for (const Line& entry : section.lines)
        {
            const bool groupOpen = entry.group < 0 || !folded.contains(foldKey(section, entry.group));
            if (entry.heading)
            {
                scene().get<scene::UiFoldout>(entry.entity).expanded = groupOpen;
            }
            UiRect& rect = scene().get<UiRect>(entry.entity);
            rect.visible = open && entry.shown && !entry.filtered && (entry.heading || groupOpen);
            if (rect.visible)
            {
                height += gap + rect.offsetMax.y - rect.offsetMin.y;
            }
        }
        card.offsetMax.y = card.offsetMin.y + height;
    }
}

void FormUi::answerForm()
{
    const ui::UiWorld& world = panel.world();
    for (Section& section : sections)
    {
        if (world.wasChanged(section.header))
        {
            if (!folded.erase(section.name))
            {
                folded.insert(section.name);
            }
        }
        for (const Line& entry : section.lines)
        {
            if (entry.heading && world.wasChanged(entry.entity))
            {
                const std::string key = foldKey(section, entry.group);
                if (!folded.erase(key))
                {
                    folded.insert(key);
                }
            }
        }
    }
    const Entity now = world.editedField();
    endedField = editingField.isValid() && editingField != now && scene().isAlive(editingField) && !panel.input().cancelPressed ? editingField
                                                                                                                              : Entity{};
    editingField = now;
}

void FormUi::openColorPopup(Entity swatch)
{
    ui::UiWorld& world = panel.world();
    hexTyping = false;
    std::optional<math::Vec2> at;
    if (const ui::LaidOutRect* const rect = world.canvases().empty() ? nullptr : world.canvases().front().layout.find(swatch))
    {
        at = math::Vec2{rect->min.x, rect->max.y + 2.0f};
    }
    world.openPopup(scene(), colorPopup, at);
}

bool FormUi::colorPopupOpen()
{
    return colorPopup.isValid() && panel.world().isPopupOpen(scene(), colorPopup);
}

void FormUi::syncColorPopup(math::Vec4 color, bool alpha)
{
    const ui::UiWorld& world = panel.world();
    scene::UiColorPicker& chosen = scene().get<scene::UiColorPicker>(picker);
    chosen.alpha = alpha;
    chosen.interactable = true;
    if (world.held() != picker)
    {
        chosen.color = color;
    }
    if (world.editedField() != hexField)
    {
        scene().get<scene::UiText>(hexField).text = hexOf(color, alpha);
    }
    const std::size_t count = alpha ? 4 : 3;
    for (std::size_t index = 0; index < channels.size(); ++index)
    {
        scene().get<UiRect>(channels[index]).visible = index < count;
        if (index < count)
        {
            setNumber(channels[index], color[static_cast<math::Vec4::length_type>(index)]);
            scene().get<scene::UiNumberField>(channels[index]).format = "{}";
        }
    }
    const scene::UiLayout& layout = scene().get<scene::UiLayout>(colorPopup);
    UiRect& popup = scene().get<UiRect>(colorPopup);
    const float height = layout.padding.y + layout.padding.w + font * 11.0f + line + (line - 4.0f) + layout.spacing * 2.0f;
    popup.offsetMax.y = popup.offsetMin.y + height;
}

std::optional<math::Vec4> FormUi::answerColorPopup(math::Vec4 current)
{
    ui::UiWorld& world = panel.world();
    if (!colorPopupOpen())
    {
        hexTyping = false;
        return std::nullopt;
    }
    std::optional<math::Vec4> chosen;
    if (world.wasChanged(picker))
    {
        chosen = scene().get<scene::UiColorPicker>(picker).color;
    }
    // The hexadecimal, read once the field is left; a colour brighter than white keeps its intensity.
    if (world.editedField() == hexField)
    {
        hexTyping = true;
    }
    else if (std::exchange(hexTyping, false) && !panel.input().cancelPressed)
    {
        const std::string written = scene().get<scene::UiText>(hexField).text;
        std::string_view digits = written;
        while (!digits.empty() && (digits.front() == '#' || digits.front() == ' '))
        {
            digits.remove_prefix(1);
        }
        const bool writesAlpha = digits.size() == 4 || digits.size() == 8;
        if (const std::optional<math::Vec4> read = ui::colorFromHex(written))
        {
            const float intensity = std::max({1.0f, current.x, current.y, current.z});
            const math::Vec4 linear = ui::linearFromSrgb(*read);
            chosen = math::Vec4{linear.x * intensity, linear.y * intensity, linear.z * intensity, writesAlpha ? read->w : current.w};
        }
    }
    for (std::size_t index = 0; index < channels.size(); ++index)
    {
        if (world.wasChanged(channels[index]))
        {
            math::Vec4 color = chosen.value_or(current);
            color[static_cast<math::Vec4::length_type>(index)] = scene().get<scene::UiNumberField>(channels[index]).value;
            chosen = color;
        }
    }
    return chosen;
}

} // namespace devex::tools::detail
