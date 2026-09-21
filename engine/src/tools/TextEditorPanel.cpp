#include "ToolsState.hpp"

#include "CodeArea.hpp"
#include "CodeCompletion.hpp"
#include "CodeHighlight.hpp"
#include "CodeOutline.hpp"

#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>

#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <string>
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

bool textEditorFocused()
{
    const ImGuiWindow* window = ImGui::FindWindowByName(textEditorWindow);
    const ImGuiWindow* focused = GImGui->NavWindow;
    return window != nullptr && focused != nullptr && focused->RootWindow == window->RootWindow;
}

namespace {


} // namespace

namespace {

[[nodiscard]] ImU32 colorOf(TokenKind kind)
{
    const ThemeColors& colors = themeColors();
    switch (kind)
    {
    case TokenKind::Keyword:
        return uiColorU32(colors.codeKeyword);
    case TokenKind::Type:
        return uiColorU32(colors.codeType);
    case TokenKind::Comment:
        return uiColorU32(colors.codeComment);
    case TokenKind::String:
        return uiColorU32(colors.codeString);
    case TokenKind::Number:
        return uiColorU32(colors.codeNumber);
    case TokenKind::Directive:
        return uiColorU32(colors.codeDirective);
    case TokenKind::Punctuation:
        return uiColorU32(colors.codePunctuation);
    case TokenKind::Text:
        break;
    }
    return ImGui::GetColorU32(ImGuiCol_Text);
}

// Where every line starts, and whether it begins inside a block comment: rebuilt after each edit
// so that only the visible lines are colored.
void indexLines(TextEditState& edit, const TextDocument& document)
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
        const std::string_view line(text.data() + index,
                                    (stop == std::string::npos ? text.size() : stop) - index);
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

// The widest line of the file, in characters: how far the view scrolls sideways.
[[nodiscard]] int longestLine(const TextEditState& edit, const TextDocument& document)
{
    int longest = 0;
    for (std::size_t line = 0; line < edit.lineStarts.size(); ++line)
    {
        const int begin = edit.lineStarts[line];
        const int end = line + 1 < edit.lineStarts.size() ? edit.lineStarts[line + 1] - 1
                                                          : static_cast<int>(document.text.size());
        longest = std::max(longest, end - begin);
    }
    return longest;
}

// The screen position of a byte offset of the text.
[[nodiscard]] ImVec2 positionOf(const TextEditState& edit, const TextDocument& document, int offset,
                                ImVec2 origin, float lineHeight)
{
    const auto line = static_cast<std::size_t>(
        std::clamp(lineOfOffsetIndexed(edit, offset), 1, static_cast<int>(edit.lineStarts.size())) - 1);
    const int begin = edit.lineStarts[line];
    const std::string_view prefix(document.text.data() + begin,
                                  static_cast<std::size_t>(std::max(offset - begin, 0)));
    const float x = prefix.empty() ? 0.0f
                                   : ImGui::CalcTextSize(prefix.data(), prefix.data() + prefix.size()).x;
    return {origin.x + x, origin.y + static_cast<float>(line) * lineHeight};
}

} // namespace

// The line a byte offset falls on, found in the index rather than by counting again.
int lineOfOffsetIndexed(const TextEditState& edit, int offset)
{
    const auto next = std::ranges::upper_bound(edit.lineStarts, offset);
    return static_cast<int>(next - edit.lineStarts.begin());
}

void drawCodeArea(ToolsState& state, TextDocument& document)
{
    TextEditState& edit = state.textEdit;
    const std::string path = core::toUtf8(document.path);
    if (edit.path != path)
    {
        // Another document: the cursor, the search and the completions start over.
        const std::string find = std::move(edit.find);
        const std::string replaceWith = std::move(edit.replace);
        const bool showFind = edit.showFind;
        const bool showReplace = edit.showReplace;
        edit = TextEditState{};
        edit.path = path;
        edit.find = find;
        edit.replace = replaceWith;
        edit.showFind = showFind;
        edit.showReplace = showReplace;
    }
    const CodeLanguage language = languageOf(document.path);
    if (edit.namesLanguage != language || edit.engineNames.empty())
    {
        edit.engineNames = engineNames(language);
        edit.namesLanguage = language;
    }

    const ThemeColors& colors = themeColors();
    ImGui::PushFont(editorFonts().mono, 0.0f);
    const float lineHeight = ImGui::GetTextLineHeight();
    const float charWidth = std::max(ImGui::CalcTextSize("0").x, 1.0f);

    if (edit.lineStarts.empty() || edit.indexedLength != static_cast<int>(document.text.size()) ||
        std::exchange(edit.textChanged, false))
    {
        indexLines(edit, document);
    }
    const auto lineCount = static_cast<int>(edit.lineStarts.size());
    const float gutterWidth = charWidth * static_cast<float>(std::to_string(lineCount).size() + 2);

    // The panel scrolls, and the field is as large as the text: colors and markers can then be
    // drawn at fixed places instead of following the scrolling of a field.
    const float height = std::max(lineHeight * 3.0f,
                                  ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing());
    ImGui::PushStyleColor(ImGuiCol_ChildBg, uiColor(colors.field));
    ImGui::BeginChild("code", ImVec2(0.0f, height), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar);
    const float contentWidth =
        std::max(static_cast<float>(longestLine(edit, document) + 4) * charWidth, ImGui::GetContentRegionAvail().x);
    const float contentHeight = static_cast<float>(lineCount) * lineHeight + ImGui::GetStyle().FramePadding.y * 2.0f;

    // The completion popup takes the arrows, Escape and Enter while it is open.
    const ImGuiID completionOwner = ImGui::GetID("##completion");
    const bool popupOpen = edit.completing && !edit.completions.empty();
    if (popupOpen)
    {
        for (const ImGuiKey key : {ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_Escape, ImGuiKey_Enter,
                                   ImGuiKey_KeypadEnter})
        {
            ImGui::SetKeyOwner(key, completionOwner, ImGuiInputFlags_LockThisFrame);
        }
    }

    CodeAreaCallbackContext context{.edit = &edit, .document = &document};
    ImGui::SetCursorPosX(gutterWidth);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushID(static_cast<int>(document.revision));
    const bool changed = ImGui::InputTextMultiline(
        "##content", &document.text, ImVec2(contentWidth, contentHeight),
        // Tab is handled by the completion callback: it takes a completion, or indents by four
        // spaces, which is what AllowTabInput would otherwise insert as a tab character.
        ImGuiInputTextFlags_CallbackAlways | ImGuiInputTextFlags_CallbackCompletion |
            ImGuiInputTextFlags_CallbackEdit | ImGuiInputTextFlags_NoHorizontalScroll,
        codeAreaCallback, &context);
    ImGui::PopID();
    ImGui::PopStyleColor(2);
    const bool active = ImGui::IsItemActive();
    const ImVec2 origin = ImGui::GetItemRectMin() + ImGui::GetStyle().FramePadding;
    if (changed)
    {
        edit.textChanged = true;
        indexLines(edit, document);
        searchDocument(edit, document);
    }

    // Only the lines on screen are colored.
    ImDrawList* const draw = ImGui::GetWindowDrawList();
    const float scroll = ImGui::GetScrollY();
    const int firstLine = std::max(0, static_cast<int>(scroll / lineHeight) - 1);
    const int lastLine = std::min(lineCount - 1, firstLine + static_cast<int>(height / lineHeight) + 2);
    const int cursorLine = lineOfOffsetIndexed(edit, edit.cursor);

    for (int line = firstLine; line <= lastLine && lineCount > 0; ++line)
    {
        const float y = origin.y + static_cast<float>(line) * lineHeight;
        const int begin = edit.lineStarts[static_cast<std::size_t>(line)];
        const int end = line + 1 < lineCount ? edit.lineStarts[static_cast<std::size_t>(line) + 1] - 1
                                             : static_cast<int>(document.text.size());
        const std::string_view text(document.text.data() + begin, static_cast<std::size_t>(end - begin));

        if (line + 1 == cursorLine && active)
        {
            draw->AddRectFilled(ImVec2(origin.x - gutterWidth, y),
                                ImVec2(origin.x + contentWidth, y + lineHeight),
                                ImGui::GetColorU32(ImGuiCol_TextSelectedBg, 0.25f));
        }

        HighlightState lineState{.inBlockComment = edit.lineInComment[static_cast<std::size_t>(line)]};
        const std::vector<Token> tokens = highlightLine(text, language, lineState);
        // The plain text first, then the tokens over it in their own colors.
        float x = origin.x;
        std::size_t drawn = 0;
        for (const Token& token : tokens)
        {
            if (token.begin > drawn)
            {
                const std::string_view plain = text.substr(drawn, token.begin - drawn);
                draw->AddText(ImVec2(x, y), ImGui::GetColorU32(ImGuiCol_Text), plain.data(),
                              plain.data() + plain.size());
                x += ImGui::CalcTextSize(plain.data(), plain.data() + plain.size()).x;
            }
            const std::string_view colored = text.substr(token.begin, token.end - token.begin);
            draw->AddText(ImVec2(x, y), colorOf(token.kind), colored.data(), colored.data() + colored.size());
            x += ImGui::CalcTextSize(colored.data(), colored.data() + colored.size()).x;
            drawn = token.end;
        }
        if (drawn < text.size())
        {
            const std::string_view plain = text.substr(drawn);
            draw->AddText(ImVec2(x, y), ImGui::GetColorU32(ImGuiCol_Text), plain.data(),
                          plain.data() + plain.size());
        }

        // The line number, and a marker for the errors of the last build.
        const std::string number = std::to_string(line + 1);
        const float numberX = origin.x - gutterWidth + charWidth;
        draw->AddText(ImVec2(numberX, y),
                      ImGui::GetColorU32(line + 1 == cursorLine ? ImGuiCol_Text : ImGuiCol_TextDisabled),
                      number.c_str());
    }

    // Everything the search found, and the match the panel is on.
    for (std::size_t match = 0; match < edit.matches.size(); ++match)
    {
        const int offset = edit.matches[match];
        const int line = lineOfOffsetIndexed(edit, offset);
        if (line - 1 < firstLine || line - 1 > lastLine)
        {
            continue;
        }
        const ImVec2 begin = positionOf(edit, document, offset, origin, lineHeight);
        const ImVec2 end = positionOf(edit, document, offset + static_cast<int>(edit.find.size()), origin,
                                      lineHeight);
        const bool current = static_cast<int>(match) == edit.currentMatch;
        draw->AddRectFilled(ImVec2(begin.x, begin.y), ImVec2(end.x, begin.y + lineHeight),
                            uiColorU32(current ? colors.accent : colors.warning), 2.0f);
    }

    // The diagnostics of the last build of the game code, on the lines they name.
    for (const CodeDiagnostic& diagnostic : state.codeDiagnostics)
    {
        if (!sameTextPath(diagnostic.path, document.path) || diagnostic.line - 1 < firstLine ||
            diagnostic.line - 1 > lastLine)
        {
            continue;
        }
        const float y = origin.y + static_cast<float>(diagnostic.line - 1) * lineHeight;
        const ImU32 color = uiColorU32(diagnostic.error ? colors.error : colors.warning);
        draw->AddRectFilled(ImVec2(origin.x - gutterWidth, y),
                            ImVec2(origin.x - gutterWidth + charWidth * 0.6f, y + lineHeight), color, 2.0f);
        draw->AddLine(ImVec2(origin.x, y + lineHeight - 1.0f),
                      ImVec2(origin.x + contentWidth, y + lineHeight - 1.0f), color & 0x40FFFFFF, 1.0f);
        if (ImGui::IsMouseHoveringRect(ImVec2(origin.x - gutterWidth, y),
                                       ImVec2(origin.x + contentWidth, y + lineHeight)))
        {
            ImGui::SetTooltip("%s", diagnostic.message.c_str());
        }
    }

    // The field draws its cursor in the text color, which is transparent here.
    if (active)
    {
        const ImVec2 position = positionOf(edit, document, edit.cursor, origin, lineHeight);
        const double blink = std::fmod(ImGui::GetTime() - edit.cursorTime, 1.06);
        if (blink < 0.53)
        {
            draw->AddLine(position, ImVec2(position.x, position.y + lineHeight),
                          ImGui::GetColorU32(ImGuiCol_Text), 1.0f);
        }
    }
    if (edit.cursor != edit.previousCursor)
    {
        edit.previousCursor = edit.cursor;
        edit.cursorTime = ImGui::GetTime();
    }

    // Scrolling to a line the panel jumped to, once its position is known.
    if (const std::optional<int> line = std::exchange(edit.revealLine, std::nullopt))
    {
        const float y = static_cast<float>(*line - 1) * lineHeight;
        if (y < scroll || y > scroll + height - lineHeight * 2.0f)
        {
            ImGui::SetScrollY(std::max(0.0f, y - height * 0.4f));
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopFont();

    // The completion popup, under the cursor.
    if (popupOpen)
    {
        const ImVec2 position = positionOf(edit, document, edit.cursor, origin, lineHeight);
        ImGui::SetNextWindowPos(ImVec2(position.x, position.y + lineHeight));
        ImGui::SetNextWindowSizeConstraints(ImVec2(ImGui::GetFontSize() * 12.0f, 0.0f),
                                            ImVec2(FLT_MAX, ImGui::GetFontSize() * 14.0f));
        if (ImGui::Begin("##completions", nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav))
        {
            for (std::size_t index = 0; index < edit.completions.size(); ++index)
            {
                const CompletionItem& item = edit.completions[index];
                ImGui::PushID(static_cast<int>(index));
                if (ImGui::Selectable(item.text.c_str(), static_cast<int>(index) == edit.completionIndex))
                {
                    edit.completionIndex = static_cast<int>(index);
                    edit.completionAccepted = true;
                }
                ImGui::SameLine();
                ImGui::TextDisabled("%s", item.detail.c_str());
                ImGui::PopID();
            }
        }
        ImGui::End();

        const int count = static_cast<int>(edit.completions.size());
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, ImGuiInputFlags_Repeat, completionOwner))
        {
            edit.completionIndex = (edit.completionIndex + 1) % count;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, ImGuiInputFlags_Repeat, completionOwner))
        {
            edit.completionIndex = (edit.completionIndex + count - 1) % count;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Enter, ImGuiInputFlags_None, completionOwner) ||
            ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, ImGuiInputFlags_None, completionOwner))
        {
            edit.completionAccepted = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, ImGuiInputFlags_None, completionOwner))
        {
            edit.completing = false;
            edit.completions.clear();
        }
    }
    if (std::exchange(edit.completionAccepted, false))
    {
        acceptCompletion(edit);
    }
    else if (active)
    {
        updateCompletion(edit, document, language);
    }
    else
    {
        edit.completing = false;
    }
}

// Find and replace, above the text: Ctrl+F opens it, Ctrl+H adds the replacement field.
void drawFindBar(ToolsState& state, TextDocument& document)
{
    TextEditState& edit = state.textEdit;
    if (!edit.showFind)
    {
        return;
    }
    const float fieldWidth = ImGui::GetFontSize() * 12.0f;
    if (std::exchange(edit.focusFind, false))
    {
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(fieldWidth);
    if (ImGui::InputTextWithHint("##find", "Find", &edit.find, ImGuiInputTextFlags_EnterReturnsTrue))
    {
        searchDocument(edit, document);
        selectMatch(edit, document, ImGui::GetIO().KeyShift ? -1 : 1);
    }
    else if (ImGui::IsItemEdited())
    {
        edit.currentMatch = -1;
        searchDocument(edit, document);
    }
    ImGui::SameLine();
    if (labelButton(icons::ChevronRight, "Next", 0.0f, !edit.matches.empty()))
    {
        selectMatch(edit, document, 1);
    }
    ImGui::SameLine();
    if (labelButton(icons::ChevronDown, "Previous", 0.0f, !edit.matches.empty()))
    {
        selectMatch(edit, document, -1);
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Aa", &edit.matchCase))
    {
        searchDocument(edit, document);
    }
    ImGui::SetItemTooltip("Match case");
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    if (edit.find.empty())
    {
        ImGui::TextDisabled("Type to search");
    }
    else if (edit.matches.empty())
    {
        ImGui::TextDisabled("No match");
    }
    else
    {
        ImGui::TextDisabled("%d of %zu", edit.currentMatch + 1, edit.matches.size());
    }
    ImGui::SameLine();
    if (labelButton(icons::Close, "Close"))
    {
        edit.showFind = false;
        edit.showReplace = false;
        edit.matches.clear();
    }

    if (edit.showReplace)
    {
        ImGui::SetNextItemWidth(fieldWidth);
        ImGui::InputTextWithHint("##replace", "Replace with", &edit.replace);
        ImGui::SameLine();
        if (labelButton(icons::Pencil, "Replace", 0.0f, !edit.matches.empty()))
        {
            replaceMatch(edit, document, false);
        }
        ImGui::SameLine();
        if (labelButton(icons::Layers, "Replace All", 0.0f, !edit.matches.empty()))
        {
            replaceMatch(edit, document, true);
        }
    }
}

// Ctrl+G: the line to jump to.
void drawGoToLinePopup(ToolsState& state, TextDocument& document)
{
    TextEditState& edit = state.textEdit;
    if (std::exchange(edit.openGoTo, false))
    {
        edit.goToLine = document.line;
        ImGui::OpenPopup("Go to line");
    }
    if (!ImGui::BeginPopup("Go to line"))
    {
        return;
    }
    if (ImGui::IsWindowAppearing())
    {
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
    const bool entered = ImGui::InputInt("##line", &edit.goToLine, 1, 10, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    ImGui::TextDisabled("of %zu", std::max<std::size_t>(edit.lineStarts.size(), 1));
    if (entered || labelButton(icons::ArrowDownToLine, "Go"))
    {
        goToLine(edit, document, edit.goToLine);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

namespace {

// The code files of the project, read again from time to time rather than at every frame.
void scanScripts(ToolsState& state)
{
    const double now = ImGui::GetTime();
    if (state.database == nullptr || (state.scriptsScanned >= 0.0 && now - state.scriptsScanned < 2.0))
    {
        return;
    }
    state.scriptsScanned = now;
    state.scriptFiles.clear();
    state.scriptFiles.push_back(state.database->project().file);
    std::error_code error;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::recursive_directory_iterator(state.database->project().codeDirectory(), error))
    {
        const std::string name = core::toUtf8(entry.path().filename());
        // Build folders hold no code of the game.
        if (entry.is_directory(error) && (name == "obj" || name == "bin" || name.starts_with('.')))
        {
            continue;
        }
        if (entry.is_regular_file(error) && languageOf(entry.path()) != CodeLanguage::PlainText)
        {
            state.scriptFiles.push_back(entry.path());
        }
    }
    std::ranges::sort(state.scriptFiles, [](const std::filesystem::path& left, const std::filesystem::path& right) {
        return left.filename() < right.filename();
    });
}

void drawScriptSidebar(ToolsState& state, scene::Scene& scene)
{
    const ThemeColors& colors = themeColors();
    scanScripts(state);
    const float width = ImGui::GetFontSize() * 11.0f;
    // Opening a document grows the vector that holds them, so the lists work on a copy of the
    // path of the open one and act once they are drawn.
    const std::filesystem::path active = state.activeText;
    std::optional<std::filesystem::path> chosen;
    std::optional<int> line;

    // One column holding both lists, so that the text beside it starts at the top.
    ImGui::BeginChild("sidebar", ImVec2(width, 0.0f));
    ImGui::BeginChild("scripts", ImVec2(0.0f, ImGui::GetContentRegionAvail().y * 0.55f),
                      ImGuiChildFlags_Borders);
    searchField("##scripts", state.scriptFilter, "Filter Scripts");
    for (const std::filesystem::path& path : state.scriptFiles)
    {
        const std::string name = core::toUtf8(path.filename());
        if (!containsIgnoringCase(name, state.scriptFilter))
        {
            continue;
        }
        const EntityIcon icon = codeIcon(path);
        const ImVec2 position = ImGui::GetCursorScreenPos();
        if (ImGui::Selectable(("      " + name).c_str(), sameTextPath(active, path)))
        {
            chosen = path;
        }
        ImGui::SetItemTooltip("%s", core::toUtf8(path).c_str());
        ImGui::GetWindowDrawList()->AddText(position, uiColorU32(icon.color), icon.icon.c_str());
    }
    ImGui::EndChild();

    ImGui::BeginChild("symbols", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    searchField("##symbols", state.symbolFilter, "Filter Methods");
    if (active.empty())
    {
        ImGui::TextDisabled("No file open.");
    }
    for (const CodeSymbol& symbol : state.textEdit.outline)
    {
        if (!containsIgnoringCase(symbol.name, state.symbolFilter))
        {
            continue;
        }
        const ImVec2 position = ImGui::GetCursorScreenPos();
        ImGui::PushID(symbol.line);
        if (ImGui::Selectable(("      " + symbol.name).c_str()))
        {
            line = symbol.line;
        }
        ImGui::SetItemTooltip("Line %d", symbol.line);
        ImGui::PopID();
        ImGui::GetWindowDrawList()->AddText(position,
                                            uiColorU32(symbol.type ? colors.codeType : colors.codeKeyword),
                                            symbol.type ? icons::Package.c_str() : icons::Code.c_str());
    }
    ImGui::EndChild();
    ImGui::EndChild();

    if (chosen)
    {
        openTextFile(state, *chosen);
    }
    else if (line)
    {
        if (const TextDocument* const document = findTextDocument(state, active))
        {
            goToLine(state.textEdit, *document, *line);
        }
    }
    static_cast<void>(scene);
}

} // namespace

void drawTextEditorPanel(ToolsState& state, scene::Scene& scene)
{
    if (auto path = std::exchange(state.dialogAnswers->openText, std::nullopt))
    {
        openTextFile(state, *path);
    }
    if (!state.showTextEditor)
    {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(900.0f, 620.0f), ImGuiCond_FirstUseEver);
    if (std::exchange(state.focusTextEditor, false))
    {
        ImGui::SetNextWindowFocus();
    }
    if (!ImGui::Begin(textEditorWindow, nullptr))
    {
        ImGui::End();
        return;
    }

    std::optional<PendingAction> action;
    if (labelButton(icons::FolderOpen, "Open..."))
    {
        showOpenTextDialog(state);
    }
    ImGui::SameLine();
    if (labelButton(icons::Save, "Save All"))
    {
        for (TextDocument& document : state.textDocuments)
        {
            if (document.modified())
            {
                static_cast<void>(saveTextFile(state, scene, document));
            }
        }
    }
    if (!state.textOpenError.empty())
    {
        ImGui::TextWrapped("%s", state.textOpenError.c_str());
    }

    drawScriptSidebar(state, scene);
    ImGui::SameLine();
    ImGui::BeginChild("editor", ImVec2(0.0f, 0.0f));
    if (state.textDocuments.empty())
    {
        ImGui::Spacing();
        ImGui::TextWrapped("Choose a script beside this text, open a file from FileSystem, or use "
                           "Edit as Text on a Devex asset.");
    }
    if (ImGui::BeginTabBar("Text files", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs |
                                         ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_TabListPopupButton))
    {
        const std::filesystem::path requestedTab = state.selectTextTab ? state.activeText : std::filesystem::path{};
        for (TextDocument& document : state.textDocuments)
        {
            const std::string path = core::toUtf8(document.path);
            const std::string label = core::toUtf8(document.path.filename()) + "###" + path;
            ImGuiTabItemFlags flags = document.modified() ? ImGuiTabItemFlags_UnsavedDocument : 0;
            if (document.path == requestedTab)
            {
                flags |= ImGuiTabItemFlags_SetSelected;
                state.selectTextTab = false;
            }
            bool open = true;
            const bool selected = ImGui::BeginTabItem(label.c_str(), &open, flags);
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s", path.c_str());
            }
            if (!open)
            {
                action = PendingAction{.kind = PendingAction::Kind::CloseText, .path = document.path};
            }
            if (!selected)
            {
                continue;
            }
            state.activeText = document.path;
            ImGui::PushID(path.c_str());
            ImGui::BeginDisabled(!document.modified());
            if (labelButton(icons::Save, "Save"))
            {
                static_cast<void>(saveTextFile(state, scene, document));
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (labelButton(icons::Refresh, "Reload"))
            {
                action = PendingAction{.kind = PendingAction::Kind::ReloadText, .path = document.path};
            }
            ImGui::SameLine();
            if (labelButton(icons::ExternalLink, "External Editor"))
            {
                openInCodeEditor(state, document.path);
            }
            ImGui::TextDisabled("%s", path.c_str());
            if (!document.error.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, uiColor(themeColors().error));
                ImGui::TextWrapped("%s", document.error.c_str());
                ImGui::PopStyleColor();
            }

            drawFindBar(state, document);
            drawCodeArea(state, document);
            drawGoToLinePopup(state, document);
            ImGui::TextDisabled(
                "Ln %d, Col %d  |  %s  |  UTF-8%s  |  %s  |  Ctrl+S Save   Ctrl+F Find   Ctrl+G Go to line   Ctrl+/ Comment",
                document.line, document.column, std::string(toString(languageOf(document.path))).c_str(),
                document.bom() ? " BOM" : "", document.crlf() ? "CRLF" : "LF");
            ImGui::PopID();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();
    ImGui::End();
    // Closing/reloading may invalidate a document: do this after all widgets have used it.
    if (action)
    {
        requestAction(state, scene, std::move(*action));
    }
}

} // namespace devex::tools::detail
