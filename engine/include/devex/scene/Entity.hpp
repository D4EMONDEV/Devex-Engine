#pragma once

#include <devex/core/SlotMap.hpp>

#include <cstdint>

namespace devex::scene {

struct EntityTag;

// Compact runtime reference to an entity of a Scene. It becomes stale when the entity is
// destroyed. Files refer to entities by their UUID instead.
using Entity = core::Handle<EntityTag>;

// Entities keep their handle in a scene, and a key tells a reused index from the entity that had it.
[[nodiscard]] constexpr std::uint64_t entityKey(Entity entity) noexcept
{
    return static_cast<std::uint64_t>(entity.generation) << 32 | entity.index;
}

} // namespace devex::scene
