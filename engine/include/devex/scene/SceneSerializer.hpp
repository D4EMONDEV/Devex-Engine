#pragma once

#include <devex/core/Error.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/serialization/Text.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

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
// in hierarchy order. Components of types that are not registered, such as those of game code that
// is not loaded, are kept as they were read and written back. Prefab instances are entity sections
// with overrides, described in Prefab.hpp.
namespace devex::scene {

// The sections of an entity's components whose type is not registered, kept so that saving the
// scene loses nothing, until restorePreservedComponents recreates them.
struct PreservedComponents
{
    std::vector<serialization::TextSection> sections;
};

// Turns the components of the pool at the type index into preserved sections, when their type is
// registered, then destroys the pool. Used before unloading the module that defines the type or
// that created the pool. Returns the number of components preserved.
std::size_t preserveComponentPool(Scene& scene, std::size_t typeIndex);

// Recreates the preserved components whose type is registered now, as after loading a game module,
// optionally only those whose type name the filter accepts. Fields that no longer exist are skipped
// with a warning. Returns the number of components restored.
std::size_t restorePreservedComponents(Scene& scene, const std::function<bool(std::string_view typeName)>& filter = {});

inline constexpr std::int64_t sceneFormatVersion = 1;
inline constexpr std::string_view sceneExtension = ".dvxscene";

// Whether loading a scene loads the prefabs of its instances (see Prefab.hpp).
enum class PrefabLoading : std::uint8_t
{
    Resolve,
    // Leaves every instance unresolved, as when only the syntax of a scene is checked.
    KeepUnresolved,
};

[[nodiscard]] std::string saveScene(const Scene& scene);

// Unknown component types are preserved, and unknown fields skipped with a warning, so that a scene
// written by a newer version or with game code that is not loaded still opens. Malformed values are
// errors that name their line. A prefab that cannot be loaded leaves its instance unresolved, with
// a warning.
[[nodiscard]] core::Result<Scene> loadScene(std::string_view text, PrefabLoading prefabs = PrefabLoading::Resolve);

// Writes an entity and its descendants as entity and component sections, without the [scene]
// header and without the parent of the root. Used to copy, restore or duplicate a subtree. An
// entity inside a prefab instance is written with its descendants as ordinary entities.
[[nodiscard]] std::string saveEntityTree(const Scene& scene, Entity root);

// Recreates entities written by saveEntityTree with their original UUIDs, which must be unused in
// the scene, and places the root under parent before the sibling `before` (see
// Scene::setParent). Prefab instances are loaded. Nothing is created when an error is returned.
[[nodiscard]] core::Result<Entity> loadEntityTree(Scene& scene, std::string_view text,
                                                  Entity parent, Entity before = {});

[[nodiscard]] core::Result<void> saveSceneFile(const Scene& scene,
                                               const std::filesystem::path& path);
[[nodiscard]] core::Result<Scene> loadSceneFile(const std::filesystem::path& path);

} // namespace devex::scene
