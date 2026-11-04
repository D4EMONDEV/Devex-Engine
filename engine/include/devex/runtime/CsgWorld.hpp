#pragma once

#include <devex/core/Export.hpp>
#include <devex/core/Uuid.hpp>
#include <devex/runtime/AssetManager.hpp>
#include <devex/scene/Scene.hpp>

#include <cstdint>
#include <unordered_map>

namespace devex::runtime {

// Keeps the solids of a scene computed: each root of CSG components gets a mesh of its own, made again when
// what it is made of changes, in the editor as in the game. Its CsgResult says which, for the renderer,
// the physics and the navigation, which see a mesh like any other.
class DEVEX_API CsgWorld
{
public:
    // Looks at every solid: cheap as long as they do not change.
    void update(scene::Scene& scene, AssetManager& assets);
    // Forgets the meshes it made.
    void clear(AssetManager& assets);

private:
    struct Built
    {
        std::uint64_t signature = 0;
        asset::AssetId mesh;
        bool seen = false;
    };
    std::unordered_map<core::Uuid, Built> m_built;
};

} // namespace devex::runtime
