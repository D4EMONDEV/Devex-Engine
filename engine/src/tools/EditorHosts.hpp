// The places of the screen the editor shows its images in, in place of ImGui's windows: which one the
// pointer is over, which one has the keyboard, and the order they are drawn in. A host is declared at
// every frame where it stands; the pointer finds it the next frame, as ImGui finds its windows.
#pragma once

#include <devex/core/Export.hpp>
#include <devex/math/Math.hpp>

#include <imgui.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace devex::tools::detail {

// A point of the editor's input as ImGui's draw lists take it.
[[nodiscard]] inline ImVec2 pointOf(math::Vec2 point) noexcept
{
    return ImVec2(point.x, point.y);
}

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
    std::optional<std::pair<ImVec2, ImVec2>> hole;
    // Painted under what the host shows, as the background of a window, and the room kept inside
    // its edges.
    std::optional<ImVec4> background;
    float padding = 0.0f;
};

class DEVEX_API EditorHosts
{
public:
    // Finds the host under the pointer among those of the last frame; a press gives the keyboard to
    // it, or takes the keyboard from the tools when it lands on no host.
    void beginFrame(math::Vec2 pointer, bool pressed);

    void begin(std::string_view id, ImVec2 min, ImVec2 max, HostLayer layer, const HostOptions& options = {});
    void end();

    // Where the next image goes in the host being declared, and the room left from there.
    [[nodiscard]] ImVec2 cursor() const noexcept;
    void setCursor(ImVec2 at) noexcept;
    [[nodiscard]] ImVec2 available() const noexcept;
    // Shows an image at the cursor and moves the cursor under it.
    void image(std::uint64_t texture, ImVec2 size);
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

    // Draws the backgrounds and the images of the frame, from the bottom layer up.
    void compose(ImDrawList& list);

private:
    struct Host
    {
        std::string id;
        std::string root;
        ImVec2 min{0.0f, 0.0f};
        ImVec2 max{0.0f, 0.0f};
        HostLayer layer = HostLayer::Panels;
        std::size_t order = 0;
        HostOptions options;
    };
    struct Draw
    {
        HostLayer layer = HostLayer::Panels;
        std::size_t order = 0;
        std::size_t sequence = 0;
        std::uint64_t texture = 0;
        ImVec2 min{0.0f, 0.0f};
        ImVec2 max{0.0f, 0.0f};
        ImU32 color = 0;
    };
    struct Open
    {
        std::size_t host = 0;
        ImVec2 cursor{0.0f, 0.0f};
        ImVec2 contentMax{0.0f, 0.0f};
        ImVec2 itemMin{0.0f, 0.0f};
        ImVec2 itemMax{0.0f, 0.0f};
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
    ImVec2 m_pointer{-1.0e6f, -1.0e6f};
};

} // namespace devex::tools::detail
