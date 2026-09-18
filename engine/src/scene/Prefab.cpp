#include "SceneText.hpp"

#include <devex/core/Log.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/Prefab.hpp>
#include <devex/scene/SceneSerializer.hpp>

#include <algorithm>
#include <deque>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace devex::scene {
namespace {

using serialization::TextDocument;
using serialization::TextProperty;
using serialization::TextSection;
using serialization::TextValue;

struct CachedPrefab
{
    std::string text;
    std::shared_ptr<const detail::FlatScene> flat;
};

struct CachedBase
{
    asset::AssetId prefab;
    core::Uuid instance;
    // The prefab the base was made from, to tell when the prefab changed.
    std::shared_ptr<const detail::FlatScene> flat;
    std::shared_ptr<const Scene> scene;
};

struct PrefabCache
{
    std::mutex mutex;
    PrefabSourceLoader loader;
    std::unordered_map<asset::AssetId, CachedPrefab> prefabs;
    // Most recently used first.
    std::deque<CachedBase> bases;
};

constexpr std::size_t maxCachedBases = 64;

[[nodiscard]] PrefabCache& prefabCache()
{
    static PrefabCache cache;
    return cache;
}

[[nodiscard]] core::Result<std::string> readPrefabText(asset::AssetId prefab)
{
    PrefabSourceLoader loader;
    {
        PrefabCache& cache = prefabCache();
        const std::scoped_lock lock(cache.mutex);
        loader = cache.loader;
    }
    if (!loader)
    {
        return core::makeError(core::ErrorCode::InvalidState, "prefabs cannot be read here");
    }
    return loader(prefab);
}

// The prefab read before, when neither its text nor the texts of the prefabs it contains changed.
[[nodiscard]] std::shared_ptr<const detail::FlatScene> findFreshPrefab(asset::AssetId prefab, const std::string& text)
{
    std::shared_ptr<const detail::FlatScene> flat;
    std::vector<std::pair<asset::AssetId, std::string>> dependencies;
    {
        PrefabCache& cache = prefabCache();
        const std::scoped_lock lock(cache.mutex);
        const auto found = cache.prefabs.find(prefab);
        if (found == cache.prefabs.end() || found->second.text != text)
        {
            return nullptr;
        }
        flat = found->second.flat;
        for (const asset::AssetId dependency : flat->prefabs)
        {
            const auto cached = cache.prefabs.find(dependency);
            if (cached == cache.prefabs.end())
            {
                // It could not be loaded before, and may be now.
                return nullptr;
            }
            dependencies.emplace_back(dependency, cached->second.text);
        }
    }
    // The loader runs without the lock.
    for (const auto& [dependency, cachedText] : dependencies)
    {
        const core::Result<std::string> current = readPrefabText(dependency);
        if (!current || *current != cachedText)
        {
            return nullptr;
        }
    }
    return flat;
}

[[nodiscard]] core::Result<std::shared_ptr<const detail::FlatScene>> loadPrefab(asset::AssetId prefab,
                                                                                 detail::PrefabStack& stack)
{
    if (std::ranges::find(stack, prefab) != stack.end())
    {
        return core::makeError(core::ErrorCode::InvalidState, "the prefab contains itself");
    }
    core::Result<std::string> text = readPrefabText(prefab);
    if (!text)
    {
        return std::unexpected(text.error());
    }
    if (std::shared_ptr<const detail::FlatScene> fresh = findFreshPrefab(prefab, *text))
    {
        return fresh;
    }

    const core::Result<TextDocument> document = serialization::parseText(*text);
    if (!document)
    {
        return std::unexpected(document.error());
    }
    if (core::Result<void> header = detail::checkSceneHeader(*document); !header)
    {
        return std::unexpected(header.error());
    }
    stack.push_back(prefab);
    core::Result<detail::FlatScene> flat = detail::flattenSections(*document, 1, PrefabLoading::Resolve, stack);
    stack.pop_back();
    if (!flat)
    {
        return std::unexpected(flat.error());
    }
    if (flat->entities.empty())
    {
        return core::makeError(core::ErrorCode::InvalidState, "the prefab has no entity");
    }

    // Parents are checked now, since instances refer to the entities of the prefab by UUID.
    for (detail::FlatEntity& entity : flat->entities)
    {
        const auto exists = [&](core::Uuid uuid) {
            return std::ranges::any_of(flat->entities, [&](const detail::FlatEntity& other) { return other.uuid == uuid; });
        };
        if (entity.parent.isNil() || exists(entity.parent))
        {
            continue;
        }
        if (entity.fallbackParent.isNil() || !exists(entity.fallbackParent))
        {
            return core::makeError(core::ErrorCode::NotFound, "line {}: parent {} does not exist", entity.line,
                                   entity.parent);
        }
        DEVEX_LOG_WARNING("Prefab line {}: parent {} of '{}' no longer exists in its prefab", entity.line,
                          entity.parent, entity.name);
        entity.parent = entity.fallbackParent;
    }

    auto shared = std::make_shared<const detail::FlatScene>(std::move(*flat));
    PrefabCache& cache = prefabCache();
    const std::scoped_lock lock(cache.mutex);
    cache.prefabs.insert_or_assign(prefab, CachedPrefab{.text = std::move(*text), .flat = shared});
    return shared;
}

void addPrefab(std::vector<asset::AssetId>& prefabs, asset::AssetId prefab)
{
    if (std::ranges::find(prefabs, prefab) == prefabs.end())
    {
        prefabs.push_back(prefab);
    }
}

// Makes the entity("<uuid>") values that name an entity of the prefab name it in the instance.
// References to other entities stay as they are: they resolve to nothing.
template <typename Map>
void remapEntityReferences(TextValue& value, const std::unordered_set<core::Uuid>& prefabEntities, const Map& map)
{
    auto* const call = std::get_if<serialization::TextCall>(&value);
    if (call == nullptr)
    {
        return;
    }
    if (call->name == "entity")
    {
        if (call->arguments.size() == 1)
        {
            const core::Result<core::Uuid> uuid = detail::readUuid(call->arguments.front());
            if (uuid && prefabEntities.contains(*uuid))
            {
                call->arguments.front() = TextValue(map(*uuid).toString());
            }
        }
        return;
    }
    for (TextValue& argument : call->arguments)
    {
        remapEntityReferences(argument, prefabEntities, map);
    }
}

// The entities of a loaded prefab for an instance, without overrides.
[[nodiscard]] detail::FlatScene expandLoadedPrefab(const detail::FlatScene& source, asset::AssetId prefab,
                                                   core::Uuid instance)
{
    const auto rootCount = std::ranges::count_if(
        source.entities, [](const detail::FlatEntity& entity) { return entity.parent.isNil(); });
    const bool singleRoot = rootCount == 1;
    const core::Uuid sourceRoot = source.entities.front().uuid;
    const auto map = [&](core::Uuid uuid) {
        return singleRoot && uuid == sourceRoot ? instance : derivePrefabUuid(instance, uuid);
    };

    detail::FlatScene result;
    result.prefabs = source.prefabs;
    addPrefab(result.prefabs, prefab);
    result.entities.reserve(source.entities.size() + (singleRoot ? 0 : 1));
    if (!singleRoot)
    {
        detail::FlatEntity group{.prefab = prefab, .owned = true, .instance = instance};
        group.uuid = instance;
        if (const ComponentType* const transform = componentRegistry().find("Transform"))
        {
            const Transform identity;
            group.components.push_back(detail::writeComponent(*transform, &identity));
        }
        result.entities.push_back(std::move(group));
    }
    std::unordered_set<core::Uuid> prefabEntities;
    for (const detail::FlatEntity& entity : source.entities)
    {
        prefabEntities.insert(entity.uuid);
    }
    for (const detail::FlatEntity& entity : source.entities)
    {
        detail::FlatEntity copy{
            .uuid = map(entity.uuid),
            .name = entity.name,
            .parent = entity.parent.isNil() ? (singleRoot ? core::Uuid{} : instance) : map(entity.parent),
            .components = entity.components,
            .prefab = entity.prefab,
            .unresolved = entity.unresolved,
            .owned = true,
            .instance = instance,
            .source = entity.uuid,
            .line = entity.line,
        };
        if (singleRoot && entity.parent.isNil())
        {
            copy.prefab = prefab;
            copy.unresolved = false;
        }
        for (TextSection& component : copy.components)
        {
            for (TextProperty& property : component.properties)
            {
                remapEntityReferences(property.value, prefabEntities, map);
            }
        }
        result.entities.push_back(std::move(copy));
    }
    return result;
}

[[nodiscard]] core::Result<asset::AssetId> readAssetId(const TextValue& value)
{
    const serialization::TextCall* const call = serialization::asCall(value, "asset");
    if (call != nullptr && call->arguments.size() == 1)
    {
        if (core::Result<core::Uuid> uuid = detail::readUuid(call->arguments.front()); uuid && !uuid->isNil())
        {
            return asset::AssetId{*uuid};
        }
    }
    return core::makeError(core::ErrorCode::Parse, "expected asset(\"<uuid>\")");
}

// Changes the entities of an instance with its override sections.
[[nodiscard]] core::Result<void> applyOverrides(detail::FlatScene& instance, std::span<const TextSection> overrides,
                                                asset::AssetId prefab)
{
    for (const TextSection& section : overrides)
    {
        if (section.type != "override" && section.type != "component")
        {
            DEVEX_LOG_WARNING("Scene line {}: skipping unknown section [{}]", section.line, section.type);
            continue;
        }
        detail::FlatEntity* target = &instance.entities.front();
        if (const TextValue* const targetValue = section.findAttribute("target"))
        {
            const core::Result<core::Uuid> source = detail::readUuid(*targetValue);
            if (!source)
            {
                return detail::errorAt(section.line, source.error());
            }
            const auto found = std::ranges::find_if(instance.entities, [&](const detail::FlatEntity& entity) {
                return !entity.source.isNil() && entity.source == *source;
            });
            if (found == instance.entities.end())
            {
                DEVEX_LOG_INFO("Scene line {}: prefab {} no longer has entity {}; its override is dropped",
                               section.line, prefab.uuid, *source);
                continue;
            }
            target = &*found;
        }

        const TextValue* const nameValue = section.findAttribute("name");
        if (nameValue != nullptr)
        {
            const std::string* const name = serialization::asString(*nameValue);
            if (name == nullptr)
            {
                return core::makeError(core::ErrorCode::Parse, "line {}: expected a name string", section.line);
            }
            target->name = *name;
        }

        const std::string* const typeName = detail::componentTypeName(section);
        if (typeName == nullptr)
        {
            if (nameValue == nullptr)
            {
                DEVEX_LOG_WARNING("Scene line {}: an override needs a type or a name", section.line);
            }
            continue;
        }
        const auto component = std::ranges::find_if(target->components, [&](const TextSection& candidate) {
            const std::string* const candidateType = detail::componentTypeName(candidate);
            return candidateType != nullptr && *candidateType == *typeName;
        });
        if (component == target->components.end())
        {
            TextSection added{.type = "component", .line = section.line};
            added.attributes.push_back({"type", TextValue(*typeName)});
            added.properties = section.properties;
            target->components.push_back(std::move(added));
            continue;
        }
        for (const TextProperty& property : section.properties)
        {
            const auto existing = std::ranges::find_if(component->properties, [&](const TextProperty& candidate) {
                return candidate.key == property.key;
            });
            if (existing != component->properties.end())
            {
                *existing = property;
            }
            else
            {
                component->properties.push_back(property);
            }
        }
    }
    return {};
}

struct InstanceHeader
{
    core::Uuid uuid;
    std::optional<std::string> name;
    core::Uuid parent;
    asset::AssetId prefab;
    core::Uuid fallbackParent;
    std::uint32_t line = 0;
};

[[nodiscard]] core::Result<void> addInstance(detail::FlatScene& flat, const InstanceHeader& header,
                                             std::span<const TextSection> body, PrefabLoading prefabs,
                                             detail::PrefabStack& stack)
{
    addPrefab(flat.prefabs, header.prefab);
    core::Result<detail::FlatScene> instance =
        prefabs == PrefabLoading::Resolve
            ? detail::expandPrefab(header.prefab, header.uuid, stack)
            : core::Result<detail::FlatScene>(core::makeError(core::ErrorCode::InvalidState, "not loaded"));
    if (!instance)
    {
        if (prefabs == PrefabLoading::Resolve)
        {
            DEVEX_LOG_WARNING("Scene line {}: cannot load prefab {} of '{}': {}", header.line, header.prefab.uuid,
                              header.name.value_or(""), instance.error());
        }
        detail::FlatEntity unresolved{
            .uuid = header.uuid,
            .name = header.name.value_or(""),
            .parent = header.parent,
            .prefab = header.prefab,
            .unresolved = true,
            .unresolvedOverrides = std::vector<TextSection>(body.begin(), body.end()),
            .fallbackParent = header.fallbackParent,
            .line = header.line,
        };
        flat.entities.push_back(std::move(unresolved));
        return {};
    }

    detail::FlatEntity& root = instance->entities.front();
    root.parent = header.parent;
    root.fallbackParent = header.fallbackParent;
    root.line = header.line;
    if (header.name)
    {
        root.name = *header.name;
    }
    if (core::Result<void> applied = applyOverrides(*instance, body, header.prefab); !applied)
    {
        return applied;
    }
    for (const asset::AssetId prefab : instance->prefabs)
    {
        addPrefab(flat.prefabs, prefab);
    }
    flat.entities.insert(flat.entities.end(), std::make_move_iterator(instance->entities.begin()),
                         std::make_move_iterator(instance->entities.end()));
    return {};
}

[[nodiscard]] std::uint64_t mix(std::uint64_t value) noexcept
{
    // The finalizer of SplitMix64.
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebULL;
    value ^= value >> 31;
    return value;
}

[[nodiscard]] std::pair<std::uint64_t, std::uint64_t> halves(core::Uuid uuid) noexcept
{
    std::uint64_t high = 0;
    std::uint64_t low = 0;
    for (std::size_t index = 0; index < 8; ++index)
    {
        high = high << 8 | uuid.bytes()[index];
        low = low << 8 | uuid.bytes()[8 + index];
    }
    return {high, low};
}

} // namespace

namespace detail {

core::Result<FlatScene> flattenSections(const TextDocument& document, std::size_t first, PrefabLoading prefabs,
                                        PrefabStack& stack)
{
    FlatScene flat;
    // The last instance read, which entities whose parent is missing probably belonged to.
    core::Uuid lastInstance;
    std::size_t index = first;
    while (index < document.sections.size())
    {
        const TextSection& section = document.sections[index];
        if (section.type != "entity")
        {
            if (section.type == "component" || section.type == "override")
            {
                return core::makeError(core::ErrorCode::Parse, "line {}: a {} must follow an entity", section.line,
                                       section.type);
            }
            DEVEX_LOG_WARNING("Scene line {}: skipping unknown section [{}]", section.line, section.type);
            ++index;
            continue;
        }

        // The sections that belong to the entity.
        std::size_t end = index + 1;
        while (end < document.sections.size() && document.sections[end].type != "entity")
        {
            ++end;
        }
        const std::span<const TextSection> body(document.sections.data() + index + 1, end - index - 1);

        const TextValue* const uuidValue = section.findAttribute("uuid");
        core::Result<core::Uuid> uuid = uuidValue != nullptr
                                            ? readUuid(*uuidValue)
                                            : core::makeError(core::ErrorCode::Parse, "an entity needs a uuid");
        if (!uuid)
        {
            return errorAt(section.line, uuid.error());
        }
        const TextValue* const nameValue = section.findAttribute("name");
        const std::string* const name = nameValue != nullptr ? serialization::asString(*nameValue) : nullptr;
        core::Uuid parent;
        if (const TextValue* const parentValue = section.findProperty("parent"))
        {
            core::Result<core::Uuid> parentUuid = readUuid(*parentValue);
            if (!parentUuid)
            {
                return errorAt(section.line, parentUuid.error());
            }
            parent = *parentUuid;
        }

        if (const TextValue* const prefabValue = section.findProperty("prefab"))
        {
            const core::Result<asset::AssetId> prefab = readAssetId(*prefabValue);
            if (!prefab)
            {
                return errorAt(section.line, prefab.error());
            }
            const InstanceHeader header{
                .uuid = *uuid,
                .name = name != nullptr ? std::optional<std::string>(*name) : std::nullopt,
                .parent = parent,
                .prefab = *prefab,
                .fallbackParent = lastInstance,
                .line = section.line,
            };
            if (core::Result<void> added = addInstance(flat, header, body, prefabs, stack); !added)
            {
                return std::unexpected(added.error());
            }
            lastInstance = *uuid;
        }
        else
        {
            FlatEntity entity{
                .uuid = *uuid,
                .name = name != nullptr ? *name : std::string(),
                .parent = parent,
                .fallbackParent = lastInstance,
                .line = section.line,
            };
            for (const TextSection& part : body)
            {
                if (part.type == "component")
                {
                    entity.components.push_back(part);
                }
                else if (part.type == "override")
                {
                    DEVEX_LOG_WARNING("Scene line {}: skipping an override outside a prefab instance", part.line);
                }
                else
                {
                    DEVEX_LOG_WARNING("Scene line {}: skipping unknown section [{}]", part.line, part.type);
                }
            }
            flat.entities.push_back(std::move(entity));
        }
        index = end;
    }
    return flat;
}

core::Result<FlatScene> expandPrefab(asset::AssetId prefab, core::Uuid instance, PrefabStack& stack)
{
    const core::Result<std::shared_ptr<const FlatScene>> loaded = loadPrefab(prefab, stack);
    if (!loaded)
    {
        return std::unexpected(loaded.error());
    }
    return expandLoadedPrefab(**loaded, prefab, instance);
}

} // namespace detail

void setPrefabSourceLoader(PrefabSourceLoader loader)
{
    PrefabCache& cache = prefabCache();
    const std::scoped_lock lock(cache.mutex);
    cache.loader = std::move(loader);
    cache.prefabs.clear();
    cache.bases.clear();
}

void clearPrefabCache()
{
    PrefabCache& cache = prefabCache();
    const std::scoped_lock lock(cache.mutex);
    cache.prefabs.clear();
    cache.bases.clear();
}

core::Uuid derivePrefabUuid(core::Uuid instance, core::Uuid source) noexcept
{
    const auto [instanceHigh, instanceLow] = halves(instance);
    const auto [sourceHigh, sourceLow] = halves(source);
    std::uint64_t high = mix(instanceHigh ^ mix(instanceLow ^ mix(sourceHigh ^ mix(sourceLow ^ 0x5d1f0c3a2b4e6978ULL))));
    std::uint64_t low = mix(sourceHigh ^ mix(sourceLow + 0x9e3779b97f4a7c15ULL) ^ mix(instanceHigh ^ mix(instanceLow)));
    // A version 8 (custom) identifier, which generated ones never are.
    high = (high & ~0xf000ULL) | 0x8000ULL;
    low = (low & ~(0xc0ULL << 56)) | (0x80ULL << 56);
    return core::Uuid::fromParts(high, low);
}

bool isPrefabInstanceRoot(const Scene& scene, Entity entity) noexcept
{
    if (!scene.has<PrefabInstance>(entity))
    {
        return false;
    }
    const PrefabEntity* const fromPrefab = scene.tryGet<PrefabEntity>(entity);
    return fromPrefab == nullptr || fromPrefab->instance == entity;
}

Entity owningPrefabInstance(const Scene& scene, Entity entity) noexcept
{
    if (const PrefabEntity* const fromPrefab = scene.tryGet<PrefabEntity>(entity))
    {
        return scene.isAlive(fromPrefab->instance) ? fromPrefab->instance : Entity{};
    }
    return scene.has<PrefabInstance>(entity) ? entity : Entity{};
}

bool isInsidePrefabInstance(const Scene& scene, Entity entity) noexcept
{
    const PrefabEntity* const fromPrefab = scene.tryGet<PrefabEntity>(entity);
    return fromPrefab != nullptr && fromPrefab->instance != entity;
}

core::Result<Entity> instantiatePrefab(Scene& scene, asset::AssetId prefab, Entity parent)
{
    detail::PrefabStack stack;
    core::Result<detail::FlatScene> flat = detail::expandPrefab(prefab, core::Uuid::generate(), stack);
    if (!flat)
    {
        return std::unexpected(flat.error());
    }
    core::Result<std::vector<Entity>> created = detail::createEntities(scene, *flat);
    if (!created)
    {
        return std::unexpected(created.error());
    }
    const Entity root = created->front();
    if (core::Result<void> placed = scene.setParent(root, parent); !placed)
    {
        scene.destroyEntity(root);
        return std::unexpected(placed.error());
    }
    return root;
}

std::vector<asset::AssetId> prefabReferences(std::string_view sceneText)
{
    std::vector<asset::AssetId> references;
    const core::Result<TextDocument> document = serialization::parseText(sceneText);
    if (!document)
    {
        return references;
    }
    for (const TextSection& section : document->sections)
    {
        if (const TextValue* const value = section.type == "entity" ? section.findProperty("prefab") : nullptr)
        {
            if (const core::Result<asset::AssetId> prefab = readAssetId(*value))
            {
                addPrefab(references, *prefab);
            }
        }
    }
    return references;
}

bool prefabUses(asset::AssetId prefab, asset::AssetId used)
{
    if (prefab == used)
    {
        return true;
    }
    detail::PrefabStack stack;
    const core::Result<std::shared_ptr<const detail::FlatScene>> loaded = loadPrefab(prefab, stack);
    return loaded && std::ranges::find((*loaded)->prefabs, used) != (*loaded)->prefabs.end();
}

std::shared_ptr<const Scene> prefabBase(asset::AssetId prefab, core::Uuid instance)
{
    detail::PrefabStack stack;
    const core::Result<std::shared_ptr<const detail::FlatScene>> loaded = loadPrefab(prefab, stack);
    if (!loaded)
    {
        return nullptr;
    }

    PrefabCache& cache = prefabCache();
    {
        const std::scoped_lock lock(cache.mutex);
        const auto found = std::ranges::find_if(cache.bases, [&](const CachedBase& base) {
            return base.prefab == prefab && base.instance == instance && base.flat == *loaded;
        });
        if (found != cache.bases.end())
        {
            CachedBase base = std::move(*found);
            cache.bases.erase(found);
            cache.bases.push_front(std::move(base));
            return cache.bases.front().scene;
        }
    }

    auto base = std::make_shared<Scene>();
    detail::FlatScene flat = expandLoadedPrefab(**loaded, prefab, instance);
    if (core::Result<std::vector<Entity>> created = detail::createEntities(*base, flat); !created)
    {
        DEVEX_LOG_WARNING("Cannot load prefab {}: {}", prefab.uuid, created.error());
        return nullptr;
    }

    const std::scoped_lock lock(cache.mutex);
    cache.bases.push_front({.prefab = prefab, .instance = instance, .flat = *loaded, .scene = base});
    if (cache.bases.size() > maxCachedBases)
    {
        cache.bases.pop_back();
    }
    return base;
}

std::string saveUnpackedEntityTree(const Scene& scene, Entity root)
{
    TextDocument document;
    const Entity instance = owningPrefabInstance(scene, root);
    const PrefabInstance* const rootInstance = scene.tryGet<PrefabInstance>(instance);
    const bool unpack = rootInstance != nullptr && rootInstance->resolved;
    detail::writeEntity(scene, root, false, document, unpack ? instance : Entity{});
    return serialization::writeText(document);
}

std::string saveRevertedPrefabInstance(const Scene& scene, Entity root)
{
    TextDocument document;
    if (isPrefabInstanceRoot(scene, root))
    {
        detail::writeRevertedInstance(scene, root, document);
    }
    else
    {
        detail::writeEntity(scene, root, false, document, Entity{});
    }
    return serialization::writeText(document);
}

std::vector<PrefabInstanceSnapshot> snapshotPrefabInstances(const Scene& scene, std::span<const asset::AssetId> prefabs)
{
    std::vector<PrefabInstanceSnapshot> snapshots;
    const ComponentPoolBase* const pool = scene.componentPool(componentTypeIndex<PrefabInstance>());
    if (pool == nullptr || prefabs.empty())
    {
        return snapshots;
    }

    std::vector<Entity> roots;
    for (const Entity entity : pool->entities())
    {
        if (!isPrefabInstanceRoot(scene, entity))
        {
            continue;
        }
        const asset::AssetId prefab = scene.get<PrefabInstance>(entity).prefab;
        if (std::ranges::any_of(prefabs, [&](asset::AssetId changed) { return prefabUses(prefab, changed); }))
        {
            roots.push_back(entity);
        }
    }

    for (const Entity root : roots)
    {
        // An instance inside another one is saved with it.
        bool nested = false;
        for (Entity ancestor = scene.parent(root); ancestor.isValid() && !nested; ancestor = scene.parent(ancestor))
        {
            nested = std::ranges::find(roots, ancestor) != roots.end();
        }
        if (nested)
        {
            continue;
        }
        const Entity parent = scene.parent(root);
        const Entity next = scene.nextSibling(root);
        snapshots.push_back({
            .root = scene.uuid(root),
            .parent = parent.isValid() ? scene.uuid(parent) : core::Uuid{},
            .nextSibling = next.isValid() ? scene.uuid(next) : core::Uuid{},
            .text = saveEntityTree(scene, root),
        });
    }
    return snapshots;
}

std::size_t rebuildPrefabInstances(Scene& scene, std::span<const PrefabInstanceSnapshot> snapshots)
{
    std::size_t rebuilt = 0;
    for (const PrefabInstanceSnapshot& snapshot : snapshots)
    {
        const Entity old = scene.findEntity(snapshot.root);
        if (!old.isValid())
        {
            continue;
        }
        // Tried in a scratch scene first, so that a prefab that no longer loads leaves the instance as it was.
        {
            Scene scratch;
            if (const core::Result<Entity> trial = loadEntityTree(scratch, snapshot.text, Entity{}); !trial)
            {
                DEVEX_LOG_ERROR("Cannot update '{}' from its prefab: {}", scene.name(old), trial.error());
                continue;
            }
        }
        const std::string name = scene.name(old);
        scene.destroyEntity(old);
        const Entity parent = snapshot.parent.isNil() ? Entity{} : scene.findEntity(snapshot.parent);
        Entity before = snapshot.nextSibling.isNil() ? Entity{} : scene.findEntity(snapshot.nextSibling);
        if (before.isValid() && scene.parent(before) != parent)
        {
            before = Entity{};
        }
        if (const core::Result<Entity> loaded = loadEntityTree(scene, snapshot.text, parent, before); !loaded)
        {
            DEVEX_LOG_ERROR("Cannot update '{}' from its prefab: {}", name, loaded.error());
            continue;
        }
        ++rebuilt;
    }
    return rebuilt;
}

} // namespace devex::scene
