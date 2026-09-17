#pragma once

#include <devex/render/RenderWorld.hpp>
#include <devex/runtime/AssetManager.hpp>
#include <devex/scene/Scene.hpp>

namespace devex::runtime {

// Copies what the renderer needs from the scene into the frame snapshot: the first primary
// camera, the first directional light, every point and spot light, the first environment, and one
// instance per submesh of every mesh renderer whose mesh is available, loading assets on first
// use. Light units become candelas and colored illuminance. World transforms must be up to date.
void extractScene(scene::Scene& scene, AssetManager& assets, render::RenderWorld& world);

} // namespace devex::runtime
