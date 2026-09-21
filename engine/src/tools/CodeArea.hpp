#pragma once

#include "CodeCompletion.hpp"
#include "CodeHighlight.hpp"

#include <devex/tools/ToolsOverlay.hpp>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct ImGuiInputTextCallbackData;

namespace devex::tools::detail {

class TextDocument;

// What the Text Editor keeps for the document it shows: where the cursor is, what it searched for,
// and the edits it asks ImGui to apply. While a text field is active ImGui owns the characters, so
// every change made by the panel goes through the callback rather than through the string.
struct TextEditState
{
    // A range of the text replaced by something else.
    struct Edit
    {
        int begin = 0;
        int end = 0;
        std::string text;
    };

    std::vector<Edit> edits;
    // Where to leave the cursor once the edits are applied, as a selection.
    std::optional<std::pair<int, int>> pendingCursor;
    int cursor = 0;
    int selectionBegin = 0;
    int selectionEnd = 0;
    // The length of the text at the previous frame, to notice a newline being typed.
    int previousLength = 0;

    // Find and replace.
    bool showFind = false;
    bool showReplace = false;
    bool focusFind = false;
    bool matchCase = false;
    std::string find;
    std::string replace;
    // Where what is searched for appears, and which one is selected.
    std::vector<int> matches;
    int currentMatch = -1;

    // Go to line.
    bool openGoTo = false;
    int goToLine = 1;

    // Completion.
    bool completing = false;
    int completionIndex = 0;
    std::string completionPrefix;
    std::vector<CompletionItem> completions;
    // The names of the engine, gathered once for the language of the document.
    std::vector<CompletionItem> engineNames;
    CodeLanguage namesLanguage = CodeLanguage::PlainText;

    // A line the panel should scroll to before the next frame.
    std::optional<int> revealLine;
    // The document the state belongs to, so that switching tabs starts over.
    std::string path;

    // Where each line starts and whether it opens inside a block comment, rebuilt after an edit so
    // that only the lines on screen are colored.
    std::vector<int> lineStarts;
    std::vector<bool> lineInComment;
    int indexedLength = -1;
    bool textChanged = false;
    // The cursor blinks from the last time it moved.
    int previousCursor = -1;
    double cursorTime = 0.0;
    // Set when the popup or a click chose a completion, applied on the next frame.
    bool completionAccepted = false;
};

// What the callback of the text field works on.
struct CodeAreaCallbackContext
{
    TextEditState* edit = nullptr;
    TextDocument* document = nullptr;
};

// Applies the queued edits, keeps the indentation when a line is typed, and reads the cursor back.
int codeAreaCallback(ImGuiInputTextCallbackData* data);

// The line a byte offset falls on, read from the index instead of counting the newlines again.
[[nodiscard]] int lineOfOffsetIndexed(const TextEditState& edit, int offset);

// Replaces what is being typed with the chosen completion.
void acceptCompletion(TextEditState& edit);

// Rebuilds the list of completions for what is being typed, and closes the popup when there is
// nothing to offer.
void updateCompletion(TextEditState& edit, const TextDocument& document, CodeLanguage language);

// Finds every occurrence of TextEditState::find in the document.
void searchDocument(TextEditState& edit, const TextDocument& document);
// Selects the next match (step 1), the previous one (-1), or the one after the cursor (0).
void selectMatch(TextEditState& edit, const TextDocument& document, int step);
// Replaces the selected match, or every match.
void replaceMatch(TextEditState& edit, const TextDocument& document, bool all);
// Puts the cursor at the start of a line and scrolls to it.
void goToLine(TextEditState& edit, const TextDocument& document, int line);
// Adds or removes the line comment of every line the selection touches.
void commentSelection(TextEditState& edit, const TextDocument& document, CodeLanguage language);

// Removes the spaces and tabs at the end of every line, and reports how many characters went.
std::size_t trimTrailingSpaces(std::string& text);

} // namespace devex::tools::detail
