#pragma once

#include <devex/core/Error.hpp>
#include <devex/scene/Scene.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

// The .dvxscene format:
//
//     [scene format=1]
//
//     [entity uuid="6f1c2a9e-3b7d-4e21-9a55-0c8d7e4f1b23" name="Player"]
//     parent = "b41e7c02-9d3a-4f6e-8c11-5a2e9b7d0f44"
//
//     [component type="Transform"]
//     position = vec3(0, 1, 0)
//     rotation = quat(0, 0, 0, 1)
//     scale = vec3(1, 1, 1)
//
// Component sections belong to the entity above them. Parents are written before their children,
// in hierarchy order, and only registered components are saved.
namespace devex::scene {

inline constexpr std::int64_t sceneFormatVersion = 1;

[[nodiscard]] std::string saveScene(const Scene& scene);

// Unknown component types and fields are skipped with a warning, so that a scene written by a
// newer version still opens. Malformed values are errors that name their line.
[[nodiscard]] core::Result<Scene> loadScene(std::string_view text);

// Writes an entity and its descendants as entity and component sections, without the [scene]
// header and without the parent of the root. Used to copy, restore or duplicate a subtree.
[[nodiscard]] std::string saveEntityTree(const Scene& scene, Entity root);

// Recreates entities written by saveEntityTree with their original UUIDs, which must be unused in
// the scene, and places the root under parent before the sibling `before` (see
// Scene::setParent). Nothing is created when an error is returned.
[[nodiscard]] core::Result<Entity> loadEntityTree(Scene& scene, std::string_view text,
                                                  Entity parent, Entity before = {});

[[nodiscard]] core::Result<void> saveSceneFile(const Scene& scene,
                                               const std::filesystem::path& path);
[[nodiscard]] core::Result<Scene> loadSceneFile(const std::filesystem::path& path);

} // namespace devex::scene
