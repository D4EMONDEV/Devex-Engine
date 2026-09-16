#pragma once

#include <devex/asset/ModelData.hpp>
#include <devex/scene/Entity.hpp>
#include <devex/scene/Scene.hpp>

#include <string>

namespace devex::scene {

// Creates the entities of a model under a new root entity placed under parent (a root when
// invalid). Every node becomes an entity with its Transform, and a MeshRenderer when it has a
// mesh. The entities are copies: they keep referring to the model's meshes and materials, but
// later changes to the model hierarchy do not reach them.
Entity instantiateModel(Scene& scene, const asset::ModelData& model, const std::string& rootName,
                        Entity parent = {});

} // namespace devex::scene
