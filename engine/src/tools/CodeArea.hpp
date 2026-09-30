#pragma once

#include <devex/core/Export.hpp>

#include "CodeCompletion.hpp"
#include "CodeHighlight.hpp"
#include "CodeOutline.hpp"

#include <devex/tools/ToolsOverlay.hpp>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace devex::tools::detail {

class TextDocument;

// What the Text Editor keeps for the document it shows: where the cursor is, what it searched for,
// and the edits it asks for. The area of text that shows the document owns its letters: the changes
// the panel asks for wait here until the panel writes them at its next update, all at once, so that
// the area goes back on them in one step.
struct DEVEX_API TextEditState
{
    // A range of the text replaced by something else.
    struct DEVEX_API Edit
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
    // What was being typed when a completion was taken or the list closed: the list stays closed
    // until something else is typed.
    std::string dismissedPrefix;
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
    // The types and functions of the document, read with its lines.
    std::vector<CodeSymbol> outline;
    // Set when the popup or a click chose a completion, applied on the next frame.
    bool completionAccepted = false;
    // Asked by the Edit menu, answered by the area of text at the next update of the panel.
    bool requestUndo = false;
    bool requestRedo = false;
};

// The line a byte offset falls on, read from the index instead of counting the newlines again.
[[nodiscard]] DEVEX_API int lineOfOffsetIndexed(const TextEditState& edit, int offset);

// Replaces what is being typed with the chosen completion.
DEVEX_API void acceptCompletion(TextEditState& edit);

// Rebuilds the list of completions for what is being typed, and closes the popup when there is
// nothing to offer.
DEVEX_API void updateCompletion(TextEditState& edit, const TextDocument& document, CodeLanguage language);

// Finds every occurrence of TextEditState::find in the document.
DEVEX_API void searchDocument(TextEditState& edit, const TextDocument& document);
// Selects the next match (step 1), the previous one (-1), or the one after the cursor (0).
DEVEX_API void selectMatch(TextEditState& edit, const TextDocument& document, int step);
// Replaces the selected match, or every match.
DEVEX_API void replaceMatch(TextEditState& edit, const TextDocument& document, bool all);
// Puts the cursor at the start of a line and scrolls to it.
DEVEX_API void goToLine(TextEditState& edit, const TextDocument& document, int line);
// Adds or removes the line comment of every line the selection touches.
DEVEX_API void commentSelection(TextEditState& edit, const TextDocument& document, CodeLanguage language);

// Removes the spaces and tabs at the end of every line, and reports how many characters went.
DEVEX_API std::size_t trimTrailingSpaces(std::string& text);

} // namespace devex::tools::detail
