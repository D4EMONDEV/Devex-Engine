// The places of the screen the editor shows its images in: which one the pointer is over, which one
// has the keyboard, and the order they are drawn in. A host is declared at every frame where it
// stands; the pointer finds it the next frame.
#pragma once

#include <devex/core/Export.hpp>
#include <devex/math/Math.hpp>
#include <devex/render/RenderWorld.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace devex::tools::detail {

// The window the tools stand in, as the frame measured it: its size in points, and how many pixels
// of the window a point covers.
struct DEVEX_API EditorScreen
{
    math::Vec2 size{0.0f, 0.0f};
    float pixelsPerPoint = 1.0f;
};

DEVEX_API void setEditorScreen(const EditorScreen& screen) noexcept;
[[nodiscard]] DEVEX_API const EditorScreen& editorScreen() noexcept;

// An image a host shows: the scene drawn for the tools, or the image of an interface surface.
struct DEVEX_API HostImage
{
    render::UiSource source = render::UiSource::Surface;
    std::uint32_t surface = 0;
};

// From the bottom up: the dock behind the panels, the panels and the screens, the strips of the
// frame, the modals, and the menus and tooltips over everything.
enum class HostLayer : std::uint8_t
{
    Dock,
    Panels,
    Strips,
    Modal,
    Menus,
};

struct DEVEX_API HostOptions
{
    // A click gives it the keyboard; the strips and the dock leave the keyboard where it was.
    bool focusable = true;
    // Answers the pointer; what only shows, such as the tooltips, lets it through.
    bool takesPointer = true;
    // Where the pointer goes through to what is behind the tools: the game under the panels.
    std::optional<std::pair<math::Vec2, math::Vec2>> hole;
    // Painted under what the host shows, in linear colours, and the room kept inside its edges.
    std::optional<math::Vec4> background;
    float padding = 0.0f;
};

class DEVEX_API EditorHosts
{
public:
    // Finds the host under the pointer among those of the last frame; a press gives the keyboard to
    // it, or takes the keyboard from the tools when it lands on no host.
    void beginFrame(math::Vec2 pointer, bool pressed);

    void begin(std::string_view id, math::Vec2 min, math::Vec2 max, HostLayer layer, const HostOptions& options = {});
    void end();

    // Where the next image goes in the host being declared, and the room left from there.
    [[nodiscard]] math::Vec2 cursor() const noexcept;
    void setCursor(math::Vec2 at) noexcept;
    [[nodiscard]] math::Vec2 available() const noexcept;
    // Shows an image at the cursor and moves the cursor under it.
    void image(HostImage shown, math::Vec2 size);
    // Whether the pointer is over the last image, in the host it is over.
    [[nodiscard]] bool itemHovered() const noexcept;
    // Whether the host being declared is under the pointer, and has the keyboard, it or the host it
    // stands in.
    [[nodiscard]] bool hovered() const noexcept;
    [[nodiscard]] bool focused() const noexcept;
    [[nodiscard]] bool isFocused(std::string_view id) const noexcept;
    [[nodiscard]] const std::string& focusedId() const noexcept;
    [[nodiscard]] const std::string& hoveredId() const noexcept;
    // Gives the keyboard to a host, as a click on it does; or to the one being declared.
    void focus(std::string_view id);
    void focusCurrent();
    // Whether the pointer is over any host: the tools have it, not the game.
    [[nodiscard]] bool pointerTaken() const noexcept;

    // Adds the backgrounds and the images of the frame to the layer of the tools, from the bottom
    // layer up, in pixels of the window.
    void compose(render::RenderWorld& world) const;

private:
    struct Host
    {
        std::string id;
        std::string root;
        math::Vec2 min{0.0f, 0.0f};
        math::Vec2 max{0.0f, 0.0f};
        HostLayer layer = HostLayer::Panels;
        std::size_t order = 0;
        HostOptions options;
    };
    struct Draw
    {
        HostLayer layer = HostLayer::Panels;
        std::size_t order = 0;
        std::size_t sequence = 0;
        // Without an image, the colour fills the place.
        std::optional<HostImage> image;
        math::Vec2 min{0.0f, 0.0f};
        math::Vec2 max{0.0f, 0.0f};
        math::Vec4 color{1.0f};
    };
    struct Open
    {
        std::size_t host = 0;
        math::Vec2 cursor{0.0f, 0.0f};
        math::Vec2 contentMax{0.0f, 0.0f};
        math::Vec2 itemMin{0.0f, 0.0f};
        math::Vec2 itemMax{0.0f, 0.0f};
    };

    [[nodiscard]] const Host* current() const noexcept;

    std::vector<Host> m_hosts;
    std::vector<Host> m_last;
    std::vector<Open> m_stack;
    std::vector<Draw> m_draws;
    std::string m_hovered;
    std::string m_hoveredRoot;
    std::string m_focused;
    std::string m_focusedRoot;
    math::Vec2 m_pointer{-1.0e6f, -1.0e6f};
};

} // namespace devex::tools::detail
