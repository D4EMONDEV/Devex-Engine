#pragma once

#include <devex/core/Error.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

struct ImFontConfig;
struct ImFontLoader;

namespace devex::tools::detail {

// The icons of the editor: Lucide icons (third_party/lucide) and the Devex logo, by name and SVG file.
// Each icon is a character of Unicode's private use area, in this order, so that it can be written
// in any ImGui text and follows the size of the font.
#define DEVEX_EDITOR_ICONS(ICON)                     \
    ICON(Activity, "activity")                       \
    ICON(Anchor, "anchor")                           \
    ICON(ArrowDownToLine, "arrow-down-to-line")      \
    ICON(ArrowUpDown, "arrow-up-down")               \
    ICON(AudioWaveform, "audio-waveform")            \
    ICON(Axis, "axis-3d")                            \
    ICON(BookOpen, "book-open")                      \
    ICON(Box, "box")                                 \
    ICON(BrushCleaning, "brush-cleaning")            \
    ICON(Bug, "bug")                                 \
    ICON(Check, "check")                             \
    ICON(ChevronDown, "chevron-down")                \
    ICON(ChevronRight, "chevron-right")              \
    ICON(Circle, "circle")                           \
    ICON(CircleCheck, "circle-check")                \
    ICON(CircleDashed, "circle-dashed")              \
    ICON(CircleHelp, "circle-question-mark")         \
    ICON(CircleX, "circle-x")                        \
    ICON(Clapperboard, "clapperboard")               \
    ICON(Clock, "clock")                             \
    ICON(Close, "x")                                 \
    ICON(CloudSun, "cloud-sun")                      \
    ICON(Code, "code")                               \
    ICON(Copy, "copy")                               \
    ICON(Crosshair, "crosshair")                     \
    ICON(Cuboid, "cuboid")                           \
    ICON(Cylinder, "cylinder")                       \
    ICON(Ear, "ear")                                 \
    ICON(Ellipsis, "ellipsis-vertical")              \
    ICON(ExternalLink, "external-link")              \
    ICON(Eye, "eye")                                 \
    ICON(EyeOff, "eye-off")                          \
    ICON(File, "file")                               \
    ICON(FileCode, "file-code")                      \
    ICON(FilePlus, "file-plus")                      \
    ICON(FileText, "file-text")                      \
    ICON(Flashlight, "flashlight")                   \
    ICON(Folder, "folder")                           \
    ICON(FolderOpen, "folder-open")                  \
    ICON(FolderSearch, "folder-search")              \
    ICON(FolderTree, "folder-tree")                  \
    ICON(Funnel, "funnel")                           \
    ICON(Gauge, "gauge")                             \
    ICON(Globe, "globe")                             \
    ICON(Grid, "grid-3x3")                           \
    ICON(GridCheck, "grid-2x2-check")                \
    ICON(Hammer, "hammer")                           \
    ICON(House, "house")                             \
    ICON(Image, "image")                             \
    ICON(Info, "info")                               \
    ICON(Keyboard, "keyboard")                       \
    ICON(Layers, "layers")                           \
    ICON(LayoutDashboard, "layout-dashboard")        \
    ICON(Lightbulb, "lightbulb")                     \
    ICON(Link, "link")                               \
    ICON(ListTree, "list-tree")                      \
    ICON(ListX, "list-x")                            \
    ICON(Loader, "loader-circle")                    \
    ICON(LogOut, "log-out")                          \
    ICON(Magnet, "magnet")                           \
    ICON(Minus, "minus")                             \
    ICON(Monitor, "monitor")                         \
    ICON(Mountain, "mountain-snow")                  \
    ICON(Move, "move")                               \
    ICON(Move3d, "move-3d")                          \
    ICON(Package, "package")                         \
    ICON(Palette, "palette")                         \
    ICON(Pause, "pause")                             \
    ICON(Pencil, "pencil")                           \
    ICON(PersonStanding, "person-standing")          \
    ICON(Play, "play")                               \
    ICON(Plus, "plus")                               \
    ICON(Pointer, "mouse-pointer-2")                 \
    ICON(Puzzle, "puzzle")                           \
    ICON(Redo, "redo-2")                             \
    ICON(Refresh, "refresh-cw")                      \
    ICON(Rotate, "rotate-3d")                        \
    ICON(Save, "save")                               \
    ICON(Scale, "scale-3d")                          \
    ICON(Scan, "scan")                               \
    ICON(ScrollText, "scroll-text")                  \
    ICON(Search, "search")                           \
    ICON(Settings, "settings")                       \
    ICON(Shapes, "shapes")                           \
    ICON(Sliders, "sliders-horizontal")              \
    ICON(Sparkles, "sparkles")                       \
    ICON(Square, "square")                           \
    ICON(SquareDashed, "square-dashed")              \
    ICON(Star, "star")                               \
    ICON(StepForward, "step-forward")                \
    ICON(Sun, "sun")                                 \
    ICON(Terminal, "terminal")                       \
    ICON(TextCursor, "text-cursor-input")            \
    ICON(Trash, "trash")                             \
    ICON(TriangleAlert, "triangle-alert")            \
    ICON(Type, "type")                               \
    ICON(Undo, "undo-2")                             \
    ICON(Unlink, "unlink")                           \
    ICON(Video, "video")                             \
    ICON(Volume, "volume-2")                         \
    ICON(Weight, "weight")                           \
    ICON(ZoomIn, "zoom-in")                          \
    ICON(Logo, "devex")

enum class Icon : std::uint16_t
{
#define DEVEX_ICON_ENUMERATOR(name, file) name,
    DEVEX_EDITOR_ICONS(DEVEX_ICON_ENUMERATOR)
#undef DEVEX_ICON_ENUMERATOR
    Count,
};

// The character of the first icon.
inline constexpr char32_t firstIconCodepoint = 0xE000;

[[nodiscard]] constexpr char32_t codepointOf(Icon icon) noexcept
{
    return firstIconCodepoint + static_cast<char32_t>(icon);
}

// The UTF-8 encoding of an icon's character, to write it in ImGui text.
class IconText
{
public:
    constexpr explicit IconText(Icon icon) noexcept
        : m_bytes{static_cast<char>(0xE0 | (codepointOf(icon) >> 12)),
                  static_cast<char>(0x80 | ((codepointOf(icon) >> 6) & 0x3F)),
                  static_cast<char>(0x80 | (codepointOf(icon) & 0x3F)), '\0'}
    {
    }

    [[nodiscard]] constexpr const char* c_str() const noexcept
    {
        return m_bytes.data();
    }

    [[nodiscard]] constexpr operator std::string_view() const noexcept
    {
        return {m_bytes.data(), 3};
    }

private:
    std::array<char, 4> m_bytes;
};

namespace icons {
#define DEVEX_ICON_TEXT(name, file) inline constexpr IconText name{Icon::name};
DEVEX_EDITOR_ICONS(DEVEX_ICON_TEXT)
#undef DEVEX_ICON_TEXT
} // namespace icons

// "icon  label", with the spacing used by menus and buttons.
[[nodiscard]] std::string withIcon(IconText icon, std::string_view label);

// The SVG file of an icon, without its extension.
[[nodiscard]] std::string_view iconFileName(Icon icon) noexcept;

// The pixels of a rendered SVG document: 8-bit RGBA, not premultiplied.
struct SvgImage
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;
};

// Renders an SVG document at a size in pixels, with CSS currentColor as white.
[[nodiscard]] core::Result<SvgImage> renderSvg(std::string_view svg, std::uint32_t width, std::uint32_t height);

// The icon documents, loaded from the SVG files of a directory. Icons whose file is missing draw
// nothing.
class IconSet
{
public:
    [[nodiscard]] static IconSet load(const std::filesystem::path& directory);

    [[nodiscard]] bool contains(Icon icon) const noexcept;
    [[nodiscard]] std::string_view svg(Icon icon) const noexcept;
    // Icons drawn with their own colors rather than the color of the text.
    [[nodiscard]] static bool isColored(Icon icon) noexcept;

private:
    std::array<std::string, static_cast<std::size_t>(Icon::Count)> m_documents;
};

// The ImGui font loader that draws the icons of a set as glyphs. A font source using it takes the
// set in FontLoaderData, which must outlive the font atlas.
[[nodiscard]] const ImFontLoader& iconFontLoader() noexcept;
// A font source to merge into a font so that its text can show the icons.
[[nodiscard]] ImFontConfig iconFontSource(IconSet& icons);

} // namespace devex::tools::detail
