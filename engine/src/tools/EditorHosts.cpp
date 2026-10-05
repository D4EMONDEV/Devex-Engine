#include "EditorHosts.hpp"

#include <algorithm>
#include <cmath>

namespace devex::tools::detail {

namespace {

EditorScreen g_screen;

[[nodiscard]] bool inside(ImVec2 point, ImVec2 min, ImVec2 max) noexcept
{
    return point.x >= min.x && point.y >= min.y && point.x < max.x && point.y < max.y;
}

[[nodiscard]] bool below(HostLayer layer, std::size_t order, HostLayer otherLayer, std::size_t otherOrder) noexcept
{
    return layer != otherLayer ? layer < otherLayer : order < otherOrder;
}

} // namespace

void setEditorScreen(const EditorScreen& screen) noexcept
{
    g_screen = screen;
}

const EditorScreen& editorScreen() noexcept
{
    return g_screen;
}

void EditorHosts::beginFrame(math::Vec2 pointer, bool pressed)
{
    m_pointer = ImVec2(pointer.x, pointer.y);
    m_last = std::move(m_hosts);
    m_hosts.clear();
    m_draws.clear();
    m_stack.clear();
    // The host on top under the pointer, as the last frame placed them.
    const Host* found = nullptr;
    for (const Host& host : m_last)
    {
        if (!host.options.takesPointer || !inside(m_pointer, host.min, host.max))
        {
            continue;
        }
        if (host.options.hole && inside(m_pointer, host.options.hole->first, host.options.hole->second))
        {
            continue;
        }
        if (found == nullptr || below(found->layer, found->order, host.layer, host.order))
        {
            found = &host;
        }
    }
    m_hovered = found != nullptr ? found->id : std::string{};
    m_hoveredRoot = found != nullptr ? found->root : std::string{};
    if (pressed)
    {
        if (found == nullptr)
        {
            m_focused.clear();
            m_focusedRoot.clear();
        }
        else if (found->options.focusable)
        {
            m_focused = found->id;
            m_focusedRoot = found->root;
        }
    }
}

void EditorHosts::begin(std::string_view id, ImVec2 min, ImVec2 max, HostLayer layer, const HostOptions& options)
{
    Host host{.id = std::string(id), .min = min, .max = ImVec2(std::max(max.x, min.x), std::max(max.y, min.y)), .layer = layer,
              .order = m_hosts.size(), .options = options};
    host.root = m_stack.empty() ? host.id : m_hosts[m_stack.back().host].root;
    if (options.background)
    {
        m_draws.push_back(Draw{.layer = layer, .order = host.order, .sequence = m_draws.size(), .min = host.min, .max = host.max,
                               .color = *options.background});
    }
    // A focus asked for by name before the host was ever placed finds its root now.
    if (m_focused == host.id)
    {
        m_focusedRoot = host.root;
    }
    const float padding = options.padding;
    m_stack.push_back(Open{.host = m_hosts.size(),
                           .cursor = ImVec2(host.min.x + padding, host.min.y + padding),
                           .contentMax = ImVec2(host.max.x - padding, host.max.y - padding)});
    m_hosts.push_back(std::move(host));
}

void EditorHosts::end()
{
    if (!m_stack.empty())
    {
        m_stack.pop_back();
    }
}

const EditorHosts::Host* EditorHosts::current() const noexcept
{
    return m_stack.empty() ? nullptr : &m_hosts[m_stack.back().host];
}

ImVec2 EditorHosts::cursor() const noexcept
{
    return m_stack.empty() ? ImVec2(0.0f, 0.0f) : m_stack.back().cursor;
}

void EditorHosts::setCursor(ImVec2 at) noexcept
{
    if (!m_stack.empty())
    {
        m_stack.back().cursor = at;
    }
}

ImVec2 EditorHosts::available() const noexcept
{
    if (m_stack.empty())
    {
        return ImVec2(0.0f, 0.0f);
    }
    const Open& open = m_stack.back();
    return ImVec2(std::max(open.contentMax.x - open.cursor.x, 0.0f), std::max(open.contentMax.y - open.cursor.y, 0.0f));
}

void EditorHosts::image(HostImage shown, ImVec2 size)
{
    const Host* const host = current();
    if (host == nullptr)
    {
        return;
    }
    Open& open = m_stack.back();
    open.itemMin = open.cursor;
    open.itemMax = ImVec2(open.cursor.x + size.x, open.cursor.y + size.y);
    m_draws.push_back(Draw{.layer = host->layer, .order = host->order, .sequence = m_draws.size(), .image = shown, .min = open.itemMin, .max = open.itemMax});
    open.cursor.y = open.itemMax.y;
}

bool EditorHosts::itemHovered() const noexcept
{
    const Host* const host = current();
    return host != nullptr && host->id == m_hovered && inside(m_pointer, m_stack.back().itemMin, m_stack.back().itemMax);
}

bool EditorHosts::hovered() const noexcept
{
    const Host* const host = current();
    return host != nullptr && host->id == m_hovered;
}

bool EditorHosts::focused() const noexcept
{
    const Host* const host = current();
    return host != nullptr && !m_focusedRoot.empty() && host->root == m_focusedRoot;
}

bool EditorHosts::isFocused(std::string_view id) const noexcept
{
    return !m_focused.empty() && (m_focused == id || m_focusedRoot == id);
}

const std::string& EditorHosts::focusedId() const noexcept
{
    return m_focused;
}

const std::string& EditorHosts::hoveredId() const noexcept
{
    return m_hovered;
}

void EditorHosts::focus(std::string_view id)
{
    m_focused = std::string(id);
    m_focusedRoot = m_focused;
    for (const std::vector<Host>* hosts : {&m_hosts, &m_last})
    {
        for (const Host& host : *hosts)
        {
            if (host.id == id)
            {
                m_focusedRoot = host.root;
                return;
            }
        }
    }
}

void EditorHosts::focusCurrent()
{
    if (const Host* const host = current())
    {
        m_focused = host->id;
        m_focusedRoot = host->root;
    }
}

bool EditorHosts::pointerTaken() const noexcept
{
    return !m_hovered.empty();
}

void EditorHosts::compose(render::RenderWorld& world) const
{
    std::vector<const Draw*> sorted;
    sorted.reserve(m_draws.size());
    for (const Draw& draw : m_draws)
    {
        sorted.push_back(&draw);
    }
    std::ranges::sort(sorted, [](const Draw* first, const Draw* second) {
        if (first->layer != second->layer)
        {
            return first->layer < second->layer;
        }
        return first->order != second->order ? first->order < second->order : first->sequence < second->sequence;
    });
    // A quad for each, in pixels of the window. The colours meet their neighbours edge to edge; an
    // image keeps its size in pixels, so that each of its pixels lands on one of the window.
    const float pixels = g_screen.pixelsPerPoint;
    for (const Draw* const draw : sorted)
    {
        const math::Vec2 min{std::round(draw->min.x * pixels), std::round(draw->min.y * pixels)};
        const math::Vec2 max = draw->image ? min + math::Vec2{std::round((draw->max.x - draw->min.x) * pixels),
                                                              std::round((draw->max.y - draw->min.y) * pixels)}
                                           : math::Vec2{std::round(draw->max.x * pixels), std::round(draw->max.y * pixels)};
        if (max.x <= min.x || max.y <= min.y)
        {
            continue;
        }
        const auto first = static_cast<std::uint32_t>(world.toolsVertices.size());
        const math::Vec4 color = draw->image ? math::Vec4{1.0f} : draw->color;
        world.toolsVertices.push_back({.position = min, .uv = {0.0f, 0.0f}, .color = color});
        world.toolsVertices.push_back({.position = {max.x, min.y}, .uv = {1.0f, 0.0f}, .color = color});
        world.toolsVertices.push_back({.position = max, .uv = {1.0f, 1.0f}, .color = color});
        world.toolsVertices.push_back({.position = {min.x, max.y}, .uv = {0.0f, 1.0f}, .color = color});
        const auto firstIndex = static_cast<std::uint32_t>(world.toolsIndices.size());
        for (const std::uint32_t corner : {0u, 1u, 2u, 0u, 2u, 3u})
        {
            world.toolsIndices.push_back(first + corner);
        }
        render::UiDraw uiDraw{.firstIndex = firstIndex, .indexCount = 6};
        if (draw->image)
        {
            uiDraw.source = draw->image->source;
            uiDraw.surface = draw->image->surface;
        }
        // Colours follow one another in one batch, as long as nothing comes between them.
        if (!draw->image && !world.toolsDraws.empty() && world.toolsDraws.back().source == render::UiSource::Texture &&
            world.toolsDraws.back().firstIndex + world.toolsDraws.back().indexCount == firstIndex)
        {
            world.toolsDraws.back().indexCount += 6;
            continue;
        }
        world.toolsDraws.push_back(uiDraw);
    }
}

} // namespace devex::tools::detail
