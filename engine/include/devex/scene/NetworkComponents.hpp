#pragma once

#include <devex/core/Export.hpp>
#include <devex/reflection/Reflection.hpp>

#include <string>
#include <vector>

// What the replication of the runtime looks at in a scene: which objects appear on every player's
// game, and which of their fields follow.
namespace devex::scene {

// Every prefab instance that the authority of this entity, the host by default, places under it appears
// on all the players' games, and goes from them when it is destroyed. Those who join later get what is
// there. Nothing changes for the code that instantiates it.
struct DEVEX_API NetworkSpawner
{
};

// Fields of the components of this entity that the authority of the entity keeps the same on the other
// players' games, those who join later included.
struct DEVEX_API NetworkSync
{
    // As "Transform.position" or "Health.value", checked in the inspector.
    std::vector<std::string> fields{"Transform.position", "Transform.rotation"};
    // How many times per second they go at most, when they change.
    float rate = 20.0f;
    // How far behind the others show the Transform of this entity, in seconds, moving smoothly between
    // what they received. 0 shows it as it comes, by jumps. Other fields apply as they arrive.
    float smoothing = 0.1f;
};

DEVEX_API void devexReflect(reflection::TypeBuilder<NetworkSpawner>& type);
DEVEX_API void devexReflect(reflection::TypeBuilder<NetworkSync>& type);

} // namespace devex::scene
