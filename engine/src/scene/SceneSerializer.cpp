#include "SceneText.hpp"

#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/FieldValue.hpp>
#include <devex/scene/Prefab.hpp>
#include <devex/scene/SceneSerializer.hpp>
#include <devex/serialization/Text.hpp>

#include <algorithm>
#include <optional>
#include <vector>

namespace devex::scene {
namespace detail {

using serialization::TextDocument;
using serialization::TextProperty;
using serialization::TextSection;
using serialization::TextValue;

std::unexpected<core::Error> errorAt(std::uint32_t line, const core::Error& error)
{
    return core::makeError(error.code, "line {}: {}", line, error.message);
}

core::Result<core::Uuid> readUuid(const TextValue& value)
{
    const std::string* const text = serialization::asString(value);
    const std::optional<core::Uuid> uuid = text != nullptr ? core::Uuid::parse(*text) : std::nullopt;
    if (!uuid)
    {
        return core::makeError(core::ErrorCode::Parse, "expected a UUID string");
    }
    return *uuid;
}

TextSection writeComponent(const ComponentType& componentType, const void* component)
{
    TextSection componentSection{.type = "component"};
    componentSection.attributes.push_back({"type", TextValue(std::string(componentType.name()))});
    for (const reflection::FieldInfo& field : componentType.type->fields)
    {
        componentSection.properties.push_back({field.name, writeFieldValue(field, field.address(component))});
    }
    return componentSection;
}

const std::string* componentTypeName(const TextSection& section)
{
    const TextValue* const typeValue = section.findAttribute("type");
    return typeValue != nullptr ? serialization::asString(*typeValue) : nullptr;
}

core::Result<void> readComponent(const ComponentType& componentType, const TextSection& section, void* component)
{
    for (const TextProperty& property : section.properties)
    {
        const reflection::FieldInfo* const field = componentType.type->findField(property.key);
        if (field == nullptr)
        {
            DEVEX_LOG_WARNING("Scene line {}: skipping unknown field '{}' of {}", property.line, property.key,
                              componentType.name());
            continue;
        }
        if (core::Result<void> read = readFieldValue(*field, property.value, field->address(component)); !read)
        {
            return core::makeError(core::ErrorCode::Parse, "line {}: {}.{}: {}", property.line, componentType.name(),
                                   property.key, read.error().message);
        }
    }
    return {};
}

core::Result<std::vector<Entity>> createEntities(Scene& scene, const FlatScene& flat)
{
    struct PendingParent
    {
        Entity child;
        core::Uuid parent;
        core::Uuid fallback;
        std::uint32_t line = 0;
    };

    std::vector<Entity> created;
    created.reserve(flat.entities.size());
    std::vector<PendingParent> pendingParents;

    // Leaves the scene unchanged when loading fails half way.
    const auto fail = [&](std::unexpected<core::Error> error) {
        for (auto entity = created.rbegin(); entity != created.rend(); ++entity)
        {
            if (scene.isAlive(*entity))
            {
                scene.destroyEntity(*entity);
            }
        }
        return error;
    };

    for (const FlatEntity& flatEntity : flat.entities)
    {
        core::Result<Entity> entity = scene.createEntity(flatEntity.uuid, flatEntity.name);
        if (!entity)
        {
            return fail(errorAt(flatEntity.line, entity.error()));
        }
        created.push_back(*entity);

        for (const TextSection& section : flatEntity.components)
        {
            const std::string* const typeName = componentTypeName(section);
            if (typeName == nullptr)
            {
                return fail(core::makeError(core::ErrorCode::Parse, "line {}: a component needs a type", section.line));
            }
            const ComponentType* const componentType = componentRegistry().find(*typeName);
            if (componentType == nullptr)
            {
                DEVEX_LOG_DEBUG("Scene line {}: keeping component of unknown type '{}'", section.line, *typeName);
                if (!scene.has<PreservedComponents>(*entity))
                {
                    scene.add<PreservedComponents>(*entity);
                }
                scene.get<PreservedComponents>(*entity).sections.push_back(section);
                continue;
            }
            void* const component = componentType->emplace(scene, *entity);
            if (core::Result<void> read = readComponent(*componentType, section, component); !read)
            {
                return fail(std::unexpected(read.error()));
            }
        }

        if (flatEntity.prefab.isValid())
        {
            scene.add<PrefabInstance>(*entity, PrefabInstance{.prefab = flatEntity.prefab,
                                                              .resolved = !flatEntity.unresolved,
                                                              .unresolvedOverrides = flatEntity.unresolvedOverrides});
        }
        if (flatEntity.owned)
        {
            // The root of the instance comes first, so it already exists.
            scene.add<PrefabEntity>(*entity, PrefabEntity{.instance = scene.findEntity(flatEntity.instance),
                                                          .source = flatEntity.source});
        }
        if (!flatEntity.parent.isNil())
        {
            pendingParents.push_back({*entity, flatEntity.parent, flatEntity.fallbackParent, flatEntity.line});
        }
    }

    for (const PendingParent& pending : pendingParents)
    {
        Entity parent = scene.findEntity(pending.parent);
        if (!parent.isValid() && !pending.fallback.isNil())
        {
            parent = scene.findEntity(pending.fallback);
            if (parent.isValid())
            {
                DEVEX_LOG_WARNING("Scene line {}: parent {} of '{}' no longer exists in its prefab; it moves under '{}'",
                                  pending.line, pending.parent, scene.name(pending.child), scene.name(parent));
            }
        }
        if (!parent.isValid())
        {
            return fail(core::makeError(core::ErrorCode::NotFound, "line {}: parent {} does not exist", pending.line,
                                        pending.parent));
        }
        if (core::Result<void> parented = scene.setParent(pending.child, parent); !parented)
        {
            return fail(errorAt(pending.line, parented.error()));
        }
    }
    return created;
}

namespace {

[[nodiscard]] TextSection entityHeader(const Scene& scene, Entity entity, bool writeParent)
{
    TextSection section{.type = "entity"};
    section.attributes.push_back({"uuid", TextValue(scene.uuid(entity).toString())});
    section.attributes.push_back({"name", TextValue(scene.name(entity))});
    if (const Entity parent = scene.parent(entity); writeParent && parent.isValid())
    {
        section.properties.push_back({"parent", TextValue(scene.uuid(parent).toString())});
    }
    return section;
}

// A copy of a section read from a file, written without its line numbers.
[[nodiscard]] TextSection withoutLines(TextSection section)
{
    section.line = 0;
    for (TextProperty& property : section.attributes)
    {
        property.line = 0;
    }
    for (TextProperty& property : section.properties)
    {
        property.line = 0;
    }
    return section;
}

// The component sections of an entity, in the order they are saved.
[[nodiscard]] std::vector<TextSection> componentSections(const Scene& scene, Entity entity)
{
    std::vector<TextSection> sections;
    for (const ComponentType& componentType : componentRegistry().types())
    {
        if (const void* const component = componentType.find(scene, entity))
        {
            sections.push_back(writeComponent(componentType, component));
        }
    }
    if (const PreservedComponents* const preserved = scene.tryGet<PreservedComponents>(entity))
    {
        for (const TextSection& section : preserved->sections)
        {
            // A type registered again is written from its component, when the entity has one.
            const std::string* const typeName = componentTypeName(section);
            const ComponentType* const registered = typeName != nullptr ? componentRegistry().find(*typeName) : nullptr;
            if (registered == nullptr || registered->find(scene, entity) == nullptr)
            {
                sections.push_back(withoutLines(section));
            }
        }
    }
    return sections;
}

[[nodiscard]] bool ownedBy(const Scene& scene, Entity entity, Entity instance) noexcept
{
    const PrefabEntity* const fromPrefab = scene.tryGet<PrefabEntity>(entity);
    return fromPrefab != nullptr && fromPrefab->instance == instance;
}

// The entities of the instance at root, in hierarchy order, without the entities added to them.
void collectInstanceEntities(const Scene& scene, Entity entity, Entity root, std::vector<Entity>& entities)
{
    entities.push_back(entity);
    for (Entity child = scene.firstChild(entity); child.isValid(); child = scene.nextSibling(child))
    {
        if (ownedBy(scene, child, root))
        {
            collectInstanceEntities(scene, child, root, entities);
        }
    }
}

// Writes an instance: its header, the overrides of its entities and the entities added to them.
// Without allOverrides, only the Transform of the root differs from the prefab.
void writeInstance(const Scene& scene, Entity root, bool writeParent, TextDocument& document, bool allOverrides)
{
    const PrefabInstance& instance = scene.get<PrefabInstance>(root);
    TextSection header = entityHeader(scene, root, writeParent);
    header.properties.push_back(
        {"prefab", serialization::makeCall("asset", {TextValue(instance.prefab.uuid.toString())})});
    document.sections.push_back(std::move(header));

    if (!instance.resolved)
    {
        // Written back as read, with the entities that were placed under the instance.
        for (const TextSection& section : instance.unresolvedOverrides)
        {
            document.sections.push_back(withoutLines(section));
        }
        for (Entity child = scene.firstChild(root); child.isValid(); child = scene.nextSibling(child))
        {
            writeEntity(scene, child, true, document, Entity{});
        }
        return;
    }

    // Without its prefab, every value of the instance is written, so that nothing is lost.
    const std::shared_ptr<const Scene> base = prefabBase(instance.prefab, scene.uuid(root));
    std::vector<Entity> entities;
    collectInstanceEntities(scene, root, root, entities);
    for (const Entity entity : entities)
    {
        const bool isRoot = entity == root;
        if (!isRoot && !allOverrides)
        {
            continue;
        }
        const Entity baseEntity = base != nullptr ? base->findEntity(scene.uuid(entity)) : Entity{};
        const auto overrideSection = [&] {
            TextSection section{.type = "override"};
            if (!isRoot)
            {
                section.attributes.push_back(
                    {"target", TextValue(scene.get<PrefabEntity>(entity).source.toString())});
            }
            return section;
        };

        if (!isRoot && (!baseEntity.isValid() || base->name(baseEntity) != scene.name(entity)))
        {
            TextSection renamed = overrideSection();
            renamed.attributes.push_back({"name", TextValue(scene.name(entity))});
            document.sections.push_back(std::move(renamed));
        }

        const std::vector<TextSection> baseSections =
            baseEntity.isValid() ? componentSections(*base, baseEntity) : std::vector<TextSection>{};
        for (TextSection& section : componentSections(scene, entity))
        {
            const std::string* const typeName = componentTypeName(section);
            if (typeName == nullptr || (!allOverrides && *typeName != "Transform"))
            {
                continue;
            }
            const auto baseSection = std::ranges::find_if(baseSections, [&](const TextSection& candidate) {
                const std::string* const candidateType = componentTypeName(candidate);
                return candidateType != nullptr && *candidateType == *typeName;
            });
            if (baseSection != baseSections.end())
            {
                std::erase_if(section.properties, [&](const TextProperty& property) {
                    const TextValue* const baseValue = baseSection->findProperty(property.key);
                    return baseValue != nullptr && *baseValue == property.value;
                });
                if (section.properties.empty())
                {
                    continue;
                }
            }
            // A component the prefab's entity does not have is added with all its fields.
            TextSection changed = overrideSection();
            changed.attributes.push_back({"type", TextValue(*typeName)});
            changed.properties = std::move(section.properties);
            document.sections.push_back(std::move(changed));
        }
    }

    for (const Entity entity : entities)
    {
        for (Entity child = scene.firstChild(entity); child.isValid(); child = scene.nextSibling(child))
        {
            if (!ownedBy(scene, child, root))
            {
                writeEntity(scene, child, true, document, Entity{});
            }
        }
    }
}

} // namespace

void writeEntity(const Scene& scene, Entity entity, bool writeParent, TextDocument& document, Entity unpacked)
{
    const PrefabEntity* const fromPrefab = scene.tryGet<PrefabEntity>(entity);
    const bool unpack = unpacked.isValid() && fromPrefab != nullptr && fromPrefab->instance == unpacked;
    if (!unpack && isPrefabInstanceRoot(scene, entity))
    {
        writeInstance(scene, entity, writeParent, document, true);
        return;
    }

    document.sections.push_back(entityHeader(scene, entity, writeParent));
    for (TextSection& section : componentSections(scene, entity))
    {
        document.sections.push_back(std::move(section));
    }
    for (Entity child = scene.firstChild(entity); child.isValid(); child = scene.nextSibling(child))
    {
        writeEntity(scene, child, true, document, unpacked);
    }
}

void writeRevertedInstance(const Scene& scene, Entity root, TextDocument& document)
{
    writeInstance(scene, root, false, document, false);
}

core::Result<void> checkSceneHeader(const TextDocument& document)
{
    if (document.sections.empty() || document.sections.front().type != "scene")
    {
        return core::makeError(core::ErrorCode::Parse, "a scene file starts with [scene]");
    }
    const TextSection& header = document.sections.front();
    const TextValue* const format = header.findAttribute("format");
    if (format == nullptr || serialization::asInteger(*format) != sceneFormatVersion)
    {
        return core::makeError(core::ErrorCode::Unsupported, "line {}: unsupported scene format, expected format={}",
                               header.line, sceneFormatVersion);
    }
    return {};
}

} // namespace detail

namespace {

using serialization::TextDocument;
using serialization::TextSection;
using serialization::TextValue;

[[nodiscard]] core::Result<Scene> createScene(const TextDocument& document, std::size_t first, PrefabLoading prefabs)
{
    detail::PrefabStack stack;
    core::Result<detail::FlatScene> flat = detail::flattenSections(document, first, prefabs, stack);
    if (!flat)
    {
        return std::unexpected(flat.error());
    }
    Scene scene;
    if (core::Result<std::vector<Entity>> created = detail::createEntities(scene, *flat); !created)
    {
        return std::unexpected(created.error());
    }
    return scene;
}

} // namespace

std::string saveScene(const Scene& scene)
{
    TextDocument document;
    TextSection header{.type = "scene"};
    header.attributes.push_back({"format", TextValue(sceneFormatVersion)});
    document.sections.push_back(std::move(header));

    for (Entity root = scene.firstRoot(); root.isValid(); root = scene.nextSibling(root))
    {
        detail::writeEntity(scene, root, true, document, Entity{});
    }
    return serialization::writeText(document);
}

core::Result<Scene> loadScene(std::string_view text, PrefabLoading prefabs)
{
    core::Result<TextDocument> document = serialization::parseText(text);
    if (!document)
    {
        return std::unexpected(document.error());
    }
    if (core::Result<void> header = detail::checkSceneHeader(*document); !header)
    {
        return std::unexpected(header.error());
    }
    return createScene(*document, 1, prefabs);
}

std::string saveEntityTree(const Scene& scene, Entity root)
{
    TextDocument document;
    // An entity inside a prefab instance is written as ordinary entities, since it cannot exist
    // without the rest of the instance.
    const PrefabEntity* const fromPrefab = scene.tryGet<PrefabEntity>(root);
    const Entity unpacked = fromPrefab != nullptr && fromPrefab->instance != root ? fromPrefab->instance : Entity{};
    detail::writeEntity(scene, root, false, document, unpacked);
    return serialization::writeText(document);
}

core::Result<Entity> loadEntityTree(Scene& scene, std::string_view text, Entity parent, Entity before)
{
    core::Result<TextDocument> document = serialization::parseText(text);
    if (!document)
    {
        return std::unexpected(document.error());
    }
    detail::PrefabStack stack;
    core::Result<detail::FlatScene> flat = detail::flattenSections(*document, 0, PrefabLoading::Resolve, stack);
    if (!flat)
    {
        return std::unexpected(flat.error());
    }
    if (flat->entities.empty())
    {
        return core::makeError(core::ErrorCode::Parse, "the text contains no entity");
    }
    core::Result<std::vector<Entity>> loaded = detail::createEntities(scene, *flat);
    if (!loaded)
    {
        return std::unexpected(loaded.error());
    }

    const Entity root = loaded->front();
    if (core::Result<void> placed = scene.setParent(root, parent, before); !placed)
    {
        scene.destroyEntity(root);
        return std::unexpected(placed.error());
    }
    return root;
}

std::size_t preserveComponentPool(Scene& scene, std::size_t typeIndex)
{
    const ComponentPoolBase* const pool = scene.componentPool(typeIndex);
    if (pool == nullptr)
    {
        return 0;
    }
    std::size_t preserved = 0;
    if (const ComponentType* const componentType = componentRegistry().findByIndex(typeIndex))
    {
        const std::vector<Entity> entities(pool->entities().begin(), pool->entities().end());
        for (const Entity entity : entities)
        {
            TextSection section = detail::writeComponent(*componentType, componentType->find(scene, entity));
            if (!scene.has<PreservedComponents>(entity))
            {
                scene.add<PreservedComponents>(entity);
            }
            scene.get<PreservedComponents>(entity).sections.push_back(std::move(section));
            ++preserved;
        }
    }
    else if (pool->size() > 0)
    {
        DEVEX_LOG_WARNING("Dropping {} components of a type that is not registered", pool->size());
    }
    scene.destroyComponentPool(typeIndex);
    return preserved;
}

std::size_t restorePreservedComponents(Scene& scene, const std::function<bool(std::string_view typeName)>& filter)
{
    const ComponentPoolBase* const pool = scene.componentPool(componentTypeIndex<PreservedComponents>());
    if (pool == nullptr)
    {
        return 0;
    }
    std::size_t restored = 0;
    const std::vector<Entity> entities(pool->entities().begin(), pool->entities().end());
    for (const Entity entity : entities)
    {
        std::vector<TextSection>& sections = scene.get<PreservedComponents>(entity).sections;
        std::erase_if(sections, [&](const TextSection& section) {
            const std::string* const typeName = detail::componentTypeName(section);
            const ComponentType* const componentType =
                typeName != nullptr ? componentRegistry().find(*typeName) : nullptr;
            if (componentType == nullptr || (filter && !filter(*typeName)))
            {
                return false;
            }
            void* const component = componentType->emplace(scene, entity);
            if (core::Result<void> read = detail::readComponent(*componentType, section, component); !read)
            {
                DEVEX_LOG_WARNING("Cannot restore a {} of {}: {}", *typeName, scene.name(entity), read.error());
            }
            ++restored;
            return true;
        });
        if (sections.empty())
        {
            scene.remove<PreservedComponents>(entity);
        }
    }
    return restored;
}

core::Result<void> saveSceneFile(const Scene& scene, const std::filesystem::path& path)
{
    return core::writeTextFile(path, saveScene(scene));
}

core::Result<Scene> loadSceneFile(const std::filesystem::path& path)
{
    core::Result<std::string> text = core::readTextFile(path);
    if (!text)
    {
        return std::unexpected(text.error());
    }
    return loadScene(*text);
}

} // namespace devex::scene
