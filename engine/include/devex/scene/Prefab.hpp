#pragma once

#include <devex/asset/AssetId.hpp>
#include <devex/core/Error.hpp>
#include <devex/core/Uuid.hpp>
#include <devex/scene/Entity.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/serialization/Text.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Prefabs are scenes placed in other scenes, which stay linked to their file. In a .dvxscene, an
// instance is an entity section that names its prefab, followed by its overrides:
//
//     [entity uuid="2c1f5e0a-7d4b-4c9e-8f3a-6b5d2e1c0f47" name="Crate"]
//     prefab = asset("9a3e4f21-5c6d-4b7a-8e9f-0a1b2c3d4e5f")
//
//     [override type="Transform"]
//     position = vec3(2, 0, -3)
//
//     [override target="7e2d9c4b-1a3f-4e5d-9b8c-2f6a0d1e3c57" type="PointLight"]
//     intensity = 2000
//
//     [override target="7e2d9c4b-1a3f-4e5d-9b8c-2f6a0d1e3c57" name="Bulb"]
//
// Loading an instance loads the entities of the prefab, its own instances included, and gives them
// UUIDs derived from the instance's and their own. With one root, the root of the prefab becomes the
// instance entity; with several, the instance entity groups them. Overrides then apply: a target is
// the UUID of an entity in the prefab, the root when absent; a type changes fields of a component,
// or adds the component with those fields when the prefab's entity has none; a name renames.
// Entities added under the entities of an instance are ordinary sections whose parent is a derived
// UUID. Saving writes only what differs from the prefab, so that later changes to the prefab reach
// every instance.
namespace devex::scene {

// The root of a prefab instance, and of the instances inside it.
struct PrefabInstance
{
    asset::AssetId prefab;
    // False when the prefab could not be loaded: the entity is empty and keeps the overrides as they
    // were read, to write them back.
    bool resolved = true;
    std::vector<serialization::TextSection> unresolvedOverrides;
};

// An entity loaded from a prefab.
struct PrefabEntity
{
    // The root of the instance whose overrides save the entity: the outermost one in the scene.
    Entity instance;
    // The UUID of the entity in the prefab, which overrides target; nil for the entity that groups
    // the roots of a prefab that has several.
    core::Uuid source;
};

// Gives the text of a prefab scene. The runtime reads the scene assets of the project.
using PrefabSourceLoader = std::function<core::Result<std::string>(asset::AssetId prefab)>;

// Sets how prefabs are read, for the whole process, and forgets the prefabs read so far. The loader
// is called on the thread that loads a scene. Without a loader, instances stay unresolved.
void setPrefabSourceLoader(PrefabSourceLoader loader);

// Forgets the prefabs read so far. They are also read again when the text of the prefab, or of a
// prefab it contains, changes.
void clearPrefabCache();

// The UUID in a scene of the entity of an instance whose UUID in the prefab is `source`.
[[nodiscard]] core::Uuid derivePrefabUuid(core::Uuid instance, core::Uuid source) noexcept;

// Whether the entity is the root of an instance saved by the scene itself, rather than part of
// another instance.
[[nodiscard]] bool isPrefabInstanceRoot(const Scene& scene, Entity entity) noexcept;

// The root of the instance whose overrides save the entity; invalid when it is not from a prefab.
[[nodiscard]] Entity owningPrefabInstance(const Scene& scene, Entity entity) noexcept;

// Whether the entity comes from the prefab of an instance without being its root. Such an entity
// changes with its prefab: it cannot be destroyed or moved on its own.
[[nodiscard]] bool isInsidePrefabInstance(const Scene& scene, Entity entity) noexcept;

// Creates an instance of the prefab with a new UUID, last under parent (a root when invalid).
[[nodiscard]] core::Result<Entity> instantiatePrefab(Scene& scene, asset::AssetId prefab, Entity parent = {});

// The prefabs that a scene text instantiates directly.
[[nodiscard]] std::vector<asset::AssetId> prefabReferences(std::string_view sceneText);

// Whether loading the prefab loads `used`, directly or through the prefabs it contains; true when
// both are the same. Used to refuse instances that would contain themselves.
[[nodiscard]] bool prefabUses(asset::AssetId prefab, asset::AssetId used);

// The entities of an instance as its prefab makes them, without overrides, with the UUIDs they have
// in the scene of the instance: the values that overrides differ from. Null when the prefab cannot
// be loaded. Recent results are kept.
[[nodiscard]] std::shared_ptr<const Scene> prefabBase(asset::AssetId prefab, core::Uuid instance);

// Writes an entity and its descendants as saveEntityTree does, but with the entities of prefab
// instances written as ordinary ones, no longer linked to their prefab.
[[nodiscard]] std::string saveUnpackedEntityTree(const Scene& scene, Entity root);

// Writes the instance at root as saveEntityTree does, without its overrides except the Transform and
// name of its root, keeping the entities added to it.
[[nodiscard]] std::string saveRevertedPrefabInstance(const Scene& scene, Entity root);

// An instance saved before its prefab changes, to be loaded again with the new prefab.
struct PrefabInstanceSnapshot
{
    core::Uuid root;
    core::Uuid parent;
    core::Uuid nextSibling;
    std::string text;
};

// Saves the instances that use one of the prefabs, while the loader still gives their previous
// texts. Instances inside other saved instances are saved with them.
[[nodiscard]] std::vector<PrefabInstanceSnapshot> snapshotPrefabInstances(const Scene& scene,
                                                                          std::span<const asset::AssetId> prefabs);

// Replaces the saved instances with their snapshots loaded again, at the same place. An instance
// that fails to load is left as it was, with an error. Returns the number of instances replaced.
std::size_t rebuildPrefabInstances(Scene& scene, std::span<const PrefabInstanceSnapshot> snapshots);

} // namespace devex::scene
