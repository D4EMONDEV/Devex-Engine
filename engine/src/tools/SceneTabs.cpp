#include "SceneTabs.hpp"

#include <devex/core/Assert.hpp>

#include <utility>

namespace devex::tools::detail {

std::size_t SceneTabs::size() const noexcept
{
    return m_tabs.size();
}

bool SceneTabs::empty() const noexcept
{
    return m_tabs.empty();
}

std::optional<std::size_t> SceneTabs::active() const noexcept
{
    return m_active;
}

std::uint64_t SceneTabs::id(std::size_t index) const noexcept
{
    DEVEX_ASSERT(index < m_tabs.size());
    return m_tabs[index].id;
}

std::optional<std::size_t> SceneTabs::findById(std::uint64_t id) const noexcept
{
    for (std::size_t index = 0; index < m_tabs.size(); ++index)
    {
        if (m_tabs[index].id == id)
        {
            return index;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> SceneTabs::findByPath(const std::filesystem::path& path, const ActiveDocument& live) const
{
    if (path.empty())
    {
        return std::nullopt;
    }
    std::error_code error;
    for (std::size_t index = 0; index < m_tabs.size(); ++index)
    {
        const std::filesystem::path& candidate = this->path(index, live);
        if (!candidate.empty() && (candidate == path || std::filesystem::equivalent(candidate, path, error)))
        {
            return index;
        }
    }
    return std::nullopt;
}

const std::filesystem::path& SceneTabs::path(std::size_t index, const ActiveDocument& live) const noexcept
{
    DEVEX_ASSERT(index < m_tabs.size());
    return index == m_active ? live.path : m_tabs[index].document.path;
}

bool SceneTabs::isModified(std::size_t index, const ActiveDocument& live) const noexcept
{
    DEVEX_ASSERT(index < m_tabs.size());
    if (index == m_active)
    {
        return live.history.stateId() != live.savedState;
    }
    const SceneDocument& document = m_tabs[index].document;
    return document.history.stateId() != document.savedState;
}

SceneDocument& SceneTabs::background(std::size_t index) noexcept
{
    DEVEX_ASSERT(index < m_tabs.size() && index != m_active);
    return m_tabs[index].document;
}

std::size_t SceneTabs::add(SceneDocument document)
{
    m_tabs.push_back(Tab{.id = m_nextId++, .document = std::move(document)});
    return m_tabs.size() - 1;
}

void SceneTabs::activate(std::size_t index, const ActiveDocument& live)
{
    DEVEX_ASSERT(index < m_tabs.size());
    if (index == m_active)
    {
        return;
    }
    if (m_active)
    {
        swapDocument(m_tabs[*m_active].document, live);
    }
    swapDocument(m_tabs[index].document, live);
    m_active = index;
}

void SceneTabs::close(std::size_t index, const ActiveDocument& live)
{
    DEVEX_ASSERT(index < m_tabs.size());
    if (index != m_active)
    {
        m_tabs.erase(m_tabs.begin() + static_cast<std::ptrdiff_t>(index));
        if (m_active && *m_active > index)
        {
            --*m_active;
        }
        return;
    }

    if (m_tabs.size() == 1)
    {
        clear(live);
        return;
    }
    // The neighbor comes to the screen and receives the closed document, dropped with its tab.
    const std::size_t neighbor = index + 1 < m_tabs.size() ? index + 1 : index - 1;
    swapDocument(m_tabs[neighbor].document, live);
    m_tabs[neighbor].document = SceneDocument();
    m_tabs.erase(m_tabs.begin() + static_cast<std::ptrdiff_t>(index));
    m_active = neighbor > index ? neighbor - 1 : neighbor;
}

void SceneTabs::move(std::size_t from, std::size_t to)
{
    DEVEX_ASSERT(from < m_tabs.size() && to < m_tabs.size());
    if (from == to)
    {
        return;
    }
    const std::optional<std::uint64_t> activeId = m_active ? std::optional(m_tabs[*m_active].id) : std::nullopt;
    Tab tab = std::move(m_tabs[from]);
    m_tabs.erase(m_tabs.begin() + static_cast<std::ptrdiff_t>(from));
    m_tabs.insert(m_tabs.begin() + static_cast<std::ptrdiff_t>(to), std::move(tab));
    m_active = activeId ? findById(*activeId) : std::nullopt;
}

void SceneTabs::clear(const ActiveDocument& live)
{
    m_tabs.clear();
    m_active.reset();
    live.path.clear();
    live.scene = scene::Scene{};
    live.history.clear();
    live.savedState = live.history.stateId();
    live.selection.clear();
    live.hidden.clear();
}

void SceneTabs::forEachBackgroundScene(const std::function<void(scene::Scene&)>& function)
{
    for (std::size_t index = 0; index < m_tabs.size(); ++index)
    {
        if (index != m_active)
        {
            function(m_tabs[index].document.scene);
        }
    }
}

void SceneTabs::swapDocument(SceneDocument& stored, const ActiveDocument& live)
{
    std::swap(stored.path, live.path);
    std::swap(stored.scene, live.scene);
    std::swap(stored.history, live.history);
    std::swap(stored.savedState, live.savedState);
    std::swap(stored.selection, live.selection);
    std::swap(stored.camera, live.camera);
    std::swap(stored.hidden, live.hidden);
}

} // namespace devex::tools::detail
