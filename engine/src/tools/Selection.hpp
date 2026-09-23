#pragma once

#include <devex/core/Uuid.hpp>
#include <devex/scene/Scene.hpp>

#include <cstddef>
#include <span>
#include <vector>

namespace devex::tools::detail {

// The selected entities of a scene, in the order they were selected. The last one is active: the
// gizmo sits on it, the inspector shows its values where the selection differs, and new entities
// are created beside it.
class Selection
{
public:
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    // Nil when nothing is selected.
    [[nodiscard]] core::Uuid active() const noexcept;
    [[nodiscard]] bool contains(core::Uuid entity) const noexcept;
    [[nodiscard]] std::span<const core::Uuid> entities() const noexcept;

    // Selects only this entity, or nothing when it is nil.
    void set(core::Uuid entity);
    // Selects these entities, the last one active. Nil ones and repeats are skipped.
    void set(std::span<const core::Uuid> entities);
    // Adds the entity, or makes it active when it is already selected.
    void add(core::Uuid entity);
    // Removes the entity when it is selected, adds it otherwise.
    void toggle(core::Uuid entity);
    void remove(core::Uuid entity);
    void clear() noexcept;
    // Forgets the entities that are not in the scene, as after an undo removed them.
    void prune(const scene::Scene& scene);

    bool operator==(const Selection&) const = default;

private:
    std::vector<core::Uuid> m_entities;
};

// The selected entities that exist and are not under another selected entity, in the order of the
// hierarchy: those that moving, copying or deleting the selection acts on.
[[nodiscard]] std::vector<scene::Entity> selectedRoots(const scene::Scene& scene, const Selection& selection);

// Whether the entity or one of its ancestors is in the set.
[[nodiscard]] bool isUnderAny(const scene::Scene& scene, scene::Entity entity, const Selection& selection);

// What a click on an entity in the viewport selects. The entities of a prefab instance are one
// object: the first click selects the outermost instance, the next ones go down, instance by
// instance, to the entity under the mouse, which further clicks keep.
[[nodiscard]] scene::Entity clickTarget(const scene::Scene& scene, scene::Entity hit, core::Uuid active);

// What a rectangle drawn over the entity selects: the outermost instance it belongs to.
[[nodiscard]] scene::Entity outermostTarget(const scene::Scene& scene, scene::Entity hit);

} // namespace devex::tools::detail
