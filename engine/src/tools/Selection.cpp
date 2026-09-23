#include "Selection.hpp"

#include <devex/scene/Prefab.hpp>

#include <algorithm>

namespace devex::tools::detail {
namespace {

void collectRoots(const scene::Scene& scene, scene::Entity entity, const Selection& selection,
                  std::vector<scene::Entity>& roots)
{
    for (; entity.isValid(); entity = scene.nextSibling(entity))
    {
        if (selection.contains(scene.uuid(entity)))
        {
            // Its descendants move, copy and go with it.
            roots.push_back(entity);
            continue;
        }
        collectRoots(scene, scene.firstChild(entity), selection, roots);
    }
}

} // namespace

bool Selection::empty() const noexcept
{
    return m_entities.empty();
}

std::size_t Selection::size() const noexcept
{
    return m_entities.size();
}

core::Uuid Selection::active() const noexcept
{
    return m_entities.empty() ? core::Uuid{} : m_entities.back();
}

bool Selection::contains(core::Uuid entity) const noexcept
{
    return !entity.isNil() && std::ranges::find(m_entities, entity) != m_entities.end();
}

std::span<const core::Uuid> Selection::entities() const noexcept
{
    return m_entities;
}

void Selection::set(core::Uuid entity)
{
    m_entities.clear();
    if (!entity.isNil())
    {
        m_entities.push_back(entity);
    }
}

void Selection::set(std::span<const core::Uuid> entities)
{
    m_entities.clear();
    for (const core::Uuid entity : entities)
    {
        add(entity);
    }
}

void Selection::add(core::Uuid entity)
{
    if (entity.isNil())
    {
        return;
    }
    std::erase(m_entities, entity);
    m_entities.push_back(entity);
}

void Selection::toggle(core::Uuid entity)
{
    if (contains(entity))
    {
        remove(entity);
    }
    else
    {
        add(entity);
    }
}

void Selection::remove(core::Uuid entity)
{
    std::erase(m_entities, entity);
}

void Selection::clear() noexcept
{
    m_entities.clear();
}

void Selection::prune(const scene::Scene& scene)
{
    std::erase_if(m_entities, [&scene](core::Uuid entity) { return !scene.findEntity(entity).isValid(); });
}

std::vector<scene::Entity> selectedRoots(const scene::Scene& scene, const Selection& selection)
{
    std::vector<scene::Entity> roots;
    if (!selection.empty())
    {
        collectRoots(scene, scene.firstRoot(), selection, roots);
    }
    return roots;
}

bool isUnderAny(const scene::Scene& scene, scene::Entity entity, const Selection& selection)
{
    for (; entity.isValid(); entity = scene.parent(entity))
    {
        if (selection.contains(scene.uuid(entity)))
        {
            return true;
        }
    }
    return false;
}

scene::Entity clickTarget(const scene::Scene& scene, scene::Entity hit, core::Uuid active)
{
    const scene::Entity outermost = scene::owningPrefabInstance(scene, hit);
    if (!outermost.isValid() || outermost == hit)
    {
        return hit;
    }
    // The instances between the outermost one and the entity, outermost first, then the entity.
    std::vector<scene::Entity> steps{hit};
    for (scene::Entity ancestor = scene.parent(hit); ancestor.isValid(); ancestor = scene.parent(ancestor))
    {
        if (scene.has<scene::PrefabInstance>(ancestor))
        {
            steps.push_back(ancestor);
        }
        if (ancestor == outermost)
        {
            break;
        }
    }
    std::ranges::reverse(steps);
    const auto current = std::ranges::find(steps, scene.findEntity(active));
    if (current == steps.end() || active.isNil())
    {
        return steps.front();
    }
    return current + 1 != steps.end() ? *(current + 1) : *current;
}

scene::Entity outermostTarget(const scene::Scene& scene, scene::Entity hit)
{
    const scene::Entity outermost = scene::owningPrefabInstance(scene, hit);
    return outermost.isValid() ? outermost : hit;
}

} // namespace devex::tools::detail
