#pragma once

#include <devex/core/Error.hpp>
#include <devex/core/Uuid.hpp>
#include <devex/scene/Scene.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

// Copies of entities, as the editor copies, pastes and duplicates them. The text of a copy is an
// [entities] header followed by each tree as saveEntityTree writes it:
//
//     [entities format=1]
//
//     [entity uuid="..." name="Crate"]
//     prefab = asset("...")
//
//     [entity uuid="..." name="Lamp"]
//     [component type="Transform"]
//     ...
//
// Pasting gives every entity a new UUID, so that the same text can be pasted again.
namespace devex::scene {

inline constexpr std::int64_t entityCopyFormatVersion = 1;

// Writes the trees at the roots, in the order given. A root under another root is written with it
// only.
[[nodiscard]] std::string saveEntityTrees(const Scene& scene, std::span<const Entity> roots);

// One tree of a copy, ready for loadEntityTree.
struct EntityTreeCopy
{
    std::string text;
    core::Uuid root;
    std::string name;
};

// Reads a text written by saveEntityTrees and gives every entity a new UUID. References between the
// copied entities follow them, those of prefab instances included; references to other entities
// stay as they are. Fails when the text is not a copy of entities.
[[nodiscard]] core::Result<std::vector<EntityTreeCopy>> copyEntityTrees(std::string_view text);

// Whether the text starts as saveEntityTrees starts it, without reading it all.
[[nodiscard]] bool isEntityCopy(std::string_view text) noexcept;

} // namespace devex::scene
