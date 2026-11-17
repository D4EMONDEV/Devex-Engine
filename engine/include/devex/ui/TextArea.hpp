#pragma once

#include <devex/core/Export.hpp>

#include <devex/asset/FontData.hpp>
#include <devex/math/Math.hpp>
#include <devex/scene/UiComponents.hpp>
#include <devex/ui/Layout.hpp>
#include <devex/ui/TextLayout.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// A text of many lines, as a text area shows and edits it: only the lines in view are ever placed,
// so that a file of thousands of lines costs what its screen holds. The interface world, the drawing
// and the tests share what is here.
namespace devex::ui {

// A run of the text of an area, as bytes of it, and the colour it takes: the words of a language,
// or what a search found when it is drawn behind the letters.
struct DEVEX_API TextSpan
{
    std::size_t begin = 0;
    std::size_t end = 0;
    math::Vec4 color{1.0f};
};

// A line an area marks at its left and underlines, counted from 0: an error, a breakpoint.
struct DEVEX_API TextLineMark
{
    std::uint32_t line = 0;
    math::Vec4 color{1.0f};
};

// A row of an area: the part of a line it shows, as bytes of the text. Every line is one row, unless the
// area wraps it over several.
struct DEVEX_API TextAreaRow
{
    std::uint32_t line = 0;
    std::uint32_t begin = 0;
    // Where it ends, its new line left out.
    std::uint32_t end = 0;
};

// A cursor of an area beside the first: where it stands, where its selection started, equal when nothing is
// selected, and where it wants to stand across while it goes up and down, negative when unset.
struct DEVEX_API TextAreaCaret
{
    std::size_t caret = 0;
    std::size_t anchor = 0;
    float wantedX = -1.0f;
};

// What an area that is being used shows beside its text, which the drawing asks the interface world
// for. Offsets are bytes of the text.
struct DEVEX_API TextAreaView
{
    std::span<const std::uint32_t> lineStarts;
    // Its rows when it wraps or folds; empty when each line is one row.
    std::span<const TextAreaRow> rows;
    // Where the block of each line ends, the line itself where none starts, and the lines whose block is folded, in
    // order: empty for an area that does not fold.
    std::span<const std::uint32_t> foldEnds;
    std::span<const std::uint32_t> folds;
    std::size_t caret = 0;
    std::size_t selectionMin = 0;
    std::size_t selectionMax = 0;
    // Whether it takes what is typed, and whether its cursor shows at this moment of its blink.
    bool focused = false;
    bool caretVisible = false;
    // The width of its longest line, in units: how far it scrolls across.
    float contentWidth = 0.0f;
    // In the order of the text.
    std::span<const TextSpan> spans;
    std::span<const TextSpan> highlights;
    std::span<const TextLineMark> marks;
    // Its cursors beside the first.
    std::span<const TextAreaCaret> carets;
};

// Where each line of a text starts, in bytes: the first at 0, the others after each new line.
DEVEX_API void indexLines(std::string_view text, std::vector<std::uint32_t>& starts);
// The line a byte of the text is on, from 0, and the bytes of a line without its new line.
[[nodiscard]] DEVEX_API std::size_t lineOfOffset(std::span<const std::uint32_t> starts, std::size_t offset) noexcept;
[[nodiscard]] DEVEX_API std::string_view lineText(std::string_view text, std::span<const std::uint32_t> starts, std::size_t line) noexcept;
// The rows of an area, its lines when it does not wrap: how many, the row a byte is on (the last of its line that
// starts at it or before), and one of them.
[[nodiscard]] DEVEX_API std::size_t areaRowCount(std::span<const TextAreaRow> rows, std::span<const std::uint32_t> starts) noexcept;
[[nodiscard]] DEVEX_API std::size_t areaRowOf(std::span<const TextAreaRow> rows, std::span<const std::uint32_t> starts, std::size_t offset) noexcept;
[[nodiscard]] DEVEX_API TextAreaRow areaRow(std::span<const TextAreaRow> rows, std::span<const std::uint32_t> starts, std::size_t textSize,
                                            std::size_t row) noexcept;

// Where an area puts things inside its rectangle, in units of its canvas.
struct DEVEX_API TextAreaMetrics
{
    float lineHeight = 0.0f;
    // The room of the numbers of the lines and of the arrows that fold, at the left; none without them.
    float gutter = 0.0f;
    // The room of the arrows, at the right of the gutter; none for an area that does not fold.
    float foldColumn = 0.0f;
    // The room the letters show in: what is left once the gutter, the padding and the bars are taken.
    math::Vec2 viewMin{0.0f};
    math::Vec2 viewMax{0.0f};
    // Where the first letter of the first line stands when nothing is scrolled.
    math::Vec2 origin{0.0f};
    // The height of every line together.
    float contentHeight = 0.0f;

    [[nodiscard]] math::Vec2 viewSize() const noexcept
    {
        return math::Vec2{std::max(viewMax.x - viewMin.x, 0.0f), std::max(viewMax.y - viewMin.y, 0.0f)};
    }
};
// The gutter is as wide as the number of the last line; the content as tall as the rows, its lines when 0.
[[nodiscard]] DEVEX_API TextAreaMetrics textAreaMetrics(const asset::FontData& font, const scene::UiText& text, const scene::UiTextArea& area,
                                                        const LaidOutRect& rect, std::size_t lineCount, std::size_t rowCount = 0);

// How far an area scrolls, and the thumbs of its bars, in units of its canvas. A thumb is empty while
// the text fits that way.
struct DEVEX_API TextAreaBars
{
    math::Vec2 maxScroll{0.0f};
    math::Vec2 verticalMin{0.0f};
    math::Vec2 verticalMax{0.0f};
    math::Vec2 horizontalMin{0.0f};
    math::Vec2 horizontalMax{0.0f};
    // How far each thumb travels along its track: across, then down.
    math::Vec2 travel{0.0f};

    [[nodiscard]] bool vertical() const noexcept
    {
        return verticalMax.y > verticalMin.y;
    }
    [[nodiscard]] bool horizontal() const noexcept
    {
        return horizontalMax.x > horizontalMin.x;
    }
};
// `contentWidth` is the width of the longest line.
[[nodiscard]] DEVEX_API TextAreaBars textAreaBars(const TextAreaMetrics& metrics, const scene::UiText& text, const scene::UiTextArea& area,
                                                  const LaidOutRect& rect, float contentWidth) noexcept;

// One line of an area, placed: its letters, and where the cursor stands before each of its bytes.
struct DEVEX_API TextAreaLine
{
    TextLayoutResult layout;
    // The text as it is placed, which differs from the line when it holds tabs, and the byte of the
    // line each byte of it comes from; empty when they are the same.
    std::string shown;
    std::vector<std::uint32_t> sources;
    // How many bytes the line holds, and where it starts across: where the cursor stands in a line
    // whose font is missing.
    std::size_t length = 0;
    float left = 0.0f;

    // Where the cursor stands before a byte of the line, across.
    [[nodiscard]] float xOf(std::size_t offset) const noexcept;
    // The byte of the line the cursor is nearest to at a place across.
    [[nodiscard]] std::size_t offsetAt(float x) const noexcept;
    // The byte of the line a byte of the placed text comes from.
    [[nodiscard]] std::size_t sourceOf(std::size_t shownOffset) const noexcept;
};
// Places a line from a point, its top left corner; a tab is as wide as `tabSize` spaces.
DEVEX_API void layoutAreaLine(const asset::FontData& font, std::string_view line, float size, std::int32_t tabSize, math::Vec2 origin,
                              TextAreaLine& placed);

// The start and the end of the word around a byte of a text: letters, digits and underscores, or
// whatever single character stands there.
[[nodiscard]] DEVEX_API std::pair<std::size_t, std::size_t> wordAround(std::string_view text, std::size_t offset) noexcept;
// The rows of an area that wraps: each line cut where it would pass `width`, after the last space before it when
// there is one, and else before the letter that passes it.
DEVEX_API void wrapAreaRows(const asset::FontData& font, std::string_view text, std::span<const std::uint32_t> starts, float size,
                            std::int32_t tabSize, float width, std::vector<TextAreaRow>& rows);
// Where the block that starts at each line ends: the last line folding it hides, or the line itself where none
// starts. A line with a brace that closes lower folds down to the line of that brace, which stays when it starts with
// it; a line followed by a brace alone folds that brace, what it holds and the one that closes it; any other line
// folds the lines under it that are more indented, to the last that is not empty. Braces in strings and comments
// count for nothing.
DEVEX_API void foldAreaLines(std::string_view text, std::span<const std::uint32_t> starts, std::vector<std::uint32_t>& ends);
// Where the word before an offset starts, and where the one after it ends, skipping the spaces on
// the way: where Ctrl and an arrow take the cursor.
[[nodiscard]] DEVEX_API std::size_t previousWord(std::string_view text, std::size_t offset) noexcept;
[[nodiscard]] DEVEX_API std::size_t nextWord(std::string_view text, std::size_t offset) noexcept;

} // namespace devex::ui
