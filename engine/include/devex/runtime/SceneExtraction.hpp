#pragma once

#include <devex/core/Export.hpp>

#include <devex/asset/Project.hpp>
#include <devex/particles/ParticleWorld.hpp>
#include <devex/render/RenderWorld.hpp>
#include <devex/runtime/AssetManager.hpp>
#include <devex/scene/Scene.hpp>

namespace devex::runtime {

// Copies what the renderer needs from the scene into the frame snapshot: the first primary
// camera, the first directional light, every point and spot light, the first environment, and one
// instance per submesh of every mesh renderer whose mesh is available, loading assets on first
// use. Light units become candelas and colored illuminance. Mesh instances are identified by the
// index of their entity plus one, for picking. World transforms must be up to date.
DEVEX_API void extractScene(scene::Scene& scene, AssetManager& assets, render::RenderWorld& world);

// Adds the SpriteRenderer components whose sprite and texture are loaded, with the frame their
// SpriteAnimator shows, in the sorting layers of the project. Sprites are identified for picking as
// mesh instances are.
DEVEX_API void extractSprites(scene::Scene& scene, AssetManager& assets, const asset::SortingSettings& sorting,
                              render::RenderWorld& world);

// Adds the Tilemap components whose tileset is loaded, with the tiles whose sprite and texture
// are, animated tiles on their frame at `seconds`. Tilemaps are identified for picking as mesh
// instances are.
DEVEX_API void extractTilemaps(scene::Scene& scene, AssetManager& assets, const asset::SortingSettings& sorting, double seconds,
                               render::RenderWorld& world);

// Adds the particles of the emitters, in the world, one batch per emitter, and the ribbons of
// their trails and of the TrailRenderer components, one batch each.
DEVEX_API void extractParticles(const particles::ParticleWorld& particles, AssetManager& assets, render::RenderWorld& world);

} // namespace devex::runtime
