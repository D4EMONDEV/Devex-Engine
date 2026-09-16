#pragma once

#include <devex/render/RenderWorld.hpp>
#include <devex/runtime/AssetRegistry.hpp>
#include <devex/scene/Scene.hpp>

namespace devex::runtime {

// Copies what the renderer needs from the scene into the frame snapshot: the first primary
// camera, the first directional light, and every mesh renderer whose mesh is loaded. World
// transforms must be up to date.
void extractScene(scene::Scene& scene, const AssetRegistry& assets, render::RenderWorld& world);

} // namespace devex::runtime
