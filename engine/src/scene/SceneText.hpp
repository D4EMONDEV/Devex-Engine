#pragma once

#include <devex/asset/AssetId.hpp>
#include <devex/core/Error.hpp>
#include <devex/core/Uuid.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/scene/SceneSerializer.hpp>
#include <devex/serialization/Text.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// What scene files and prefabs share: sections read into entities, and entities written back.
namespace devex::scene::detail {

// Checks the [scene format=1] header that starts a scene file.
[[nodiscard]] core::Result<void> checkSceneHeader(const serialization::TextDocument& document);

[[nodiscard]] std::unexpected<core::Error> errorAt(std::uint32_t line, const core::Error& error);
[[nodiscard]] core::Result<core::Uuid> readUuid(const serialization::TextValue& value);

[[nodiscard]] serialization::TextSection writeComponent(const ComponentType& componentType, const void* component);
// The type attribute of a component or override section.
[[nodiscard]] const std::string* componentTypeName(const serialization::TextSection& section);
// Reads the fields of a component section into a component. Unknown fields are skipped with a warning.
[[nodiscard]] core::Result<void> readComponent(const ComponentType& componentType,
                                               const serialization::TextSection& section, void* component);

// An entity read from sections, with its prefab instances expanded, before it is created in a scene.
struct FlatEntity
{
    core::Uuid uuid;
    std::string name;
    // Nil for a root.
    core::Uuid parent;
    // The component sections, whether their type is registered or not.
    std::vector<serialization::TextSection> components;
    // Set on the roots of instances.
    asset::AssetId prefab;
    bool unresolved = false;
    std::vector<serialization::TextSection> unresolvedOverrides;
    // Entities of an instance: the root of the instance and the UUID of the entity in its prefab.
    bool owned = false;
    core::Uuid instance;
    core::Uuid source;
    // The instance that a missing parent most likely belonged to, as when the prefab no longer has
    // the entity that an added entity was under.
    core::Uuid fallbackParent;
    std::uint32_t line = 0;
};

struct FlatScene
{
    // Parents before their children, in hierarchy order.
    std::vector<FlatEntity> entities;
    // Every prefab loaded to make the entities, directly or not.
    std::vector<asset::AssetId> prefabs;
};

// The prefabs being loaded, outermost first, to detect prefabs that contain themselves.
using PrefabStack = std::vector<asset::AssetId>;

// Reads the entity sections starting at `first`.
[[nodiscard]] core::Result<FlatScene> flattenSections(const serialization::TextDocument& document, std::size_t first,
                                                      PrefabLoading prefabs, PrefabStack& stack);

// The entities of an instance of a prefab without overrides, as flattenSections makes them.
[[nodiscard]] core::Result<FlatScene> expandPrefab(asset::AssetId prefab, core::Uuid instance, PrefabStack& stack);

// Creates the entities in the scene and links their parents, which may be entities of the scene.
// The created entities are returned in order; nothing is created when an error is returned.
[[nodiscard]] core::Result<std::vector<Entity>> createEntities(Scene& scene, const FlatScene& flat);

// Writes the entity section, its components and its descendants. The entities of the instance
// `unpacked`, when valid, are written as ordinary entities.
void writeEntity(const Scene& scene, Entity entity, bool writeParent, serialization::TextDocument& document,
                 Entity unpacked);

// Writes the instance at root with only the overrides of its root's Transform and name.
void writeRevertedInstance(const Scene& scene, Entity root, serialization::TextDocument& document);

} // namespace devex::scene::detail
