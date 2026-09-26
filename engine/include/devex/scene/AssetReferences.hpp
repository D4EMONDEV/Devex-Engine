#pragma once

#include <devex/asset/AssetId.hpp>
#include <devex/scene/Scene.hpp>

#include <vector>

namespace devex::scene {

// The assets the components of the scene name in their fields, lists included: meshes, materials,
// textures, fonts, sounds, prefabs... Once each, in the order they are met. A scene loaded in the
// background preloads them before it replaces the current one.
[[nodiscard]] std::vector<asset::AssetId> referencedAssets(const Scene& scene);

} // namespace devex::scene
