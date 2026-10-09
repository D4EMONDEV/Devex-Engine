// The Script screen, made with the interface of the engine: the files that are open and their text,
// which an area of text of that interface edits.
#include "EditorFrame.hpp"
#include "FormUi.hpp"
#include "SettingsUi.hpp"
#include "ToolsState.hpp"

#include "CodeArea.hpp"
#include "CodeCompletion.hpp"
#include "CodeHighlight.hpp"
#include "CodeOutline.hpp"

#include <devex/core/Profiler.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace devex::tools::detail {

TextDocument* findTextDocument(ToolsState& state, const std::filesystem::path& path)
{
    for (TextDocument& document : state.textDocuments)
    {
        if (sameTextPath(document.path, path))
        {
            return &document;
        }
    }
    return nullptr;
}

std::vector<TextDocument*> affectedTextDocuments(ToolsState& state, const PendingAction& action)
{
    std::vector<TextDocument*> documents;
    if (action.kind == PendingAction::Kind::CloseTab)
    {
        return documents;
    }
    for (TextDocument& document : state.textDocuments)
    {
        const bool concerned = (action.kind != PendingAction::Kind::CloseText && action.kind != PendingAction::Kind::ReloadText) ||
                               sameTextPath(document.path, action.path);
        if (concerned && document.modified())
        {
            documents.push_back(&document);
        }
    }
    return documents;
}

void openTextFile(ToolsState& state, const std::filesystem::path& path)
{
    // The game overlay has no editor session or unsaved-document confirmation on exit.
    if (state.mode != ToolsMode::Editor)
    {
        openInCodeEditor(state, path);
        return;
    }
    setMainScreen(state, MainScreen::Script);
    state.focusTextEditor = true;
    state.textOpenError.clear();
    TextDocument* document = findTextDocument(state, path);
    if (document == nullptr)
    {
        auto opened = TextDocument::open(path);
        if (!opened)
        {
            state.textOpenError = opened.error().message;
            DEVEX_LOG_WARNING("Cannot open text file: {}", opened.error());
            return;
        }
        document = &state.textDocuments.emplace_back(std::move(*opened));
    }
    state.activeText = document->path;
    state.selectTextTab = true;
}

void showOpenTextDialog(ToolsState& state)
{
    const std::weak_ptr<DialogAnswers> answers = state.dialogAnswers;
    state.platform.showFileDialog(state.window,
        {.type = platform::FileDialogType::OpenFile,
         .filters = {{"Text and code", "cpp;h;hpp;c;cs;csproj;txt;md;json;glsl;vert;frag;cmake;dvxscene;dvxmat;dvxproj;dvxmeta;dvxprefab"},
                     {"All files", "*"}},
         .defaultLocation = state.database != nullptr ? state.database->project().root : std::filesystem::path{}},
        [answers](std::optional<std::filesystem::path> chosen) {
            if (const auto inbox = answers.lock(); inbox && chosen)
            {
                inbox->openText = std::move(*chosen);
            }
        });
}

bool textEditorFocused(const ToolsState& state)
{
    return state.hosts.isFocused(textEditorWindow);
}

// The line a byte offset falls on, found in the index rather than by counting again.
int lineOfOffsetIndexed(const TextEditState& edit, int offset)
{
    const auto next = std::ranges::upper_bound(edit.lineStarts, offset);
    return static_cast<int>(next - edit.lineStarts.begin());
}

namespace {

constexpr std::uint32_t scriptSurface = 20;
// What opened a menu of the Script screen, among what opens the menus of the layer over the editor:
// after the titles of the menu bar.
constexpr std::size_t scriptMenuOwner = 100;
constexpr std::size_t completionsShown = 10;

[[nodiscard]] math::Vec4 colorOf(TokenKind kind)
{
    const ThemeColors& colors = themeColors();
    switch (kind)
    {
    case TokenKind::Keyword:
        return colors.codeKeyword;
    case TokenKind::Type:
        return colors.codeType;
    case TokenKind::Comment:
        return colors.codeComment;
    case TokenKind::String:
        return colors.codeString;
    case TokenKind::Number:
        return colors.codeNumber;
    case TokenKind::Directive:
        return colors.codeDirective;
    case TokenKind::Punctuation:
        return colors.codePunctuation;
    case TokenKind::Text:
        break;
    }
    return colors.text;
}

// Where every line starts, and whether it begins inside a block comment: rebuilt after each edit
// so that only the visible lines are colored.
void indexDocument(TextEditState& edit, const TextDocument& document)
{
    edit.lineStarts.clear();
    edit.lineInComment.clear();
    bool inComment = false;
    std::size_t index = 0;
    const std::string& text = document.text;
    while (true)
    {
        edit.lineStarts.push_back(static_cast<int>(index));
        edit.lineInComment.push_back(inComment);
        const std::size_t stop = text.find('\n', index);
        const std::string_view line(text.data() + index, (stop == std::string::npos ? text.size() : stop) - index);
        // A light scan, enough to know where the next line starts inside a comment.
        for (std::size_t scan = 0; scan + 1 < line.size(); ++scan)
        {
            if (inComment && line[scan] == '*' && line[scan + 1] == '/')
            {
                inComment = false;
                ++scan;
            }
            else if (!inComment && line[scan] == '/' && line[scan + 1] == '*')
            {
                inComment = true;
                ++scan;
            }
            else if (!inComment && line[scan] == '/' && line[scan + 1] == '/')
            {
                break;
            }
        }
        if (stop == std::string::npos)
        {
            break;
        }
        index = stop + 1;
    }
    edit.indexedLength = static_cast<int>(text.size());
    edit.outline = outlineOf(document.text, languageOf(document.path));
}

// ---- The menus above the text ----

[[nodiscard]] TextDocument* shownDocument(ToolsState& state)
{
    return findTextDocument(state, state.activeText);
}

[[nodiscard]] std::vector<MenuEntry> fileMenu(ToolsState& state)
{
    const TextDocument* const document = shownDocument(state);
    const bool any = std::ranges::any_of(state.textDocuments, [](const TextDocument& open) { return open.modified(); });
    std::vector<MenuEntry> entries;
    entries.push_back({.icon = Icon::FilePlus, .label = "New Script...", .action = [](ToolsState& tools, scene::Scene&) { requestNewScript(tools); }});
    entries.push_back({.icon = Icon::FolderOpen, .label = "Open...", .shortcut = "Ctrl+O",
                       .action = [](ToolsState& tools, scene::Scene&) { showOpenTextDialog(tools); }});
    entries.push_back(MenuEntry::line());
    entries.push_back({.icon = Icon::Save, .label = "Save", .shortcut = "Ctrl+S", .enabled = document != nullptr && document->modified(),
                       .action = [](ToolsState& tools, scene::Scene& edited) {
                           if (TextDocument* const open = shownDocument(tools))
                           {
                               static_cast<void>(saveTextFile(tools, edited, *open));
                           }
                       }});
    entries.push_back({.label = "Save All", .enabled = any, .action = [](ToolsState& tools, scene::Scene& edited) {
                           for (TextDocument& open : tools.textDocuments)
                           {
                               if (open.modified())
                               {
                                   static_cast<void>(saveTextFile(tools, edited, open));
                               }
                           }
                       }});
    entries.push_back({.icon = Icon::Refresh, .label = "Reload from Disk", .enabled = document != nullptr,
                       .action = [](ToolsState& tools, scene::Scene& edited) {
                           requestAction(tools, edited, {.kind = PendingAction::Kind::ReloadText, .path = tools.activeText});
                       }});
    entries.push_back(MenuEntry::line());
    entries.push_back({.icon = Icon::ExternalLink, .label = "Open in External Editor", .enabled = document != nullptr,
                       .action = [](ToolsState& tools, scene::Scene&) { openInCodeEditor(tools, tools.activeText); }});
    entries.push_back(MenuEntry::line());
    entries.push_back({.icon = Icon::Close, .label = "Close", .shortcut = "Ctrl+W", .enabled = document != nullptr,
                       .action = [](ToolsState& tools, scene::Scene& edited) {
                           requestAction(tools, edited, {.kind = PendingAction::Kind::CloseText, .path = tools.activeText});
                       }});
    return entries;
}

[[nodiscard]] std::vector<MenuEntry> textEditMenu(ToolsState& state, bool canUndo, bool canRedo)
{
    const TextDocument* const document = shownDocument(state);
    const bool comments = document != nullptr && !lineCommentOf(languageOf(document->path)).empty();
    std::vector<MenuEntry> entries;
    entries.push_back({.icon = Icon::Undo, .label = "Undo", .shortcut = "Ctrl+Z", .enabled = canUndo,
                       .action = [](ToolsState& tools, scene::Scene&) { tools.textEdit.requestUndo = true; }});
    entries.push_back({.icon = Icon::Redo, .label = "Redo", .shortcut = "Ctrl+Y", .enabled = canRedo,
                       .action = [](ToolsState& tools, scene::Scene&) { tools.textEdit.requestRedo = true; }});
    entries.push_back(MenuEntry::line());
    entries.push_back({.label = "Toggle Comment", .shortcut = "Ctrl+K", .enabled = comments, .action = [](ToolsState& tools, scene::Scene&) {
                           if (const TextDocument* const open = shownDocument(tools))
                           {
                               commentSelection(tools.textEdit, *open, languageOf(open->path));
                           }
                       }});
    return entries;
}

[[nodiscard]] std::vector<MenuEntry> searchMenu(ToolsState& state)
{
    const bool open = shownDocument(state) != nullptr;
    const bool found = !state.textEdit.matches.empty();
    std::vector<MenuEntry> entries;
    entries.push_back({.icon = Icon::Search, .label = "Find...", .shortcut = "Ctrl+F", .enabled = open, .action = [](ToolsState& tools, scene::Scene&) {
                           tools.textEdit.showFind = true;
                           tools.textEdit.focusFind = true;
                       }});
    entries.push_back({.label = "Replace...", .shortcut = "Ctrl+H", .enabled = open, .action = [](ToolsState& tools, scene::Scene&) {
                           tools.textEdit.showFind = true;
                           tools.textEdit.showReplace = true;
                           tools.textEdit.focusFind = true;
                       }});
    const auto step = [](int direction) {
        return [direction](ToolsState& tools, scene::Scene&) {
            if (const TextDocument* const document = shownDocument(tools))
            {
                selectMatch(tools.textEdit, *document, direction);
            }
        };
    };
    entries.push_back({.label = "Find Next", .shortcut = "F3", .enabled = found, .action = step(1)});
    entries.push_back({.label = "Find Previous", .shortcut = "Shift+F3", .enabled = found, .action = step(-1)});
    entries.push_back(MenuEntry::line());
    entries.push_back({.icon = Icon::ArrowDownToLine, .label = "Go to Line...", .shortcut = "Ctrl+G", .enabled = open,
                       .action = [](ToolsState& tools, scene::Scene&) { tools.textEdit.openGoTo = true; }});
    return entries;
}

} // namespace

using scene::Entity;
using scene::UiRect;
using namespace rects;
using Button = PanelButton;

// The Script screen, as the script editor of Godot is laid out: the files that are open and what the
// one shown declares at the left, its text at the right under the menus, the search and the line the
// cursor is on.
struct ScriptUi : FormUi
{
    ScriptUi()
        : FormUi(scriptSurface)
    {
    }

    // A file that is open: its area of text keeps its cursor, its scrolling and what it can go back
    // on for as long as the file stays open.
    struct OpenText
    {
        std::filesystem::path path;
        Entity area;
        unsigned revision = 0;
    };
    struct FileRow
    {
        Entity row;
        Entity close;
        std::filesystem::path path;
    };
    struct SymbolRow
    {
        Entity row;
        int line = 1;
    };
    struct CompletionRow
    {
        Entity row;
        Entity name;
        Entity detail;
    };
    // The keys the list of completions takes from the text while it is open, and Escape.
    struct Keys
    {
        int move = 0;
        bool accept = false;
        bool cancel = false;
    };

    bool built = false;
    float builtFont = 0.0f;
    float builtCode = 0.0f;
    float codeSize = 13.0f;

    Entity bar;
    std::array<Button, 3> menus{};
    Entity pathText;

    Entity body;
    Entity filesFilter;
    Entity filesRows;
    Entity symbolsFilter;
    Entity symbolsRows;
    std::vector<FileRow> fileRows;
    std::vector<SymbolRow> symbolRows;
    std::string filesKey;
    std::string symbolsKey;

    Entity findRow;
    Entity findField;
    Button findNext;
    Button findPrevious;
    Entity matchCase;
    Entity findCount;
    Button findClose;
    Entity replaceRow;
    Entity replaceField;
    Button replaceOne;
    Button replaceEvery;
    Entity goRow;
    Entity goField;
    Entity goCount;
    Button goButton;
    Button goClose;
    bool going = false;

    Entity areas;
    std::vector<OpenText> open;
    Entity status;
    Entity message;
    Entity completions;
    std::array<CompletionRow, completionsShown> completionRows{};
    std::size_t firstCompletion = 0;
    // The words of the lines in view, found once and kept while a line says the same thing: the
    // lines scrolled into view are the only ones read again.
    struct ColoredLine
    {
        std::string text;
        bool inComment = false;
        CodeLanguage language = CodeLanguage::PlainText;
        std::vector<Token> tokens;
        bool used = false;
    };
    std::unordered_map<std::size_t, ColoredLine> coloredLines;
    Entity coloredArea;
    Keys keys;
    // The file shown at the last update, and whether its text takes the keyboard at the next one.
    std::filesystem::path shownPath;
    bool focusText = false;

    [[nodiscard]] float barHeight() const noexcept
    {
        return std::round(font * 2.2f);
    }
    [[nodiscard]] float rowHeight() const noexcept
    {
        return std::round(font * 1.9f);
    }

    void build(EditorUiKit& kit);
    [[nodiscard]] OpenText* openOf(const std::filesystem::path& path);
    void syncAreas(ToolsState& state);
    void fillFiles(ToolsState& state, EditorUiKit& kit);
    void fillSymbols(ToolsState& state, EditorUiKit& kit);
    void colorLines(ToolsState& state, const TextDocument& document, Entity area);
    void showCompletions(ToolsState& state, Entity area, bool focused);
    std::optional<PendingAction> update(ToolsState& state, EditorUiKit& kit, core::Duration delta);
};

void ScriptUi::build(EditorUiKit& kit)
{
    ui::UiWorld& world = panel.world();
    std::vector<Entity> children;
    for (Entity child = scene().firstChild(panel.canvas()); child.isValid(); child = scene().nextSibling(child))
    {
        children.push_back(child);
    }
    for (const Entity child : children)
    {
        world.closePopup(scene(), child);
        scene().destroyEntity(child);
    }
    built = true;
    builtFont = font;
    builtCode = codeSize;
    open.clear();
    fileRows.clear();
    symbolRows.clear();
    filesKey.clear();
    symbolsKey.clear();
    shownPath.clear();
    panel.setKeyboardNavigation(false);
    // While its list of completions is open, the arrows, Enter and Tab choose in it rather than move
    // and write in the text; Escape closes it, or the search.
    panel.setInputFilter([this](ui::UiInput& input) {
        keys = Keys{};
        if (scene().isAlive(completions) && scene().get<UiRect>(completions).visible)
        {
            keys.move = (input.downPressed ? 1 : 0) - (input.upPressed ? 1 : 0);
            keys.accept = input.submitPressed || input.tabPressed;
            input.upPressed = false;
            input.downPressed = false;
            input.submitPressed = false;
            input.tabPressed = false;
        }
        keys.cancel = input.cancelPressed;
        input.cancelPressed = false;
    });

    const float edge = std::round(font * 0.5f);
    const float tall = rowHeight();
    const float inner = tall - 4.0f;

    // The menus, and the file shown at their right.
    bar = add({}, "Menus", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 0.0f}, .offsetMin = {edge, 0.0f}, .offsetMax = {-edge, barHeight()}});
    scene().add<scene::UiLayout>(bar, scene::UiLayout{.kind = scene::UiLayoutKind::Row, .spacing = 2.0f, .align = scene::TextAlign::Left});
    const std::array<const char*, 3> titles{"File", "Edit", "Search"};
    for (std::size_t index = 0; index < titles.size(); ++index)
    {
        menus[index] = button(kit, bar, std::nullopt, titles[index], "bar_button", 0.0f, inner);
    }
    add(bar, "Gap", middle({font, 1.0f}));
    pathText = text(bar, grow(tall), "", "dim");

    // The files and what the one shown declares at the left of a bar that is dragged, the text at its right.
    body = add({}, "Body", whole(math::Vec4{0.0f, barHeight(), 0.0f, 0.0f}));
    scene().add<scene::UiSplitter>(body, scene::UiSplitter{.position = std::round(font * 14.0f), .minSize = std::round(font * 8.0f)});
    const Entity side = add(body, "Side", whole());
    scene().add<scene::UiSplitter>(side, scene::UiSplitter{.vertical = true, .position = std::round(font * 16.0f), .minSize = std::round(font * 6.0f)});
    const auto list = [&](const char* name, const char* placeholder, Entity& filter, Entity& rows) {
        const Entity box = add(side, name, whole(), "list");
        scene().add<scene::UiImage>(box);
        filter = searchField(kit, box, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 0.0f}, .offsetMin = {4.0f, 4.0f}, .offsetMax = {-4.0f, 4.0f + inner}}, "",
                             placeholder);
        const Entity scrolled = add(box, "List",
                                    UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {0.0f, inner + 8.0f}, .offsetMax = {0.0f, -2.0f}, .clipChildren = true},
                                    "scroll");
        scene().add<scene::UiScroll>(scrolled, scene::UiScroll{});
        rows = add(scrolled, "Rows", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 0.0f}, .offsetMin = {4.0f, 0.0f}, .offsetMax = {-10.0f, 1.0f}});
        scene().add<scene::UiLayout>(rows, scene::UiLayout{.kind = scene::UiLayoutKind::Column, .spacing = 1.0f, .align = scene::TextAlign::Left});
    };
    list("Files", "Filter Scripts", filesFilter, filesRows);
    list("Symbols", "Filter Methods", symbolsFilter, symbolsRows);

    const Entity main = add(body, "Text", whole());
    const auto strip = [&](const char* name) {
        const Entity row = add(main, name, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 0.0f}, .offsetMin = {edge, 0.0f}, .offsetMax = {-edge, tall}, .visible = false});
        scene().add<scene::UiLayout>(row, scene::UiLayout{.kind = scene::UiLayoutKind::Row, .spacing = font * 0.4f, .align = scene::TextAlign::Left});
        return row;
    };
    // Find and replace, above the text: Ctrl+F opens it, Ctrl+H adds the replacement.
    findRow = strip("Find");
    findField = field(findRow, middle({font * 16.0f, inner}), "", "Find");
    findNext = button(kit, findRow, Icon::ChevronDown, "Next", "button", 0.0f, inner);
    findPrevious = button(kit, findRow, Icon::ChevronUp, "Previous", "button", 0.0f, inner);
    const float box = std::round(font * 1.3f);
    matchCase = add(findRow, "Toggle", middle({box, box}), "toggle");
    scene().add<scene::UiImage>(matchCase);
    scene().add<scene::UiToggle>(matchCase);
    scene().add<scene::UiButton>(matchCase);
    tooltip(matchCase, "Match case");
    text(findRow, middle({std::ceil(kit.textWidth(EditorUiKit::regularFont(), "Aa", font)) + 2.0f, tall}), "Aa", "text");
    findCount = text(findRow, grow(tall), "", "dim");
    findClose = toolButton(kit, findRow, Icon::Close, middle({inner, inner}));
    replaceRow = strip("Replace");
    replaceField = field(replaceRow, middle({font * 16.0f, inner}), "", "Replace with");
    replaceOne = button(kit, replaceRow, Icon::Pencil, "Replace", "button", 0.0f, inner);
    replaceEvery = button(kit, replaceRow, Icon::Layers, "Replace All", "button", 0.0f, inner);
    // Ctrl+G: the line to jump to.
    goRow = strip("Go to line");
    text(goRow, middle({std::ceil(kit.textWidth(EditorUiKit::regularFont(), "Go to line", font)) + 2.0f, tall}), "Go to line", "text");
    goField = field(goRow, middle({font * 6.0f, inner}), "", "");
    goButton = button(kit, goRow, Icon::ArrowDownToLine, "Go", "button", 0.0f, inner);
    goCount = text(goRow, grow(tall), "", "dim");
    goClose = toolButton(kit, goRow, Icon::Close, middle({inner, inner}));

    areas = add(main, "Code", whole(math::Vec4{0.0f, 0.0f, edge * 0.5f, tall}));
    status = text(main, UiRect{.anchorMin = {0.0f, 1.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {edge, -tall}, .offsetMax = {-edge, 0.0f}}, "", "dim");
    message = text(main, whole(math::Vec4{edge * 2.0f}), "", "dim", false, scene::TextAlign::Center);
    scene().get<scene::UiText>(message).wrap = true;

    // The list of completions, over everything, under the cursor.
    completions = add({}, "Completions", fixed({font * 24.0f, tall}), "popup");
    scene().get<UiRect>(completions).visible = false;
    scene().add<scene::UiImage>(completions);
    for (std::size_t index = 0; index < completionRows.size(); ++index)
    {
        CompletionRow& row = completionRows[index];
        const float top = 3.0f + static_cast<float>(index) * tall;
        row.row = add(completions, "Completion", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 0.0f}, .offsetMin = {3.0f, top}, .offsetMax = {-3.0f, top + tall}},
                      "menu_item");
        scene().add<scene::UiImage>(row.row);
        scene().add<scene::UiButton>(row.row);
        row.name = text(row.row, whole(math::Vec4{font * 0.6f, 0.0f, font * 0.6f, 0.0f}), "", "text", false, scene::TextAlign::Left, codeSize);
        scene().get<scene::UiText>(row.name).font = EditorUiKit::monoFont();
        row.detail = text(row.row, whole(math::Vec4{font * 0.6f, 0.0f, font * 0.6f, 0.0f}), "", "dim", false, scene::TextAlign::Right, std::round(font * 0.9f));
    }
}

ScriptUi::OpenText* ScriptUi::openOf(const std::filesystem::path& path)
{
    const auto found = std::ranges::find_if(open, [&](const OpenText& entry) { return sameTextPath(entry.path, path); });
    return found != open.end() ? &*found : nullptr;
}

void ScriptUi::syncAreas(ToolsState& state)
{
    // An area for each file that is open, and none for those that closed.
    std::erase_if(open, [&](const OpenText& entry) {
        if (findTextDocument(state, entry.path) != nullptr)
        {
            return false;
        }
        scene().destroyEntity(entry.area);
        return true;
    });
    for (const TextDocument& document : state.textDocuments)
    {
        if (openOf(document.path) != nullptr)
        {
            continue;
        }
        OpenText entry{.path = document.path, .revision = document.revision};
        entry.area = add(areas, "Text", whole(), "code");
        scene().get<UiRect>(entry.area).visible = false;
        scene().add<scene::UiImage>(entry.area);
        scene().add<scene::UiText>(entry.area, scene::UiText{.text = document.text, .font = EditorUiKit::monoFont(), .size = codeSize, .wrap = false});
        scene().add<scene::UiTextArea>(entry.area, scene::UiTextArea{.padding = {std::round(font * 0.4f), std::round(font * 0.3f)},
                                                                     .lineNumbers = true,
                                                                     .indentAfter = "{(:"});
        open.push_back(std::move(entry));
    }
}

void ScriptUi::fillFiles(ToolsState& state, EditorUiKit& kit)
{
    const std::string_view filter = scene().get<scene::UiText>(filesFilter).text;
    std::string key(filter);
    for (const TextDocument& document : state.textDocuments)
    {
        std::format_to(std::back_inserter(key), "|{}{}", document.modified() ? '*' : ':', core::toUtf8(document.path));
    }
    if (key == filesKey)
    {
        return;
    }
    filesKey = std::move(key);
    std::vector<Entity> children;
    for (Entity child = scene().firstChild(filesRows); child.isValid(); child = scene().nextSibling(child))
    {
        children.push_back(child);
    }
    for (const Entity child : children)
    {
        scene().destroyEntity(child);
    }
    fileRows.clear();
    const float tall = rowHeight();
    const float small = std::round(font * 1.3f);
    const float dot = std::round(font * 0.5f);
    for (const TextDocument& document : state.textDocuments)
    {
        const std::string name = core::toUtf8(document.path.filename());
        if (!containsIgnoringCase(name, filter))
        {
            continue;
        }
        const EntityIcon look = codeIcon(document.path);
        const Button made = button(kit, filesRows, iconOf(look.icon), name, "row", -1.0f, tall, scene::TextAlign::Left);
        scene().get<scene::UiImage>(made.icon).color = linearColor(look.color);
        scene().get<UiRect>(made.icon).style = {};
        scene().get<UiRect>(made.label) = grow(tall);
        scene().get<UiRect>(made.label).style = "text";
        if (document.modified())
        {
            // A dot for what is not saved.
            const Entity mark = add(made.entity, "Unsaved", middle({dot, dot}));
            scene().add<scene::UiImage>(mark, scene::UiImage{.color = linearColor(themeColors().textDim), .cornerRadius = dot * 0.5f, .raycastTarget = false});
        }
        FileRow row{.row = made.entity, .path = document.path};
        row.close = add(made.entity, "Close", middle({small, small}), "bar_button");
        scene().add<scene::UiImage>(row.close);
        scene().add<scene::UiButton>(row.close);
        icon(kit, row.close, whole(math::Vec4{std::round(small * 0.2f)}), Icon::Close, "icon_dim");
        tooltip(made.entity, core::toUtf8(document.path));
        fileRows.push_back(std::move(row));
    }
    scene().get<UiRect>(filesRows).offsetMax.y = (tall + 1.0f) * static_cast<float>(fileRows.size());
}

void ScriptUi::fillSymbols(ToolsState& state, EditorUiKit& kit)
{
    const ThemeColors& colors = themeColors();
    const std::string_view filter = scene().get<scene::UiText>(symbolsFilter).text;
    const bool shown = shownDocument(state) != nullptr;
    std::string key(filter);
    key += shown ? core::toUtf8(state.activeText) : std::string{};
    for (const CodeSymbol& symbol : state.textEdit.outline)
    {
        std::format_to(std::back_inserter(key), "|{}:{}{}", symbol.line, symbol.type ? 'T' : 'f', symbol.name);
    }
    if (key == symbolsKey)
    {
        return;
    }
    symbolsKey = std::move(key);
    std::vector<Entity> children;
    for (Entity child = scene().firstChild(symbolsRows); child.isValid(); child = scene().nextSibling(child))
    {
        children.push_back(child);
    }
    for (const Entity child : children)
    {
        scene().destroyEntity(child);
    }
    symbolRows.clear();
    const float tall = rowHeight();
    if (shown)
    {
        for (const CodeSymbol& symbol : state.textEdit.outline)
        {
            if (!containsIgnoringCase(symbol.name, filter))
            {
                continue;
            }
            const Button made = button(kit, symbolsRows, symbol.type ? Icon::Package : Icon::Code, symbol.name, "row", -1.0f, tall, scene::TextAlign::Left);
            scene().get<scene::UiImage>(made.icon).color = linearColor(symbol.type ? colors.codeType : colors.codeKeyword);
            scene().get<UiRect>(made.icon).style = {};
            tooltip(made.entity, std::format("Line {}", symbol.line));
            symbolRows.push_back({.row = made.entity, .line = symbol.line});
        }
    }
    scene().get<UiRect>(symbolsRows).offsetMax.y = (tall + 1.0f) * static_cast<float>(symbolRows.size());
}

void ScriptUi::colorLines(ToolsState& state, const TextDocument& document, Entity area)
{
    const ThemeColors& colors = themeColors();
    const TextEditState& edit = state.textEdit;
    ui::UiWorld& world = panel.world();
    const CodeLanguage language = languageOf(document.path);
    const auto [first, count] = world.visibleTextLines(scene(), area);
    const std::size_t lineCount = edit.lineStarts.size();

    // Only the lines in view are coloured, each kind of word in its colour, worked out once.
    std::array<math::Vec4, 8> palette{};
    for (std::size_t kind = 0; kind < palette.size(); ++kind)
    {
        palette[kind] = linearColor(colorOf(static_cast<TokenKind>(kind)));
    }
    if (coloredArea != area)
    {
        coloredLines.clear();
        coloredArea = area;
    }
    std::vector<ui::TextSpan> spans;
    for (std::size_t index = first; index < first + count && index < lineCount; ++index)
    {
        const auto begin = static_cast<std::size_t>(edit.lineStarts[index]);
        const std::size_t end = index + 1 < lineCount ? static_cast<std::size_t>(edit.lineStarts[index + 1]) - 1 : document.text.size();
        const std::string_view text = std::string_view(document.text).substr(begin, end - begin);
        const bool inComment = edit.lineInComment[index];
        ColoredLine& kept = coloredLines[index];
        kept.used = true;
        if (kept.text != text || kept.inComment != inComment || kept.language != language)
        {
            kept.text.assign(text);
            kept.inComment = inComment;
            kept.language = language;
            HighlightState lineState{.inBlockComment = inComment};
            kept.tokens = highlightLine(text, language, lineState);
        }
        for (const Token& token : kept.tokens)
        {
            spans.push_back({.begin = begin + token.begin, .end = begin + token.end, .color = palette[static_cast<std::size_t>(token.kind) % palette.size()]});
        }
    }
    std::erase_if(coloredLines, [](auto& entry) {
        const bool used = entry.second.used;
        entry.second.used = false;
        return !used;
    });
    world.setTextSpans(scene(), area, std::move(spans));

    // Everything the search found in view, and the match the panel is on.
    std::vector<ui::TextSpan> found;
    if (edit.showFind && !edit.find.empty() && lineCount > 0)
    {
        const int from = edit.lineStarts[std::min(first, lineCount - 1)];
        const int to = first + count < lineCount ? edit.lineStarts[first + count] : static_cast<int>(document.text.size()) + 1;
        for (auto match = std::ranges::lower_bound(edit.matches, from); match != edit.matches.end() && *match < to; ++match)
        {
            const bool current = static_cast<int>(match - edit.matches.begin()) == edit.currentMatch;
            const math::Vec4 tint = current ? colors.accent : colors.warning;
            found.push_back({.begin = static_cast<std::size_t>(*match),
                             .end = static_cast<std::size_t>(*match) + edit.find.size(),
                             .color = linearColor(math::Vec4(tint.x, tint.y, tint.z, current ? 0.6f : 0.35f))});
        }
    }
    world.setTextHighlights(scene(), area, std::move(found));

    // The diagnostics of the last build of the game code, and those of the last import of a shader,
    // on the lines they name.
    std::vector<ui::TextLineMark> marks;
    const std::array<const std::vector<CodeDiagnostic>*, 2> lists{&state.codeDiagnostics, &shaderDiagnosticsOf(state, document.path)};
    for (const std::vector<CodeDiagnostic>* const list : lists)
    {
        for (const CodeDiagnostic& diagnostic : *list)
        {
            if (diagnostic.line >= 1 && sameTextPath(diagnostic.path, document.path))
            {
                marks.push_back(
                    {.line = static_cast<std::uint32_t>(diagnostic.line - 1), .color = linearColor(diagnostic.error ? colors.error : colors.warning)});
            }
        }
    }
    world.setTextMarks(scene(), area, std::move(marks));
}

void ScriptUi::showCompletions(ToolsState& state, Entity area, bool focused)
{
    TextEditState& edit = state.textEdit;
    UiRect& box = scene().get<UiRect>(completions);
    const std::optional<ui::UiWorld::CaretPlace> caret =
        focused && edit.completing && !edit.completions.empty() ? panel.world().textCaretPlace(scene(), area) : std::nullopt;
    box.visible = caret.has_value();
    if (!caret)
    {
        return;
    }
    const std::size_t count = edit.completions.size();
    const auto chosen = static_cast<std::size_t>(std::clamp(edit.completionIndex, 0, static_cast<int>(count) - 1));
    // The one chosen stays in view of the few the list shows.
    firstCompletion = std::min(firstCompletion, count > completionsShown ? count - completionsShown : 0);
    if (chosen < firstCompletion)
    {
        firstCompletion = chosen;
    }
    else if (chosen >= firstCompletion + completionsShown)
    {
        firstCompletion = chosen + 1 - completionsShown;
    }
    const std::size_t shown = std::min(count - firstCompletion, completionsShown);
    const float tall = rowHeight();
    for (std::size_t index = 0; index < completionRows.size(); ++index)
    {
        const CompletionRow& row = completionRows[index];
        UiRect& rect = scene().get<UiRect>(row.row);
        rect.visible = index < shown;
        if (!rect.visible)
        {
            continue;
        }
        const CompletionItem& item = edit.completions[firstCompletion + index];
        rect.style = firstCompletion + index == chosen ? "row_selected" : "menu_item";
        if (scene::UiText& name = scene().get<scene::UiText>(row.name); name.text != item.text)
        {
            name.text = item.text;
        }
        if (scene::UiText& detail = scene().get<scene::UiText>(row.detail); detail.text != item.detail)
        {
            detail.text = item.detail;
        }
    }
    // Under the cursor, or over it when the panel ends first, and never past its right edge.
    const math::Vec2 size{font * 24.0f, 6.0f + tall * static_cast<float>(shown)};
    const math::Vec2 room = panel.size();
    math::Vec2 at{caret->position.x, caret->position.y + caret->height + 2.0f};
    if (at.y + size.y > room.y && caret->position.y - size.y - 2.0f >= 0.0f)
    {
        at.y = caret->position.y - size.y - 2.0f;
    }
    at.x = std::clamp(at.x, 0.0f, std::max(room.x - size.x, 0.0f));
    box.offsetMin = at;
    box.offsetMax = at + size;
}

std::optional<PendingAction> ScriptUi::update(ToolsState& state, EditorUiKit& kit, core::Duration delta)
{
    const ThemeColors& colors = themeColors();
    kit.refreshTheme(colors);
    if (!built || builtFont != state.theme.fontSize || builtCode != state.theme.codeFontSize)
    {
        setFont(state.theme.fontSize);
        codeSize = state.theme.codeFontSize;
        build(kit);
    }
    styleTooltips(colors);
    ui::UiWorld& world = panel.world();
    TextEditState& edit = state.textEdit;
    std::optional<PendingAction> action;

    // The file shown: the one asked for, or another one that is still open.
    syncAreas(state);
    if (shownDocument(state) == nullptr)
    {
        state.activeText = state.textDocuments.empty() ? std::filesystem::path{} : state.textDocuments.back().path;
    }
    state.selectTextTab = false;
    TextDocument* const document = shownDocument(state);
    OpenText* const shown = document != nullptr ? openOf(document->path) : nullptr;
    for (const OpenText& entry : open)
    {
        scene().get<UiRect>(entry.area).visible = &entry == shown;
    }
    const Entity area = shown != nullptr ? shown->area : Entity{};
    if (document != nullptr && !sameTextPath(shownPath, document->path))
    {
        // Another file takes the keyboard as it comes to the screen.
        focusText = true;
    }
    shownPath = document != nullptr ? document->path : std::filesystem::path{};

    CodeLanguage language = CodeLanguage::PlainText;
    if (document != nullptr && shown != nullptr)
    {
        const std::string path = core::toUtf8(document->path);
        if (edit.path != path)
        {
            // Another document: the search and the completions start over.
            std::string find = std::move(edit.find);
            std::string replaceWith = std::move(edit.replace);
            const bool showFind = edit.showFind;
            const bool showReplace = edit.showReplace;
            const bool matchCaseBefore = edit.matchCase;
            edit = TextEditState{};
            edit.path = path;
            edit.find = std::move(find);
            edit.replace = std::move(replaceWith);
            edit.showFind = showFind;
            edit.showReplace = showReplace;
            edit.matchCase = matchCaseBefore;
            going = false;
        }
        language = languageOf(document->path);
        if (edit.namesLanguage != language || edit.engineNames.empty())
        {
            edit.engineNames = engineNames(language);
            edit.namesLanguage = language;
        }

        // What the search, the comments and the completions asked to change, then the text itself:
        // written into the area, which makes it a change it can go back on.
        for (const TextEditState::Edit& pending : edit.edits)
        {
            const auto begin = static_cast<std::size_t>(std::clamp(pending.begin, 0, static_cast<int>(document->text.size())));
            const auto end = static_cast<std::size_t>(std::clamp(pending.end, static_cast<int>(begin), static_cast<int>(document->text.size())));
            document->text.replace(begin, end - begin, pending.text);
        }
        edit.edits.clear();
        std::string& shownText = scene().get<scene::UiText>(area).text;
        if (shown->revision != document->revision)
        {
            // Read again from its file: another text, which starts a history of its own.
            shownText = document->text;
            world.forgetTextHistory(scene(), area);
            shown->revision = document->revision;
        }
        else if (shownText != document->text)
        {
            shownText = document->text;
        }
        if (edit.lineStarts.empty() || edit.indexedLength != static_cast<int>(document->text.size()) || std::exchange(edit.textChanged, false))
        {
            indexDocument(edit, *document);
            searchDocument(edit, *document);
        }
        colorLines(state, *document, area);
    }

    // The bar, the lists and the strips above the text.
    const std::optional<std::size_t> menuOpen = editorMenuOwner(state);
    for (std::size_t index = 0; index < menus.size(); ++index)
    {
        scene().get<UiRect>(menus[index].entity).style = barStyle(world, menus[index].entity, menuOpen == scriptMenuOwner + index);
    }
    {
        scene::UiText& shownPathText = scene().get<scene::UiText>(pathText);
        std::string value = document != nullptr ? core::toUtf8(document->path) : std::string{};
        if (shownPathText.text != value)
        {
            shownPathText.text = std::move(value);
        }
    }
    fillFiles(state, kit);
    fillSymbols(state, kit);
    for (const FileRow& row : fileRows)
    {
        scene().get<UiRect>(row.row).style = document != nullptr && sameTextPath(row.path, document->path) ? "row_selected" : "row";
        scene().get<UiRect>(row.close).style = barStyle(world, row.close, false);
    }

    const bool searching = document != nullptr && edit.showFind;
    const bool replacing = searching && edit.showReplace;
    if (std::exchange(edit.openGoTo, false) && document != nullptr)
    {
        going = true;
        scene().get<scene::UiText>(goField).text = std::to_string(document->line);
        world.startEditing(scene(), goField, true);
    }
    going = going && document != nullptr;
    const float tall = rowHeight();
    float top = 0.0f;
    const auto place = [&](Entity row, bool visible) {
        UiRect& rect = scene().get<UiRect>(row);
        rect.visible = visible;
        rect.offsetMin.y = top;
        rect.offsetMax.y = top + tall;
        top += visible ? tall + 2.0f : 0.0f;
    };
    place(findRow, searching);
    place(replaceRow, replacing);
    place(goRow, going);
    scene().get<UiRect>(areas).offsetMin.y = top;
    scene().get<UiRect>(areas).visible = document != nullptr;
    scene().get<UiRect>(status).visible = document != nullptr;
    scene().get<UiRect>(message).visible = document == nullptr;
    if (document == nullptr)
    {
        scene::UiText& said = scene().get<scene::UiText>(message);
        const std::string_view sentence =
            !state.textOpenError.empty() ? std::string_view(state.textOpenError)
                                         : "Open a file from FileSystem, make a script with File > New Script, or use Edit as Text on a Devex asset.";
        if (said.text != sentence)
        {
            said.text = std::string(sentence);
        }
        scene().get<UiRect>(message).style = state.textOpenError.empty() ? "dim" : "error";
    }
    if (searching)
    {
        setText(findField, edit.find);
        setText(replaceField, edit.replace);
        scene().get<scene::UiToggle>(matchCase).value = edit.matchCase;
        enable(findNext, !edit.matches.empty());
        enable(findPrevious, !edit.matches.empty());
        enable(replaceOne, !edit.matches.empty());
        enable(replaceEvery, !edit.matches.empty());
        scene::UiText& count = scene().get<scene::UiText>(findCount);
        std::string value = edit.find.empty()      ? std::string("Type to search")
                            : edit.matches.empty() ? std::string("No match")
                                                   : std::format("{} of {}", edit.currentMatch + 1, edit.matches.size());
        if (count.text != value)
        {
            count.text = std::move(value);
        }
        if (std::exchange(edit.focusFind, false))
        {
            world.startEditing(scene(), findField, true);
        }
    }
    if (going)
    {
        scene::UiText& count = scene().get<scene::UiText>(goCount);
        if (std::string value = std::format("of {}", std::max<std::size_t>(edit.lineStarts.size(), 1)); count.text != value)
        {
            count.text = std::move(value);
        }
    }
    if (document != nullptr)
    {
        // Where the cursor is, what the file is, and why it would not be saved.
        scene::UiText& written = scene().get<scene::UiText>(status);
        std::string value = !document->error.empty()
                                ? document->error
                                : std::format("Ln {}, Col {}  |  {}  |  UTF-8{}  |  {}  |  Ctrl+S Save   Ctrl+F Find   Ctrl+G Go to line   Ctrl+K Comment",
                                              document->line, document->column, toString(language), document->bom() ? " BOM" : "",
                                              document->crlf() ? "CRLF" : "LF");
        if (written.text != value)
        {
            written.text = std::move(value);
        }
        scene().get<UiRect>(status).style = document->error.empty() ? "dim" : "error";
        if (std::exchange(focusText, false))
        {
            world.startEditing(scene(), area);
        }
    }
    const bool typing = area.isValid() && world.editedTextArea() == area;
    showCompletions(state, area, typing);

    panel.update(kit, delta, UiPanel::zoomFor(font));
    answerForm();

    // The menus open under their titles, and the pointer on another title goes to its menu.
    const ui::LayoutResult* const layout = world.canvases().empty() ? nullptr : &world.canvases().front().layout;
    const math::Vec2 mouse = state.input.mouse();
    for (std::size_t index = 0; index < menus.size() && layout != nullptr; ++index)
    {
        const ui::LaidOutRect* const rect = layout->find(menus[index].entity);
        if (rect == nullptr)
        {
            continue;
        }
        const math::Vec2 min = panel.screenOf(rect->min);
        const math::Vec2 max = panel.screenOf(rect->max);
        const bool pointed = mouse.x >= min.x && mouse.x < max.x && mouse.y >= min.y && mouse.y < max.y;
        const std::size_t owner = scriptMenuOwner + index;
        if ((world.wasClicked(menus[index].entity) && menuOpen != owner) ||
            (menuOpen && *menuOpen >= scriptMenuOwner && *menuOpen < scriptMenuOwner + menus.size() && *menuOpen != owner && pointed))
        {
            std::vector<MenuEntry> entries = index == 0   ? fileMenu(state)
                                             : index == 1 ? textEditMenu(state, area.isValid() && world.canUndoText(area), area.isValid() && world.canRedoText(area))
                                                          : searchMenu(state);
            openEditorMenu(state, std::move(entries), math::Vec2(min.x, max.y), owner);
        }
    }

    // The files: a click shows one, its cross or the middle button closes it.
    for (const FileRow& row : fileRows)
    {
        if (world.wasClicked(row.close))
        {
            action = PendingAction{.kind = PendingAction::Kind::CloseText, .path = row.path};
        }
        else if (world.wasClicked(row.row))
        {
            state.activeText = row.path;
        }
    }
    if (panel.hovered() && state.input.clicked(Mouse::Middle))
    {
        for (Entity above = world.hovered(); above.isValid() && scene().isAlive(above); above = scene().parent(above))
        {
            const auto found = std::ranges::find(fileRows, above, &FileRow::row);
            if (found != fileRows.end())
            {
                action = PendingAction{.kind = PendingAction::Kind::CloseText, .path = found->path};
                break;
            }
        }
    }
    if (document == nullptr || shown == nullptr)
    {
        return action;
    }

    if (std::exchange(edit.requestUndo, false))
    {
        world.undoText(scene(), area);
    }
    if (std::exchange(edit.requestRedo, false))
    {
        world.redoText(scene(), area);
    }
    // What was typed goes back to the document, with where the cursor is.
    const std::string& typed = scene().get<scene::UiText>(area).text;
    if (typed != document->text)
    {
        document->text = typed;
        indexDocument(edit, *document);
        searchDocument(edit, *document);
    }
    // A selection the search or a jump asked for, once the text it counts in is the one shown.
    if (edit.pendingCursor && edit.edits.empty())
    {
        const auto [begin, end] = *std::exchange(edit.pendingCursor, std::nullopt);
        world.selectText(scene(), area, static_cast<std::size_t>(std::max(begin, 0)), static_cast<std::size_t>(std::max(end, 0)));
    }
    edit.revealLine.reset();
    const auto [caret, anchor] = world.textSelection(area);
    edit.cursor = static_cast<int>(caret);
    edit.selectionBegin = static_cast<int>(anchor);
    edit.selectionEnd = static_cast<int>(caret);
    document->line = std::max(lineOfOffsetIndexed(edit, edit.cursor), 1);
    document->column = 1 + edit.cursor - edit.lineStarts[static_cast<std::size_t>(document->line - 1)];

    // The symbols: a click goes to the line that declares one.
    for (const SymbolRow& row : symbolRows)
    {
        if (world.wasClicked(row.row))
        {
            goToLine(edit, *document, row.line);
            focusText = true;
        }
    }

    // Find and replace.
    if (searching)
    {
        if (const std::string& wanted = scene().get<scene::UiText>(findField).text; world.editedField() == findField && wanted != edit.find)
        {
            edit.find = wanted;
            edit.currentMatch = -1;
            searchDocument(edit, *document);
        }
        if (const std::string& wanted = scene().get<scene::UiText>(replaceField).text; world.editedField() == replaceField && wanted != edit.replace)
        {
            edit.replace = wanted;
        }
        if (world.wasSubmitted(findField))
        {
            // Enter goes to the next match, and the field keeps the keyboard for the one after.
            searchDocument(edit, *document);
            selectMatch(edit, *document, panel.input().selecting ? -1 : 1);
            world.startEditing(scene(), findField, false);
        }
        if (world.wasClicked(findNext.entity))
        {
            selectMatch(edit, *document, 1);
        }
        if (world.wasClicked(findPrevious.entity))
        {
            selectMatch(edit, *document, -1);
        }
        if (world.wasChanged(matchCase))
        {
            edit.matchCase = scene().get<scene::UiToggle>(matchCase).value;
            searchDocument(edit, *document);
        }
        if (world.wasClicked(replaceOne.entity) || world.wasSubmitted(replaceField))
        {
            replaceMatch(edit, *document, false);
        }
        if (world.wasClicked(replaceEvery.entity))
        {
            replaceMatch(edit, *document, true);
        }
        if (world.wasClicked(findClose.entity) || (keys.cancel && !going && !scene().get<UiRect>(completions).visible))
        {
            edit.showFind = false;
            edit.showReplace = false;
            edit.matches.clear();
            focusText = true;
        }
    }
    if (going)
    {
        if (world.wasSubmitted(goField) || world.wasClicked(goButton.entity))
        {
            int number = document->line;
            const std::string& wanted = scene().get<scene::UiText>(goField).text;
            std::from_chars(wanted.data(), wanted.data() + wanted.size(), number);
            goToLine(edit, *document, std::clamp(number, 1, static_cast<int>(std::max<std::size_t>(edit.lineStarts.size(), 1))));
            going = false;
            focusText = true;
        }
        else if (world.wasClicked(goClose.entity) || keys.cancel)
        {
            going = false;
            focusText = true;
        }
    }

    // The completions: the arrows choose, Enter, Tab or a click takes one, Escape closes the list.
    if (scene().get<UiRect>(completions).visible && !edit.completions.empty())
    {
        const int count = static_cast<int>(edit.completions.size());
        edit.completionIndex = (std::clamp(edit.completionIndex, 0, count - 1) + keys.move + count) % count;
        edit.completionAccepted = edit.completionAccepted || keys.accept;
        for (std::size_t index = 0; index < completionRows.size(); ++index)
        {
            if (world.wasClicked(completionRows[index].row) && firstCompletion + index < edit.completions.size())
            {
                edit.completionIndex = static_cast<int>(firstCompletion + index);
                edit.completionAccepted = true;
                // The click took the keyboard from the text: it goes back to it.
                focusText = true;
            }
        }
        if (keys.cancel)
        {
            edit.dismissedPrefix = edit.completionPrefix;
            edit.completing = false;
            edit.completions.clear();
        }
    }
    const bool stillTyping = world.editedTextArea() == area || focusText;
    if (std::exchange(edit.completionAccepted, false))
    {
        acceptCompletion(edit);
    }
    else if (stillTyping)
    {
        updateCompletion(edit, *document, language);
    }
    else
    {
        edit.completing = false;
    }

    // What a line of the last build, or of the last import of a shader, says, next to the pointer on it.
    const std::vector<CodeDiagnostic>& shaderErrors = shaderDiagnosticsOf(state, document->path);
    if (panel.hovered() && (!state.codeDiagnostics.empty() || !shaderErrors.empty()))
    {
        const std::int32_t pointed = world.textLineAt(scene(), area, panel.input().pointer);
        bool told = false;
        const std::array<const std::vector<CodeDiagnostic>*, 2> lists{&state.codeDiagnostics, &shaderErrors};
        for (const std::vector<CodeDiagnostic>* const list : lists)
        {
            for (const CodeDiagnostic& diagnostic : *list)
            {
                if (!told && pointed >= 0 && diagnostic.line - 1 == pointed && sameTextPath(diagnostic.path, document->path))
                {
                    kit.showTooltip(diagnostic.message, mouse);
                    told = true;
                }
            }
        }
    }
    return action;
}

void drawTextEditorPanel(ToolsState& state, scene::Scene& scene)
{
    DEVEX_PROFILE_SCOPE("Text editor");
    if (auto path = std::exchange(state.dialogAnswers->openText, std::nullopt))
    {
        openTextFile(state, *path);
    }
    if (!state.showTextEditor)
    {
        return;
    }
    if (std::exchange(state.focusTextEditor, false))
    {
        focusPanel(state, textEditorWindow);
    }
    if (!beginDockedPanel(state, textEditorWindow))
    {
        return;
    }
    EditorUiKit& kit = editorUiKit(state);
    if (!state.scriptUi)
    {
        state.scriptUi = std::make_shared<ScriptUi>();
    }
    std::optional<PendingAction> action = state.scriptUi->update(state, kit, core::Duration(state.input.delta()));
    endDockedPanel(state);
    // Closing/reloading may invalidate a document: do this after the panel has used it.
    if (action)
    {
        requestAction(state, scene, std::move(*action));
    }
}

void openTextMoved(ToolsState& state, const std::filesystem::path& from, const std::filesystem::path& to)
{
    if (!state.scriptUi)
    {
        return;
    }
    for (ScriptUi::OpenText& entry : state.scriptUi->open)
    {
        if (entry.path == from)
        {
            entry.path = to;
        }
    }
    // The list of the files is made again with its new name.
    state.scriptUi->filesKey.clear();
}

void renderTextEditor(ToolsState& state, render::RenderWorld& world)
{
    if (state.scriptUi && state.uiKit)
    {
        state.scriptUi->panel.render(*state.uiKit, world, linearColor(themeColors().panel));
    }
}

} // namespace devex::tools::detail
