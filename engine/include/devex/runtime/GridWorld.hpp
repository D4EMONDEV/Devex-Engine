#pragma once

#include <devex/core/Export.hpp>
#include <devex/core/Uuid.hpp>
#include <devex/render/RenderWorld.hpp>
#include <devex/runtime/AssetManager.hpp>
#include <devex/scene/GridMapComponents.hpp>
#include <devex/scene/Scene.hpp>

#include <cstdint>
#include <memory>
#include <unordered_map>

namespace devex::runtime {

// What the GridMap components of a scene are, in the game as in the editor: what collides with them, as one mesh
// made again when their cells change, and what draws them, each cell its own instance, which the renderer culls.
class DEVEX_API GridWorld
{
public:
    // Looks at every grid: cheap as long as they do not change.
    void update(scene::Scene& scene, AssetManager& assets);
    void extract(const scene::Scene& scene, AssetManager& assets, render::RenderWorld& world) const;
    void clear(AssetManager& assets);

private:
    struct Built
    {
        std::uint64_t signature = 0;
        asset::AssetId collisionMesh;
        std::shared_ptr<const scene::GridPalette> palette;
        bool seen = false;
    };
    std::unordered_map<core::Uuid, Built> m_built;
};

} // namespace devex::runtime
