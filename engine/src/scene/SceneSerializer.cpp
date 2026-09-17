#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/FieldValue.hpp>
#include <devex/scene/SceneSerializer.hpp>
#include <devex/serialization/Text.hpp>

#include <optional>
#include <vector>

namespace devex::scene {
namespace {

using serialization::TextDocument;
using serialization::TextProperty;
using serialization::TextSection;
using serialization::TextValue;

[[nodiscard]] std::unexpected<core::Error> errorAt(std::uint32_t line, const core::Error& error)
{
    return core::makeError(error.code, "line {}: {}", line, error.message);
}

[[nodiscard]] core::Result<core::Uuid> readUuid(const TextValue& value)
{
    const std::string* const text = serialization::asString(value);
    const std::optional<core::Uuid> uuid =
        text != nullptr ? core::Uuid::parse(*text) : std::nullopt;
    if (!uuid)
    {
        return core::makeError(core::ErrorCode::Parse, "expected a UUID string");
    }
    return *uuid;
}

void writeEntity(const Scene& scene, Entity entity, bool writeParent, TextDocument& document)
{
    TextSection entitySection{.type = "entity"};
    entitySection.attributes.push_back({"uuid", TextValue(scene.uuid(entity).toString())});
    entitySection.attributes.push_back({"name", TextValue(scene.name(entity))});
    if (const Entity parent = scene.parent(entity); writeParent && parent.isValid())
    {
        entitySection.properties.push_back({"parent", TextValue(scene.uuid(parent).toString())});
    }
    document.sections.push_back(std::move(entitySection));

    for (const ComponentType& componentType : componentRegistry().types())
    {
        const void* const component = componentType.find(scene, entity);
        if (component == nullptr)
        {
            continue;
        }
        TextSection componentSection{.type = "component"};
        componentSection.attributes.push_back(
            {"type", TextValue(std::string(componentType.name()))});
        for (const reflection::FieldInfo& field : componentType.type->fields)
        {
            componentSection.properties.push_back(
                {field.name, writeFieldValue(field, field.address(component))});
        }
        document.sections.push_back(std::move(componentSection));
    }

    for (Entity child = scene.firstChild(entity); child.isValid(); child = scene.nextSibling(child))
    {
        writeEntity(scene, child, true, document);
    }
}

// Creates the entities and components of the sections starting at `first`, then links the
// parents they name. The created entities are returned in file order.
[[nodiscard]] core::Result<std::vector<Entity>> loadEntitySections(Scene& scene,
                                                                    const TextDocument& document,
                                                                    std::size_t first)
{
    struct PendingParent
    {
        Entity child;
        core::Uuid parent;
        std::uint32_t line = 0;
    };

    std::vector<Entity> created;
    std::vector<PendingParent> pendingParents;

    // Leaves the scene unchanged when loading fails half way.
    const auto fail = [&](std::unexpected<core::Error> error) {
        for (auto entity = created.rbegin(); entity != created.rend(); ++entity)
        {
            scene.destroyEntity(*entity);
        }
        return error;
    };

    for (std::size_t index = first; index < document.sections.size(); ++index)
    {
        const TextSection& section = document.sections[index];
        if (section.type == "entity")
        {
            const TextValue* const uuidValue = section.findAttribute("uuid");
            core::Result<core::Uuid> uuid =
                uuidValue != nullptr
                    ? readUuid(*uuidValue)
                    : core::makeError(core::ErrorCode::Parse, "an entity needs a uuid");
            if (!uuid)
            {
                return fail(errorAt(section.line, uuid.error()));
            }

            const TextValue* const nameValue = section.findAttribute("name");
            const std::string* const name =
                nameValue != nullptr ? serialization::asString(*nameValue) : nullptr;
            core::Result<Entity> entity = scene.createEntity(*uuid, name != nullptr ? *name : "");
            if (!entity)
            {
                return fail(errorAt(section.line, entity.error()));
            }
            created.push_back(*entity);

            if (const TextValue* const parentValue = section.findProperty("parent"))
            {
                core::Result<core::Uuid> parent = readUuid(*parentValue);
                if (!parent)
                {
                    return fail(errorAt(section.line, parent.error()));
                }
                pendingParents.push_back({*entity, *parent, section.line});
            }
        }
        else if (section.type == "component")
        {
            if (created.empty())
            {
                return fail(core::makeError(core::ErrorCode::Parse,
                                            "line {}: a component must follow an entity",
                                            section.line));
            }
            const TextValue* const typeValue = section.findAttribute("type");
            const std::string* const typeName =
                typeValue != nullptr ? serialization::asString(*typeValue) : nullptr;
            if (typeName == nullptr)
            {
                return fail(core::makeError(core::ErrorCode::Parse,
                                            "line {}: a component needs a type", section.line));
            }

            const ComponentType* const componentType = componentRegistry().find(*typeName);
            if (componentType == nullptr)
            {
                DEVEX_LOG_WARNING("Scene line {}: skipping unknown component type '{}'",
                                  section.line, *typeName);
                continue;
            }

            void* const component = componentType->emplace(scene, created.back());
            for (const TextProperty& property : section.properties)
            {
                const reflection::FieldInfo* const field =
                    componentType->type->findField(property.key);
                if (field == nullptr)
                {
                    DEVEX_LOG_WARNING("Scene line {}: skipping unknown field '{}' of {}",
                                      property.line, property.key, *typeName);
                    continue;
                }
                if (core::Result<void> read =
                        readFieldValue(*field, property.value, field->address(component));
                    !read)
                {
                    return fail(core::makeError(core::ErrorCode::Parse, "line {}: {}.{}: {}",
                                                property.line, *typeName, property.key,
                                                read.error().message));
                }
            }
        }
        else
        {
            DEVEX_LOG_WARNING("Scene line {}: skipping unknown section [{}]", section.line,
                              section.type);
        }
    }

    for (const PendingParent& pending : pendingParents)
    {
        const Entity parent = scene.findEntity(pending.parent);
        if (!parent.isValid())
        {
            return fail(core::makeError(core::ErrorCode::NotFound,
                                        "line {}: parent {} does not exist", pending.line,
                                        pending.parent));
        }
        if (core::Result<void> parented = scene.setParent(pending.child, parent); !parented)
        {
            return fail(errorAt(pending.line, parented.error()));
        }
    }
    return created;
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
        writeEntity(scene, root, true, document);
    }
    return serialization::writeText(document);
}

core::Result<Scene> loadScene(std::string_view text)
{
    core::Result<TextDocument> document = serialization::parseText(text);
    if (!document)
    {
        return std::unexpected(document.error());
    }
    if (document->sections.empty() || document->sections.front().type != "scene")
    {
        return core::makeError(core::ErrorCode::Parse, "a scene file starts with [scene]");
    }
    const TextSection& header = document->sections.front();
    const TextValue* const format = header.findAttribute("format");
    if (format == nullptr || serialization::asInteger(*format) != sceneFormatVersion)
    {
        return core::makeError(core::ErrorCode::Unsupported,
                               "line {}: unsupported scene format, expected format={}",
                               header.line, sceneFormatVersion);
    }

    Scene scene;
    if (core::Result<std::vector<Entity>> loaded = loadEntitySections(scene, *document, 1);
        !loaded)
    {
        return std::unexpected(loaded.error());
    }
    return scene;
}

std::string saveEntityTree(const Scene& scene, Entity root)
{
    TextDocument document;
    writeEntity(scene, root, false, document);
    return serialization::writeText(document);
}

core::Result<Entity> loadEntityTree(Scene& scene, std::string_view text, Entity parent,
                                    Entity before)
{
    core::Result<TextDocument> document = serialization::parseText(text);
    if (!document)
    {
        return std::unexpected(document.error());
    }
    core::Result<std::vector<Entity>> loaded = loadEntitySections(scene, *document, 0);
    if (!loaded)
    {
        return std::unexpected(loaded.error());
    }
    if (loaded->empty())
    {
        return core::makeError(core::ErrorCode::Parse, "the text contains no entity");
    }

    const Entity root = loaded->front();
    if (core::Result<void> placed = scene.setParent(root, parent, before); !placed)
    {
        scene.destroyEntity(root);
        return std::unexpected(placed.error());
    }
    return root;
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
