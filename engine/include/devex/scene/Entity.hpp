#pragma once

#include <devex/core/SlotMap.hpp>

namespace devex::scene {

struct EntityTag;

// Compact runtime reference to an entity of a Scene. It becomes stale when the entity is
// destroyed. Files refer to entities by their UUID instead.
using Entity = core::Handle<EntityTag>;

} // namespace devex::scene
