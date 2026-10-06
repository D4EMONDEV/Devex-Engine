// The Translations panel: a table of translations as a spreadsheet shows it, its keys down and its
// languages across, edited in place and written back to its .csv file; and the page of the
// inspector for such a file.
#include "EditorFrame.hpp"
#include "InspectorUi.hpp"
#include "SettingsUi.hpp"

#include <devex/asset/Artifact.hpp>
#include <devex/asset/TranslationData.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>
#include <devex/core/Profiler.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <format>
#include <iterator>
#include <memory>
#include <optional>
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

constexpr std::uint32_t translationsSurface = 24;
// Who opened the menu of the layer over the editor that adds a language.
constexpr std::size_t languageMenuOwner = 300;
// The steps the panel goes back on.
constexpr std::size_t undoSteps = 100;

// Whether a column of a table holds a language, rather than the keys or notes.
[[nodiscard]] bool isLanguageColumn(const asset::CsvTable& csv, std::size_t column)
{
    return column > 0 && !csv.rows.empty() && column < csv.rows.front().size() && asset::isLanguageCode(csv.rows.front()[column]);
}

[[nodiscard]] const std::string& cellOf(const asset::CsvTable& csv, std::size_t row, std::size_t column)
{
    static const std::string none;
    return row < csv.rows.size() && column < csv.rows[row].size() ? csv.rows[row][column] : none;
}

} // namespace

struct TranslationsUi : PanelBuilder
{
    TranslationsUi()
        : PanelBuilder(translationsSurface)
    {
    }

    struct Cell
    {
        Entity entity;
        Entity label;
        // What the missing translations of a column count, in its header.
        Entity badge;
        Entity field;
    };
    struct Row
    {
        Entity entity;
        std::vector<Cell> cells;
    };
    struct Editing
    {
        Entity field;
        std::size_t row = 0;
        std::size_t column = 0;
    };

    bool built = false;
    float builtFont = 0.0f;
    std::size_t builtColumns = 0;
    Entity toolbar;
    Entity tableChoice;
    Entity search;
    Button newTable;
    Button keyButton;
    Button languageButton;
    Button missingButton;
    Entity table;
    Row header;
    Entity list;
    Entity scroll;
    Entity lines;
    std::vector<Row> rows;
    Entity note;
    Entity rowMenu;
    Button menuUp;
    Button menuDown;
    Button menuDelete;
    Entity columnMenu;
    Button menuRemoveColumn;

    // The table shown, as its file holds it.
    asset::AssetId shown;
    std::filesystem::path file;
    std::filesystem::file_time_type fileTime{};
    double checkedAt = -1.0;
    asset::CsvTable csv;
    std::string error;
    // The rows of the table in view, by their place in it, after the filter.
    std::vector<std::size_t> visible;
    // The tables of the project, as the choice lists them.
    std::vector<asset::AssetId> tables;
    std::vector<std::string> tableNames;
    asset::AssetId lastSelected;
    std::vector<asset::CsvTable> undo;
    std::vector<asset::CsvTable> redo;
    std::optional<Editing> editing;
    std::optional<std::pair<std::size_t, std::size_t>> pendingEdit;
    std::optional<std::size_t> revealRow;
    // The rows of the view, where the first one stands.
    std::size_t first = 0;
    bool onlyMissing = false;

    [[nodiscard]] float rowHeight() const noexcept
    {
        return std::round(font * 1.9f);
    }

    [[nodiscard]] std::size_t columns() const noexcept
    {
        return csv.rows.empty() ? 0 : csv.rows.front().size();
    }

    void build(EditorUiKit& kit);
    Row makeRow(EditorUiKit& kit, Entity parent, bool isHeader);
    void destroyRows();
    void listTables(ToolsState& state);
    void load(ToolsState& state, asset::AssetId id);
    void reloadIfChanged(ToolsState& state);
    // Writes the table to its file, after keeping what it was for undo.
    void change(const asset::CsvTable& before);
    void save();
    void filter(ToolsState& state);
    void fill(EditorUiKit& kit);
    void startEdit(std::size_t row, std::size_t column);
    void commit(ToolsState& state, const Editing& edited);
    // The cell an element is, or is inside of: its row in the table, and its column.
    [[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>> cellAt(Entity element);
    void addKey(ToolsState& state);
    void addColumn(std::string code);
    void update(ToolsState& state, EditorUiKit& kit, core::Duration delta);
};

void TranslationsUi::build(EditorUiKit& kit)
{
    std::vector<Entity> children;
    for (Entity child = scene().firstChild(panel.canvas()); child.isValid(); child = scene().nextSibling(child))
    {
        children.push_back(child);
    }
    for (const Entity child : children)
    {
        scene().destroyEntity(child);
    }
    built = true;
    builtFont = font;
    builtColumns = 0;
    rows.clear();
    header = {};
    editing.reset();
    const float line = font * 2.0f;

    const Entity root = add({}, "Translations", whole());
    scene().add<scene::UiLayout>(root, scene::UiLayout{.kind = scene::UiLayoutKind::Column, .spacing = font * 0.4f, .align = scene::TextAlign::Left});

    // The table shown and what adds to it on the left, the filter on the right.
    toolbar = add(root, "Toolbar", wide(line));
    scene().add<scene::UiLayout>(toolbar, scene::UiLayout{.kind = scene::UiLayoutKind::Row, .spacing = font * 0.25f, .align = scene::TextAlign::Left});
    tableChoice = add(toolbar, "Table", middle({font * 16.0f, line}), "dropdown");
    scene().add<scene::UiImage>(tableChoice);
    scene().add<scene::UiText>(tableChoice, scene::UiText{.text = "",
                                                          .font = EditorUiKit::regularFont(),
                                                          .size = font,
                                                          .verticalAlign = scene::TextVerticalAlign::Middle,
                                                          .wrap = false});
    scene().add<scene::UiDropdown>(tableChoice);
    tooltip(tableChoice, "The table of translations shown: a .csv file of the project");
    newTable = button(kit, toolbar, Icon::FilePlus, "New Table", "flat", 0.0f, line);
    tooltip(newTable.entity, "Creates a table of translations, with a column for the fallback language");
    keyButton = button(kit, toolbar, Icon::Plus, "Key", "flat", 0.0f, line);
    tooltip(keyButton.entity, "Adds a key: the text the interfaces and the code write, which each language translates");
    languageButton = button(kit, toolbar, Icon::Languages, "Language", "flat", 0.0f, line);
    tooltip(languageButton.entity, "Adds a column for a language");
    missingButton = button(kit, toolbar, Icon::TriangleAlert, "Missing", "flat", 0.0f, line);
    tooltip(missingButton.entity, "Shows only the keys a language does not translate yet");
    search = searchField(kit, toolbar, grow(line), "", "Filter Keys");

    // The header of the columns, and the rows under it in a list that has entities for those in view.
    table = add(root, "Table", whole(), "list");
    scene().add<scene::UiImage>(table);
    scene().add<scene::UiTable>(table, scene::UiTable{.resizable = false});
    list = add(table, "Rows", whole(math::Vec4{0.0f, rowHeight(), 0.0f, 0.0f}));
    scroll = add(list, "Scroll", whole(math::Vec4{0.0f, 0.0f, 0.0f, 2.0f}), "scroll");
    scene().add<scene::UiScroll>(scroll, scene::UiScroll{.horizontal = false, .vertical = true});
    lines = add(scroll, "Lines", fixed({1.0f, 1.0f}));
    scene().add<scene::UiVirtualList>(lines);
    note = text(table, whole(math::Vec4{font, rowHeight() + font, font, font}), "", "dim");
    scene().get<scene::UiText>(note).wrap = true;

    rowMenu = menu("Translation row menu", font * 12.0f);
    menuUp = menuItem(kit, rowMenu, std::nullopt, "Move Up");
    menuDown = menuItem(kit, rowMenu, std::nullopt, "Move Down");
    menuSeparator(rowMenu);
    menuDelete = menuItem(kit, rowMenu, Icon::Trash, "Delete Key");
    columnMenu = menu("Translation column menu", font * 12.0f);
    menuRemoveColumn = menuItem(kit, columnMenu, Icon::Trash, "Remove Column");
}

TranslationsUi::Row TranslationsUi::makeRow(EditorUiKit& kit, Entity parent, bool isHeader)
{
    Row row;
    row.entity = add(parent, isHeader ? "Header" : "Row", isHeader ? wide(rowHeight()) : fixed({1.0f, rowHeight()}));
    scene().add<scene::UiTableRow>(row.entity, scene::UiTableRow{.header = isHeader});
    if (isHeader)
    {
        // Above the list, which starts under it.
        UiRect& rect = scene().get<UiRect>(row.entity);
        rect.anchorMin = {0.0f, 0.0f};
        rect.anchorMax = {1.0f, 0.0f};
        rect.offsetMin = {0.0f, 0.0f};
        rect.offsetMax = {0.0f, rowHeight()};
    }
    static_cast<void>(kit);
    for (std::size_t column = 0; column < builtColumns; ++column)
    {
        Cell cell;
        cell.entity = add(row.entity, "Cell", whole());
        scene().add<scene::UiImage>(cell.entity, scene::UiImage{.color = math::Vec4{0.0f}});
        scene().add<scene::UiButton>(cell.entity);
        scene().add<scene::UiContextMenu>(cell.entity, scene::UiContextMenu{.popup = scene().reference(isHeader ? columnMenu : rowMenu)});
        scene().get<UiRect>(cell.entity).clipChildren = true;
        cell.label = text(cell.entity, whole(math::Vec4{font * 0.45f, 0.0f, font * 0.45f, 0.0f}), "", isHeader ? "text" : "text", isHeader);
        if (isHeader)
        {
            cell.badge = text(cell.entity, UiRect{.anchorMin = {1.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {-font * 2.6f, 0.0f}, .offsetMax = {-font * 0.4f, 0.0f}},
                              "", "warning", false, scene::TextAlign::Right);
        }
        cell.field = field(cell.entity, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {1.0f, 1.0f}, .offsetMax = {-1.0f, -1.0f}, .visible = false},
                           "", "");
        row.cells.push_back(cell);
    }
    return row;
}

void TranslationsUi::destroyRows()
{
    if (header.entity.isValid())
    {
        scene().destroyEntity(header.entity);
    }
    for (const Row& row : rows)
    {
        scene().destroyEntity(row.entity);
    }
    header = {};
    rows.clear();
    editing.reset();
}

void TranslationsUi::listTables(ToolsState& state)
{
    tables.clear();
    tableNames.clear();
    for (const asset::AssetInfo& info : state.database->assets(asset::AssetType::Translation))
    {
        const std::optional<asset::SourceFile> source = state.database->sourceOf(info.id);
        std::string name = source ? source->path : info.name;
        if (name.starts_with("res://assets/"))
        {
            name.erase(0, std::string_view("res://assets/").size());
        }
        tables.push_back(info.id);
        tableNames.push_back(std::move(name));
    }
}

void TranslationsUi::load(ToolsState& state, asset::AssetId id)
{
    shown = id;
    state.translationTable = id;
    undo.clear();
    redo.clear();
    editing.reset();
    pendingEdit.reset();
    csv = {};
    error.clear();
    file.clear();
    const std::optional<asset::SourceFile> source = id.isValid() ? state.database->sourceOf(id) : std::nullopt;
    const std::optional<std::filesystem::path> path = source ? state.database->project().absolutePath(source->path) : std::nullopt;
    if (!path)
    {
        return;
    }
    file = *path;
    std::error_code ignored;
    fileTime = std::filesystem::last_write_time(file, ignored);
    const core::Result<std::string> text = core::readTextFile(file);
    const core::Result<asset::CsvTable> read = text ? asset::parseCsv(*text) : core::Result<asset::CsvTable>(std::unexpected(text.error()));
    if (!read)
    {
        error = read.error().message;
        return;
    }
    csv = *read;
    if (csv.rows.empty())
    {
        csv.rows.push_back({"keys", state.database->project().localization.fallbackLanguage});
    }
    // Every row as wide as the first, which names the columns.
    for (std::vector<std::string>& row : csv.rows)
    {
        row.resize(std::max(row.size(), csv.rows.front().size()));
    }
}

void TranslationsUi::reloadIfChanged(ToolsState& state)
{
    // At most twice a second, and never under the cell being typed into.
    if (file.empty() || editing || state.clock - checkedAt < 0.5)
    {
        return;
    }
    checkedAt = state.clock;
    std::error_code ignored;
    const std::filesystem::file_time_type time = std::filesystem::last_write_time(file, ignored);
    if (!ignored && time != fileTime)
    {
        // Written elsewhere, by a spreadsheet: read again, its history gone with what it went back on.
        std::vector<asset::CsvTable> kept = std::move(undo);
        load(state, shown);
        undo = std::move(kept);
    }
}

void TranslationsUi::change(const asset::CsvTable& before)
{
    undo.push_back(before);
    if (undo.size() > undoSteps)
    {
        undo.erase(undo.begin());
    }
    redo.clear();
    save();
}

void TranslationsUi::save()
{
    if (file.empty())
    {
        return;
    }
    if (core::Result<void> written = core::writeTextFile(file, asset::writeCsv(csv)); !written)
    {
        DEVEX_LOG_ERROR("Cannot save {}: {}", core::toUtf8(file), written.error());
        return;
    }
    std::error_code ignored;
    fileTime = std::filesystem::last_write_time(file, ignored);
}

void TranslationsUi::filter(ToolsState& state)
{
    static_cast<void>(state);
    const std::string_view wanted = scene().get<scene::UiText>(search).text;
    visible.clear();
    for (std::size_t row = 1; row < csv.rows.size(); ++row)
    {
        const std::vector<std::string>& cells = csv.rows[row];
        bool matches = wanted.empty();
        bool missing = false;
        for (std::size_t column = 0; column < cells.size(); ++column)
        {
            matches = matches || containsIgnoringCase(cells[column], wanted);
            missing = missing || (isLanguageColumn(csv, column) && !cells.front().empty() && cells[column].empty());
        }
        if (matches && (!onlyMissing || missing))
        {
            visible.push_back(row);
        }
    }
}

void TranslationsUi::startEdit(std::size_t row, std::size_t column)
{
    pendingEdit = std::pair{row, column};
    if (row > 0)
    {
        revealRow = row;
    }
}

std::optional<std::pair<std::size_t, std::size_t>> TranslationsUi::cellAt(Entity element)
{
    for (Entity above = element; above.isValid() && scene().isAlive(above); above = scene().parent(above))
    {
        for (std::size_t column = 0; column < header.cells.size(); ++column)
        {
            if (header.cells[column].entity == above)
            {
                return std::pair<std::size_t, std::size_t>{0, column};
            }
        }
        for (std::size_t index = 0; index < rows.size(); ++index)
        {
            for (std::size_t column = 0; column < rows[index].cells.size(); ++column)
            {
                if (rows[index].cells[column].entity == above && first + index < visible.size())
                {
                    return std::pair{visible[first + index], column};
                }
            }
        }
    }
    return std::nullopt;
}

void TranslationsUi::commit(ToolsState& state, const Editing& edited)
{
    std::string value = scene().get<scene::UiText>(edited.field).text;
    if (edited.row >= csv.rows.size() || edited.column >= columns() || cellOf(csv, edited.row, edited.column) == value)
    {
        return;
    }
    if (edited.row == 0 && edited.column > 0)
    {
        // The name of a column: a language, or notes when it starts with _.
        if (!value.starts_with('_'))
        {
            if (!asset::isLanguageCode(value))
            {
                DEVEX_LOG_WARNING("'{}' is not a language code such as fr or pt_BR: a column of notes starts with _", value);
                return;
            }
            value = asset::normalizeLanguage(value);
        }
        if (std::ranges::find(csv.rows.front(), value) != csv.rows.front().end())
        {
            DEVEX_LOG_WARNING("The table already has a column {}", value);
            return;
        }
    }
    if (edited.row > 0 && edited.column == 0 && !value.empty())
    {
        for (std::size_t row = 1; row < csv.rows.size(); ++row)
        {
            if (row != edited.row && cellOf(csv, row, 0) == value)
            {
                DEVEX_LOG_WARNING("The table already has the key {}", value);
                return;
            }
        }
    }
    static_cast<void>(state);
    const asset::CsvTable before = csv;
    csv.rows[edited.row][edited.column] = std::move(value);
    change(before);
}

void TranslationsUi::addKey(ToolsState& state)
{
    static_cast<void>(state);
    std::string key = "NEW_KEY";
    for (int number = 2; std::ranges::any_of(csv.rows, [&](const std::vector<std::string>& row) { return !row.empty() && row.front() == key; });
         ++number)
    {
        key = std::format("NEW_KEY_{}", number);
    }
    const asset::CsvTable before = csv;
    std::vector<std::string> row(columns());
    row.front() = key;
    csv.rows.push_back(std::move(row));
    change(before);
    // Shown whatever the filter says, its key ready to be typed.
    scene().get<scene::UiText>(search).text.clear();
    onlyMissing = false;
    startEdit(csv.rows.size() - 1, 0);
}

void TranslationsUi::addColumn(std::string code)
{
    if (csv.rows.empty() || std::ranges::find(csv.rows.front(), code) != csv.rows.front().end())
    {
        return;
    }
    const asset::CsvTable before = csv;
    csv.rows.front().push_back(code);
    for (std::size_t row = 1; row < csv.rows.size(); ++row)
    {
        csv.rows[row].resize(csv.rows.front().size());
    }
    change(before);
    if (code.starts_with('_'))
    {
        // A column for another language: its code is typed into its header.
        startEdit(0, csv.rows.front().size() - 1);
    }
}

void TranslationsUi::fill(EditorUiKit& kit)
{
    const ThemeColors& colors = themeColors();
    const math::Vec4 missing = linearColor(math::Vec4(colors.warning.x, colors.warning.y, colors.warning.z, 0.16f));
    const math::Vec4 keyColumn = linearColor(math::Vec4(colors.outer.x, colors.outer.y, colors.outer.z, 0.5f));
    const math::Vec4 none{0.0f};

    // The header: the keys, then each language with what it misses.
    for (std::size_t column = 0; column < header.cells.size(); ++column)
    {
        const Cell& cell = header.cells[column];
        const std::string& name = cellOf(csv, 0, column);
        const bool language = isLanguageColumn(csv, column);
        std::string label = column == 0 ? std::string("Key") : language ? std::format("{} ({})", asset::languageName(name), name) : name;
        scene::UiText& shownLabel = scene().get<scene::UiText>(cell.label);
        if (shownLabel.text != label)
        {
            shownLabel.text = std::move(label);
        }
        scene().get<UiRect>(cell.label).style = language || column == 0 ? "text" : "dim";
        std::size_t count = 0;
        for (std::size_t row = 1; language && row < csv.rows.size(); ++row)
        {
            count += !cellOf(csv, row, 0).empty() && cellOf(csv, row, column).empty() ? 1 : 0;
        }
        scene::UiText& badge = scene().get<scene::UiText>(cell.badge);
        badge.text = count > 0 ? std::format("{}", count) : std::string{};
        tooltip(cell.entity, column == 0 ? "The keys: what the interfaces and the code write. Click a cell to change it."
                             : language  ? (count > 0 ? std::format("{} key{} not translated into {} yet. Click to change the code.", count,
                                                                   count == 1 ? " is" : "s are", asset::languageName(name))
                                                      : std::format("Everything is translated into {}. Click to change the code.",
                                                                    asset::languageName(name)))
                                         : "A column of notes, which the game leaves out");
        scene().get<scene::UiImage>(cell.entity).color = linearColor(colors.outer);
    }

    // The rows in view.
    for (std::size_t index = 0; index < rows.size(); ++index)
    {
        const Row& row = rows[index];
        const std::size_t at = first + index;
        const bool used = at < visible.size();
        scene().get<UiRect>(row.entity).visible = used;
        if (!used)
        {
            continue;
        }
        const std::size_t tableRow = visible[at];
        const bool keyed = !cellOf(csv, tableRow, 0).empty();
        for (std::size_t column = 0; column < row.cells.size(); ++column)
        {
            const Cell& cell = row.cells[column];
            const std::string& value = cellOf(csv, tableRow, column);
            const bool absent = keyed && value.empty() && isLanguageColumn(csv, column);
            scene::UiText& label = scene().get<scene::UiText>(cell.label);
            const std::string& written = value.empty() && column == 0 ? std::string("(no key)") : value;
            if (label.text != written)
            {
                label.text = written;
            }
            // A line break of a message shows as \n, as the file writes it.
            scene().get<UiRect>(cell.label).style = column == 0 ? (keyed ? "text" : "warning") : column > 0 && !isLanguageColumn(csv, column) ? "dim" : "text";
            scene().get<scene::UiImage>(cell.entity).color = absent ? missing : column == 0 ? keyColumn : none;
            const bool edited = editing && editing->row == tableRow && editing->column == column;
            scene().get<UiRect>(cell.field).visible = edited;
            scene().get<UiRect>(cell.label).visible = !edited;
        }
    }
    static_cast<void>(kit);
}

void TranslationsUi::update(ToolsState& state, EditorUiKit& kit, core::Duration delta)
{
    const ThemeColors& colors = themeColors();
    kit.refreshTheme(colors);
    if (!built || builtFont != state.theme.fontSize)
    {
        font = state.theme.fontSize;
        build(kit);
    }
    styleTooltips(colors);
    ui::UiWorld& world = panel.world();

    // The table: the one asked for, the one chosen in the FileSystem, or the first of the project.
    listTables(state);
    if (state.selectedAsset != lastSelected)
    {
        lastSelected = state.selectedAsset;
        if (std::ranges::find(tables, state.selectedAsset) != tables.end())
        {
            state.translationTable = state.selectedAsset;
        }
    }
    if (std::ranges::find(tables, state.translationTable) == tables.end())
    {
        state.translationTable = tables.empty() ? asset::AssetId{} : tables.front();
    }
    if (state.translationTable != shown || (shown.isValid() && file.empty()))
    {
        load(state, state.translationTable);
    }
    reloadIfChanged(state);
    {
        // The list stays as it is while it is open.
        scene::UiDropdown& dropdown = scene().get<scene::UiDropdown>(tableChoice);
        if (dropdown.options != tableNames && world.listedDropdown() != tableChoice)
        {
            dropdown.options = tableNames;
        }
        const auto chosen = std::ranges::find(tables, shown);
        dropdown.selected = chosen != tables.end() ? static_cast<std::int32_t>(chosen - tables.begin()) : -1;
        dropdown.placeholder = "No table";
    }

    // As many cells a row as the table has columns.
    if (builtColumns != columns())
    {
        destroyRows();
        builtColumns = columns();
        if (builtColumns > 0)
        {
            header = makeRow(kit, table, true);
        }
    }
    filter(state);

    // The columns share the width: the keys a little more than each language.
    const float width = std::max(panel.size().x - font * 1.6f, font * 10.0f);
    scene::UiTable& layout = scene().get<scene::UiTable>(table);
    layout.columns.assign(builtColumns, 0.0f);
    if (builtColumns > 0)
    {
        const float keys = builtColumns == 1 ? width : std::max(std::round(width * 0.28f), font * 9.0f);
        layout.columns.front() = keys;
        for (std::size_t column = 1; column < builtColumns; ++column)
        {
            layout.columns[column] = std::floor((width - keys) / static_cast<float>(builtColumns - 1));
        }
    }

    // As many rows as the view holds, for the rows the scroll leaves in view.
    const float tall = rowHeight();
    scene::UiVirtualList& virtualList = scene().get<scene::UiVirtualList>(lines);
    virtualList.itemCount = static_cast<std::uint32_t>(visible.size());
    virtualList.itemSize = tall;
    scene::UiScroll& scrolled = scene().get<scene::UiScroll>(scroll);
    scrolled.speed = tall * 3.0f;
    const ui::LaidOutRect* const view = world.canvases().empty() ? nullptr : world.canvases().front().layout.find(scroll);
    const float viewHeight = view != nullptr ? view->size().y : 0.0f;
    if (revealRow)
    {
        if (const auto found = std::ranges::find(visible, *revealRow); found != visible.end() && viewHeight > 0.0f)
        {
            const float top = static_cast<float>(found - visible.begin()) * tall;
            scrolled.offset.y = std::clamp(scrolled.offset.y, top + tall - viewHeight, top);
        }
        revealRow.reset();
    }
    const std::size_t needed = builtColumns == 0 ? 0 : static_cast<std::size_t>(std::ceil(std::max(viewHeight, 200.0f) / tall)) + 2;
    while (rows.size() < needed)
    {
        rows.push_back(makeRow(kit, lines, false));
    }
    first = visible.empty() ? 0 : std::min(static_cast<std::size_t>(std::max(scrolled.offset.y, 0.0f) / tall), visible.size() - 1);

    // The cell to type into, once its row is in view.
    if (pendingEdit)
    {
        const auto [row, column] = *pendingEdit;
        const Cell* cell = row == 0 && column < header.cells.size() ? &header.cells[column] : nullptr;
        for (std::size_t index = 0; cell == nullptr && index < rows.size(); ++index)
        {
            if (first + index < visible.size() && visible[first + index] == row && column < rows[index].cells.size())
            {
                cell = &rows[index].cells[column];
            }
        }
        if (cell != nullptr)
        {
            scene().get<scene::UiText>(cell->field).text = cellOf(csv, row, column);
            scene().get<UiRect>(cell->field).visible = true;
            world.startEditing(scene(), cell->field, true);
            editing = Editing{.field = cell->field, .row = row, .column = column};
            pendingEdit.reset();
        }
    }

    fill(kit);
    const bool hasTable = shown.isValid() && error.empty();
    enable(keyButton, hasTable && builtColumns > 0);
    enable(languageButton, hasTable && builtColumns > 0);
    enable(missingButton, hasTable);
    scene().get<UiRect>(missingButton.entity).style = onlyMissing ? "button" : "flat";
    scene().get<UiRect>(header.entity.isValid() ? header.entity : table).visible = true;
    scene().get<UiRect>(list).visible = hasTable;
    scene::UiText& said = scene().get<scene::UiText>(note);
    const std::string message = tables.empty() ? "No table of translations yet. New Table makes one: keys down, languages across, in a .csv "
                                                 "file that spreadsheets open too. The texts of the interfaces whose text is a key show its "
                                                 "translation, and Localization.Tr gives it to the code."
                                : !error.empty() ? std::format("This table cannot be read: {}", error)
                                : visible.empty() && csv.rows.size() > 1 ? "No key matches the filter."
                                : visible.empty() ? "No key yet: + Key adds one."
                                                  : std::string{};
    if (said.text != message)
    {
        said.text = message;
    }
    scene().get<UiRect>(note).visible = !message.empty();

    panel.update(kit, delta, UiPanel::zoomFor(font));

    // The field typed into: its edit ends with Enter, which goes on to the cell below, a click
    // elsewhere, or Escape, which drops what was typed.
    const ui::UiInput& input = panel.input();
    if (editing && world.editedField() != editing->field)
    {
        const Editing ended = *editing;
        editing.reset();
        scene().get<UiRect>(ended.field).visible = false;
        if (!input.cancelPressed && !state.input.pressed(platform::Key::Escape, false))
        {
            commit(state, ended);
            if (world.wasSubmitted(ended.field) && ended.row > 0)
            {
                if (const auto found = std::ranges::find(visible, ended.row); found != visible.end() && found + 1 != visible.end())
                {
                    startEdit(*(found + 1), ended.column);
                }
            }
        }
    }
    else if (const Entity typed = world.editedField(); !editing && typed.isValid())
    {
        // A field took the keyboard without being asked: the one of a cell clicked into.
        if (const std::optional<std::pair<std::size_t, std::size_t>> cell = cellAt(typed); cell && scene().has<scene::UiInput>(typed))
        {
            editing = Editing{.field = typed, .row = cell->first, .column = cell->second};
        }
    }

    if (world.wasChanged(tableChoice))
    {
        const std::int32_t index = scene().get<scene::UiDropdown>(tableChoice).selected;
        if (index >= 0 && static_cast<std::size_t>(index) < tables.size())
        {
            state.translationTable = tables[static_cast<std::size_t>(index)];
        }
    }
    if (world.wasClicked(newTable.entity))
    {
        if (core::Result<std::filesystem::path> created = createTranslationFile(state, "res://assets"); !created)
        {
            DEVEX_LOG_ERROR("Cannot create a table of translations: {}", created.error());
        }
    }
    if (world.wasClicked(keyButton.entity))
    {
        addKey(state);
    }
    if (world.wasClicked(missingButton.entity))
    {
        onlyMissing = !onlyMissing;
    }
    if (world.wasClicked(languageButton.entity))
    {
        // The usual languages the table has no column for, then another one, or notes.
        std::vector<MenuEntry> entries;
        for (const std::string_view code : asset::commonLanguages())
        {
            if (std::ranges::find(csv.rows.front(), code) != csv.rows.front().end())
            {
                continue;
            }
            entries.push_back({.label = std::format("{} ({})", asset::languageName(code), code),
                               .action = [code = std::string(code)](ToolsState& tools, scene::Scene&) {
                                   if (tools.translationsUi)
                                   {
                                       tools.translationsUi->addColumn(code);
                                   }
                               }});
        }
        entries.push_back(MenuEntry::line());
        entries.push_back({.label = "Other Language...", .action = [](ToolsState& tools, scene::Scene&) {
                               if (tools.translationsUi)
                               {
                                   tools.translationsUi->addColumn("_language");
                               }
                           }});
        entries.push_back({.label = "Notes", .action = [](ToolsState& tools, scene::Scene&) {
                               if (tools.translationsUi)
                               {
                                   tools.translationsUi->addColumn("_notes");
                               }
                           }});
        openEditorMenu(state, std::move(entries), state.input.mouse(), languageMenuOwner);
    }

    // A click on a cell types into it.
    for (std::size_t column = 0; column < header.cells.size(); ++column)
    {
        if (column > 0 && world.wasClicked(header.cells[column].entity))
        {
            startEdit(0, column);
        }
    }
    for (std::size_t index = 0; index < rows.size(); ++index)
    {
        for (std::size_t column = 0; column < rows[index].cells.size(); ++column)
        {
            if (first + index < visible.size() && world.wasClicked(rows[index].cells[column].entity))
            {
                startEdit(visible[first + index], column);
            }
        }
    }

    // The menus of a row and of a column.
    const std::optional<std::pair<std::size_t, std::size_t>> target = cellAt(world.contextTarget());
    const auto moveRow = [&](int step) {
        if (!target || target->first == 0)
        {
            return;
        }
        const std::size_t from = target->first;
        const std::size_t to = step < 0 ? from - 1 : from + 1;
        if (to < 1 || to >= csv.rows.size())
        {
            return;
        }
        const asset::CsvTable before = csv;
        std::swap(csv.rows[from], csv.rows[to]);
        change(before);
    };
    if (world.wasClicked(menuUp.entity))
    {
        moveRow(-1);
    }
    if (world.wasClicked(menuDown.entity))
    {
        moveRow(1);
    }
    if (world.wasClicked(menuDelete.entity) && target && target->first > 0 && target->first < csv.rows.size())
    {
        const asset::CsvTable before = csv;
        csv.rows.erase(csv.rows.begin() + static_cast<std::ptrdiff_t>(target->first));
        change(before);
    }
    if (world.wasClicked(menuRemoveColumn.entity) && target && target->first == 0 && target->second > 0 && target->second < columns())
    {
        const asset::CsvTable before = csv;
        for (std::vector<std::string>& row : csv.rows)
        {
            if (target->second < row.size())
            {
                row.erase(row.begin() + static_cast<std::ptrdiff_t>(target->second));
            }
        }
        change(before);
    }

    // The history of the table, while no field takes the keys.
    if (panel.focused() && !world.isEditing())
    {
        if (state.input.chord(KeyModifiers{.ctrl = true}, 'z') && !undo.empty())
        {
            redo.push_back(std::exchange(csv, std::move(undo.back())));
            undo.pop_back();
            save();
        }
        else if ((state.input.chord(KeyModifiers{.ctrl = true}, 'y') || state.input.chord(KeyModifiers{.ctrl = true, .shift = true}, 'z')) &&
                 !redo.empty())
        {
            undo.push_back(std::exchange(csv, std::move(redo.back())));
            redo.pop_back();
            save();
        }
    }
}

namespace {

constexpr std::array<std::string_view, 4> delimiters{"auto", "comma", "semicolon", "tab"};

// A table of translations in the inspector: what it holds, what each language misses, how its file
// is read, and the panel that edits it.
class TranslationPage final : public InspectorPage
{
public:
    std::string signature(ToolsState& state) override
    {
        // Read again at most twice a second, so that a new import shows.
        if (state.clock - m_readAt >= 0.5 || m_readFor != state.selectedAsset)
        {
            m_readAt = state.clock;
            m_readFor = state.selectedAsset;
            const core::Result<std::vector<std::byte>> bytes = state.database->loadArtifact(state.selectedAsset);
            const core::Result<asset::TranslationData> table =
                bytes ? asset::decodeTranslation(*bytes) : core::Result<asset::TranslationData>(std::unexpected(bytes.error()));
            m_table = table ? *table : asset::TranslationData{};
        }
        const std::optional<asset::SourceFile> source = state.database->sourceOf(state.selectedAsset);
        std::string signature = std::format("{}|{}", source ? static_cast<int>(source->status) : -1, m_table.keys.size());
        for (std::size_t language = 0; language < m_table.languages.size(); ++language)
        {
            std::format_to(std::back_inserter(signature), "|{}:{}", m_table.languages[language], missing(language));
        }
        return signature;
    }

    void build(InspectorUi& ui, ToolsState& state, EditorUiKit& kit) override
    {
        const asset::AssetInfo* const info = state.database->find(state.selectedAsset);
        const std::optional<asset::SourceFile> source = state.database->sourceOf(state.selectedAsset);
        ui.heading(kit, icons::Languages, themeColors().scene, info->name, source ? source->path : std::string{});
        const Entity row = ui.actions(nullptr);
        m_open = ui.action(kit, row, Icon::Languages, "Edit in Translations");
        m_reimport = ui.action(kit, row, Icon::Refresh, "Reimport");
        if (source && source->status == asset::ImportStatus::Failed)
        {
            ui.note(nullptr, "The table could not be imported.", "error");
            ui.note(nullptr, source->error, "dim", 3.0f);
        }

        Section& contents = ui.card(kit, "Translations");
        ui.note(&contents, std::format("{} key{} in {} language{}", m_table.keys.size(), m_table.keys.size() == 1 ? "" : "s",
                                       m_table.languages.size(), m_table.languages.size() == 1 ? "" : "s"));
        for (std::size_t language = 0; language < m_table.languages.size(); ++language)
        {
            const std::size_t count = missing(language);
            ui.note(&contents,
                    count == 0 ? std::format("{} ({}): complete", asset::languageName(m_table.languages[language]), m_table.languages[language])
                               : std::format("{} ({}): {} missing", asset::languageName(m_table.languages[language]), m_table.languages[language], count),
                    count == 0 ? "dim" : "warning");
        }
        ui.note(&contents, "The texts of the interfaces whose text is a key show its translation; Localization.Tr gives it to C#.", "dim", 2.0f);

        Section& import = ui.card(kit, "Import");
        const FormRow delimiterRow = ui.formRow(import, "Delimiter");
        m_delimiter = ui.choice(delimiterRow.editor, {"Auto", "Comma", "Semicolon", "Tab"});
        ui.tooltip(delimiterRow.row, "What separates the cells: Auto finds it from the first line; spreadsheets write semicolons where the comma is the decimal mark");
    }

    void sync(InspectorUi& ui, ToolsState& state, EditorUiKit&) override
    {
        const std::optional<serialization::TextValue> option = state.database->importOption(state.selectedAsset, "delimiter");
        const std::string* const text = option ? serialization::asString(*option) : nullptr;
        const auto found = std::ranges::find(delimiters, text != nullptr ? std::string_view(*text) : std::string_view("auto"));
        ui.setChoice(m_delimiter, {"Auto", "Comma", "Semicolon", "Tab"},
                     found != delimiters.end() ? static_cast<std::int32_t>(found - delimiters.begin()) : 0);
    }

    void answer(InspectorUi& ui, ToolsState& state, EditorUiKit&) override
    {
        const ui::UiWorld& world = ui.panel.world();
        if (world.wasClicked(m_open.entity))
        {
            openTranslationTable(state, state.selectedAsset);
        }
        if (world.wasClicked(m_reimport.entity))
        {
            if (core::Result<void> queued = state.database->reimport(state.selectedAsset); !queued)
            {
                DEVEX_LOG_WARNING("{}", queued.error());
            }
        }
        if (world.wasChanged(m_delimiter))
        {
            const std::int32_t index = ui.scene().get<scene::UiDropdown>(m_delimiter).selected;
            if (index >= 0 && static_cast<std::size_t>(index) < delimiters.size())
            {
                if (core::Result<void> set = state.database->setImportOption(
                        state.selectedAsset, "delimiter", serialization::TextValue(std::string(delimiters[static_cast<std::size_t>(index)])));
                    !set)
                {
                    DEVEX_LOG_WARNING("{}", set.error());
                }
            }
        }
    }

private:
    [[nodiscard]] std::size_t missing(std::size_t language) const
    {
        std::size_t count = 0;
        for (std::size_t key = 0; key < m_table.keys.size(); ++key)
        {
            count += m_table.message(key, language).empty() ? 1 : 0;
        }
        return count;
    }

    asset::TranslationData m_table;
    double m_readAt = -1.0;
    asset::AssetId m_readFor;
    Button m_open;
    Button m_reimport;
    Entity m_delimiter;
};

} // namespace

std::unique_ptr<InspectorPage> makeTranslationPage()
{
    return std::make_unique<TranslationPage>();
}

std::vector<std::string> projectLanguages(ToolsState& state)
{
    if (state.database == nullptr)
    {
        return {};
    }
    // Read again at most once a second: the windows that list them do so at every frame.
    if (state.projectLanguagesTime >= 0.0 && state.clock - state.projectLanguagesTime < 1.0)
    {
        return state.projectLanguages;
    }
    state.projectLanguagesTime = state.clock;
    std::vector<std::string> languages;
    for (const asset::AssetInfo& info : state.database->assets(asset::AssetType::Translation))
    {
        const core::Result<std::vector<std::byte>> bytes = state.database->loadArtifact(info.id);
        const core::Result<asset::TranslationData> table =
            bytes ? asset::decodeTranslation(*bytes) : core::Result<asset::TranslationData>(std::unexpected(bytes.error()));
        if (!table)
        {
            continue;
        }
        for (const std::string& language : table->languages)
        {
            if (std::ranges::find(languages, language) == languages.end())
            {
                languages.push_back(language);
            }
        }
    }
    std::ranges::sort(languages);
    state.projectLanguages = languages;
    return languages;
}

core::Result<std::filesystem::path> createTranslationFile(ToolsState& state, std::string_view folder, std::string_view name)
{
    const std::string fallback = state.database != nullptr ? state.database->project().localization.fallbackLanguage : std::string("en");
    core::Result<std::filesystem::path> created =
        writeNewAssetFile(state, folder, name, "Translations", asset::translationExtension, std::format("keys,{}\n", fallback));
    if (created)
    {
        state.showTranslations = true;
        state.focusTranslations = true;
    }
    return created;
}

void openTranslationTable(ToolsState& state, asset::AssetId table)
{
    state.translationTable = table;
    state.showTranslations = true;
    state.focusTranslations = true;
}

void drawTranslationsPanel(ToolsState& state)
{
    DEVEX_PROFILE_SCOPE("Translations panel");
    if (!state.showTranslations || state.database == nullptr)
    {
        return;
    }
    if (std::exchange(state.focusTranslations, false))
    {
        focusPanel(state, translationsWindow);
    }
    if (!beginDockedPanel(state, translationsWindow))
    {
        return;
    }
    EditorUiKit& kit = editorUiKit(state);
    if (!state.translationsUi)
    {
        state.translationsUi = std::make_shared<TranslationsUi>();
    }
    state.translationsUi->update(state, kit, core::Duration(state.input.delta()));
    endDockedPanel(state);
}

void renderTranslationsPanel(ToolsState& state, render::RenderWorld& world)
{
    if (state.translationsUi && state.uiKit)
    {
        state.translationsUi->panel.render(*state.uiKit, world, linearColor(themeColors().panel));
    }
}

} // namespace devex::tools::detail
