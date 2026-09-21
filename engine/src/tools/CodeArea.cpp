#include "CodeArea.hpp"

#include "TextDocument.hpp"

#include "CodeCompletion.hpp"
#include "CodeHighlight.hpp"
#include "ToolsState.hpp"

#include <devex/core/Path.hpp>

#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace devex::tools::detail {
namespace {

[[nodiscard]] bool isIdentifierPart(char value) noexcept
{
    return std::isalnum(static_cast<unsigned char>(value)) != 0 || value == '_';
}

// The byte offset where a line starts, counting from zero.
[[nodiscard]] std::size_t offsetOfLine(std::string_view text, int line)
{
    std::size_t offset = 0;
    for (int current = 1; current < line; ++current)
    {
        const std::size_t next = text.find('\n', offset);
        if (next == std::string_view::npos)
        {
            return text.size();
        }
        offset = next + 1;
    }
    return std::min(offset, text.size());
}

[[nodiscard]] int lineOfOffset(std::string_view text, std::size_t offset)
{
    return 1 + static_cast<int>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(
                                                             std::min(offset, text.size())),
                                           '\n'));
}

// The start of the line holding an offset, and the end of the line before the newline.
[[nodiscard]] std::pair<std::size_t, std::size_t> lineBounds(std::string_view text, std::size_t offset)
{
    offset = std::min(offset, text.size());
    const std::size_t begin = text.rfind('\n', offset == 0 ? 0 : offset - 1);
    const std::size_t end = text.find('\n', offset);
    return {begin == std::string_view::npos ? 0 : begin + 1,
            end == std::string_view::npos ? text.size() : end};
}

[[nodiscard]] char lowered(char value) noexcept
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
}

// Every place where what is searched for appears, as byte offsets.
[[nodiscard]] std::vector<int> findMatches(std::string_view text, std::string_view needle, bool matchCase)
{
    std::vector<int> matches;
    if (needle.empty())
    {
        return matches;
    }
    for (std::size_t index = 0; index + needle.size() <= text.size(); ++index)
    {
        const bool same =
            matchCase ? text.compare(index, needle.size(), needle) == 0
                      : std::ranges::equal(text.substr(index, needle.size()), needle,
                                           [](char left, char right) { return lowered(left) == lowered(right); });
        if (same)
        {
            matches.push_back(static_cast<int>(index));
            index += needle.size() - 1;
        }
    }
    return matches;
}

// The indentation of the line holding an offset: what a new line below it starts with.
[[nodiscard]] std::string indentationAt(std::string_view text, std::size_t offset)
{
    const auto [begin, end] = lineBounds(text, offset);
    std::size_t stop = begin;
    while (stop < end && (text[stop] == ' ' || text[stop] == '\t'))
    {
        ++stop;
    }
    std::string indentation(text.substr(begin, stop - begin));
    // A line that opens a block indents the next one.
    std::string_view trimmed = text.substr(begin, end - begin);
    while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t'))
    {
        trimmed.remove_suffix(1);
    }
    if (!trimmed.empty() && (trimmed.back() == '{' || trimmed.back() == '(' || trimmed.back() == ':'))
    {
        indentation += "    ";
    }
    return indentation;
}

// Adds or removes the line comment of every line the selection touches.
[[nodiscard]] TextEditState::Edit toggleComment(std::string_view text, std::size_t selectionBegin,
                                                std::size_t selectionEnd, std::string_view comment)
{
    const auto [begin, unusedEnd] = lineBounds(text, selectionBegin);
    static_cast<void>(unusedEnd);
    const auto [unusedBegin, end] = lineBounds(text, selectionEnd);
    static_cast<void>(unusedBegin);
    const std::string_view block = text.substr(begin, end - begin);

    // Lines already commented lose their marker; otherwise every line gets one.
    bool allCommented = true;
    for (std::size_t index = 0; index <= block.size(); ++index)
    {
        const std::size_t stop = block.find('\n', index);
        std::string_view line = block.substr(index, (stop == std::string_view::npos ? block.size() : stop) - index);
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
        {
            line.remove_prefix(1);
        }
        if (!line.empty() && !line.starts_with(comment))
        {
            allCommented = false;
        }
        if (stop == std::string_view::npos)
        {
            break;
        }
        index = stop;
    }

    std::string changed;
    for (std::size_t index = 0; index <= block.size(); ++index)
    {
        const std::size_t stop = block.find('\n', index);
        const std::string_view line = block.substr(index, (stop == std::string_view::npos ? block.size() : stop) - index);
        const std::size_t indent = line.find_first_not_of(" \t");
        if (indent == std::string_view::npos)
        {
            changed += line;
        }
        else if (allCommented)
        {
            std::string_view rest = line.substr(indent + comment.size());
            // The space written after the marker goes with it.
            if (rest.starts_with(' '))
            {
                rest.remove_prefix(1);
            }
            changed += line.substr(0, indent);
            changed += rest;
        }
        else
        {
            changed += line.substr(0, indent);
            changed += comment;
            changed += ' ';
            changed += line.substr(indent);
        }
        if (stop == std::string_view::npos)
        {
            break;
        }
        changed += '\n';
        index = stop;
    }
    return {static_cast<int>(begin), static_cast<int>(end), std::move(changed)};
}

} // namespace

// Applies what the panel asked for on ImGui's own buffer, and reports where the cursor is.
int codeAreaCallback(ImGuiInputTextCallbackData* data)
{
    auto& context = *static_cast<CodeAreaCallbackContext*>(data->UserData);
    TextEditState& edit = *context.edit;
    TextDocument& document = *context.document;

    if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion)
    {
        // Tab takes the selected completion, or indents when the popup is closed.
        if (edit.completing && !edit.completions.empty())
        {
            const CompletionItem& item = edit.completions[static_cast<std::size_t>(edit.completionIndex)];
            const int begin = data->CursorPos - static_cast<int>(edit.completionPrefix.size());
            data->DeleteChars(begin, static_cast<int>(edit.completionPrefix.size()));
            data->InsertChars(begin, item.text.c_str());
            edit.completing = false;
        }
        else
        {
            data->InsertChars(data->CursorPos, "    ");
        }
        return 0;
    }
    // CallbackAlways: apply the edits the panel queued, then read the cursor back.
    for (const TextEditState::Edit& pending : edit.edits)
    {
        const int begin = std::clamp(pending.begin, 0, data->BufTextLen);
        const int end = std::clamp(pending.end, begin, data->BufTextLen);
        data->DeleteChars(begin, end - begin);
        if (!pending.text.empty())
        {
            data->InsertChars(begin, pending.text.c_str());
        }
    }
    edit.edits.clear();

    if (edit.pendingCursor)
    {
        const auto [begin, end] = *std::exchange(edit.pendingCursor, std::nullopt);
        data->CursorPos = std::clamp(end, 0, data->BufTextLen);
        data->SelectionStart = std::clamp(begin, 0, data->BufTextLen);
        data->SelectionEnd = data->CursorPos;
    }

    // Typing a newline keeps the indentation of the line above.
    const bool inserted = data->BufTextLen == edit.previousLength + 1;
    if (inserted && data->CursorPos > 0 && data->Buf[data->CursorPos - 1] == '\n')
    {
        const std::string indentation =
            indentationAt(std::string_view(data->Buf, static_cast<std::size_t>(data->BufTextLen)),
                          static_cast<std::size_t>(data->CursorPos) - 1);
        if (!indentation.empty())
        {
            data->InsertChars(data->CursorPos, indentation.c_str());
        }
    }
    edit.previousLength = data->BufTextLen;

    edit.cursor = data->CursorPos;
    edit.selectionBegin = data->SelectionStart;
    edit.selectionEnd = data->SelectionEnd;
    const std::string_view text(data->Buf, static_cast<std::size_t>(data->BufTextLen));
    document.line = lineOfOffset(text, static_cast<std::size_t>(data->CursorPos));
    document.column = 1 + static_cast<int>(static_cast<std::size_t>(data->CursorPos) -
                                           lineBounds(text, static_cast<std::size_t>(data->CursorPos)).first);
    return 0;
}

void acceptCompletion(TextEditState& edit)
{
    if (!edit.completing || edit.completions.empty())
    {
        return;
    }
    const CompletionItem& item = edit.completions[static_cast<std::size_t>(
        std::clamp(edit.completionIndex, 0, static_cast<int>(edit.completions.size()) - 1))];
    const int begin = edit.cursor - static_cast<int>(edit.completionPrefix.size());
    edit.edits.push_back({begin, edit.cursor, item.text});
    const int end = begin + static_cast<int>(item.text.size());
    edit.pendingCursor = std::pair{end, end};
    edit.completing = false;
    edit.completions.clear();
}

void updateCompletion(TextEditState& edit, const TextDocument& document, CodeLanguage language)
{
    const std::string_view prefix =
        identifierBefore(document.text, static_cast<std::size_t>(std::max(edit.cursor, 0)));
    if (prefix.size() < 2 || language == CodeLanguage::PlainText)
    {
        edit.completing = false;
        edit.completions.clear();
        return;
    }
    if (prefix == edit.completionPrefix && !edit.completions.empty())
    {
        return;
    }
    edit.completionPrefix = prefix;
    edit.completions = completionsFor(prefix, language, document.text, edit.engineNames,
                                      static_cast<std::size_t>(std::max(edit.cursor, 0)));
    edit.completing = !edit.completions.empty();
    edit.completionIndex = std::clamp(edit.completionIndex, 0,
                                      std::max(0, static_cast<int>(edit.completions.size()) - 1));
}

void searchDocument(TextEditState& edit, const TextDocument& document)
{
    edit.matches = findMatches(document.text, edit.find, edit.matchCase);
    if (edit.matches.empty())
    {
        edit.currentMatch = -1;
        return;
    }
    edit.currentMatch = std::clamp(edit.currentMatch, 0, static_cast<int>(edit.matches.size()) - 1);
}

void selectMatch(TextEditState& edit, const TextDocument& document, int step)
{
    if (edit.matches.empty())
    {
        return;
    }
    const int count = static_cast<int>(edit.matches.size());
    if (edit.currentMatch < 0)
    {
        // Start from the match after the cursor.
        const auto next = std::ranges::find_if(edit.matches, [&](int offset) { return offset >= edit.cursor; });
        edit.currentMatch = next == edit.matches.end() ? 0 : static_cast<int>(next - edit.matches.begin());
    }
    else
    {
        edit.currentMatch = (edit.currentMatch + step + count) % count;
    }
    const int begin = edit.matches[static_cast<std::size_t>(edit.currentMatch)];
    edit.pendingCursor = std::pair{begin, begin + static_cast<int>(edit.find.size())};
    edit.revealLine = lineOfOffset(document.text, static_cast<std::size_t>(begin));
}

void replaceMatch(TextEditState& edit, const TextDocument& document, bool all)
{
    if (edit.matches.empty() || edit.find.empty())
    {
        return;
    }
    if (all)
    {
        // From the end, so that the offsets of the matches before it stay valid.
        for (auto match = edit.matches.rbegin(); match != edit.matches.rend(); ++match)
        {
            edit.edits.push_back({*match, *match + static_cast<int>(edit.find.size()), edit.replace});
        }
        edit.currentMatch = -1;
        return;
    }
    if (edit.currentMatch < 0)
    {
        selectMatch(edit, document, 0);
        return;
    }
    const int begin = edit.matches[static_cast<std::size_t>(edit.currentMatch)];
    edit.edits.push_back({begin, begin + static_cast<int>(edit.find.size()), edit.replace});
    edit.pendingCursor = std::pair{begin + static_cast<int>(edit.replace.size()),
                                   begin + static_cast<int>(edit.replace.size())};
}

void goToLine(TextEditState& edit, const TextDocument& document, int line)
{
    const int clamped = std::max(1, line);
    const auto offset = static_cast<int>(offsetOfLine(document.text, clamped));
    edit.pendingCursor = std::pair{offset, offset};
    edit.revealLine = clamped;
}

void commentSelection(TextEditState& edit, const TextDocument& document, CodeLanguage language)
{
    const std::string_view comment = lineCommentOf(language);
    if (comment.empty())
    {
        return;
    }
    const auto begin = static_cast<std::size_t>(std::min(edit.selectionBegin, edit.selectionEnd));
    const auto end = static_cast<std::size_t>(std::max(edit.selectionBegin, edit.selectionEnd));
    edit.edits.push_back(toggleComment(document.text, begin, end, comment));
}

std::size_t trimTrailingSpaces(std::string& text)
{
    std::string trimmed;
    trimmed.reserve(text.size());
    std::size_t removed = 0;
    std::size_t index = 0;
    while (index <= text.size())
    {
        const std::size_t stop = text.find('\n', index);
        const std::size_t end = stop == std::string::npos ? text.size() : stop;
        std::size_t last = end;
        while (last > index && (text[last - 1] == ' ' || text[last - 1] == '\t'))
        {
            --last;
        }
        removed += end - last;
        trimmed.append(text, index, last - index);
        if (stop == std::string::npos)
        {
            break;
        }
        trimmed += '\n';
        index = stop + 1;
    }
    if (removed > 0)
    {
        text = std::move(trimmed);
    }
    return removed;
}

} // namespace devex::tools::detail
