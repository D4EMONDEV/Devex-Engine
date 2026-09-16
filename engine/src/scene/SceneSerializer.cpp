#include <devex/asset/AssetId.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/SceneSerializer.hpp>
#include <devex/serialization/Text.hpp>

#include <array>
#include <charconv>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace devex::scene {
namespace {

using serialization::TextCall;
using serialization::TextDocument;
using serialization::TextProperty;
using serialization::TextSection;
using serialization::TextValue;

// Returns the double closest to the shortest decimal form of the float, so that the file shows
// 0.1 rather than 0.10000000149011612 while still reading back to the same float.
[[nodiscard]] double shortestDouble(float value) noexcept
{
    std::array<char, 32> text{};
    const auto [end, writeStatus] = std::to_chars(text.data(), text.data() + text.size(), value);
    double result = value;
    std::from_chars(text.data(), end, result);
    return result;
}

[[nodiscard]] TextValue numbers(std::string name, std::span<const float> components)
{
    std::vector<TextValue> arguments;
    for (const float component : components)
    {
        arguments.emplace_back(shortestDouble(component));
    }
    return serialization::makeCall(std::move(name), std::move(arguments));
}

[[nodiscard]] TextValue toText(reflection::ValueKind kind, const void* address)
{
    using reflection::ValueKind;
    switch (kind)
    {
    case ValueKind::Bool:
        return TextValue(*static_cast<const bool*>(address));
    case ValueKind::Int32:
        return TextValue(std::int64_t{*static_cast<const std::int32_t*>(address)});
    case ValueKind::UInt32:
        return TextValue(std::int64_t{*static_cast<const std::uint32_t*>(address)});
    case ValueKind::Float:
        return TextValue(shortestDouble(*static_cast<const float*>(address)));
    case ValueKind::String:
        return TextValue(*static_cast<const std::string*>(address));
    case ValueKind::Vec2: {
        const auto& vector = *static_cast<const math::Vec2*>(address);
        return numbers("vec2", std::array{vector.x, vector.y});
    }
    case ValueKind::Vec3: {
        const auto& vector = *static_cast<const math::Vec3*>(address);
        return numbers("vec3", std::array{vector.x, vector.y, vector.z});
    }
    case ValueKind::Vec4: {
        const auto& vector = *static_cast<const math::Vec4*>(address);
        return numbers("vec4", std::array{vector.x, vector.y, vector.z, vector.w});
    }
    case ValueKind::Quat: {
        const auto& quaternion = *static_cast<const math::Quat*>(address);
        return numbers("quat", std::array{quaternion.x, quaternion.y, quaternion.z, quaternion.w});
    }
    case ValueKind::Uuid:
        return TextValue(static_cast<const core::Uuid*>(address)->toString());
    case ValueKind::AssetId:
        return serialization::makeCall(
            "asset", {TextValue(static_cast<const asset::AssetId*>(address)->uuid.toString())});
    }
    DEVEX_UNREACHABLE();
}

// Reads a call such as vec3(x, y, z) whose arguments are all numbers.
[[nodiscard]] core::Result<void> readNumbers(const TextValue& value, std::string_view name,
                                             std::span<float> components)
{
    const TextCall* const call = serialization::asCall(value, name);
    if (call == nullptr || call->arguments.size() != components.size())
    {
        return core::makeError(core::ErrorCode::Parse, "expected {}() with {} numbers", name,
                               components.size());
    }
    for (std::size_t index = 0; index < components.size(); ++index)
    {
        const std::optional<double> number = serialization::asNumber(call->arguments[index]);
        if (!number)
        {
            return core::makeError(core::ErrorCode::Parse, "{}() arguments must be numbers", name);
        }
        components[index] = static_cast<float>(*number);
    }
    return {};
}

[[nodiscard]] core::Result<std::int64_t> readInteger(const TextValue& value, std::int64_t minimum,
                                                     std::int64_t maximum)
{
    const std::optional<std::int64_t> integer = serialization::asInteger(value);
    if (!integer || *integer < minimum || *integer > maximum)
    {
        return core::makeError(core::ErrorCode::Parse, "expected an integer in [{}, {}]", minimum,
                               maximum);
    }
    return *integer;
}

[[nodiscard]] core::Result<core::Uuid> readUuid(const TextValue& value)
{
    const std::string* const text = serialization::asString(value);
    std::optional<core::Uuid> uuid = text != nullptr ? core::Uuid::parse(*text) : std::nullopt;
    if (!uuid)
    {
        return core::makeError(core::ErrorCode::Parse, "expected a UUID string");
    }
    return *uuid;
}

[[nodiscard]] core::Result<void> fromText(reflection::ValueKind kind, const TextValue& value,
                                          void* address)
{
    using reflection::ValueKind;
    switch (kind)
    {
    case ValueKind::Bool: {
        const std::optional<bool> boolean = serialization::asBool(value);
        if (!boolean)
        {
            return core::makeError(core::ErrorCode::Parse, "expected true or false");
        }
        *static_cast<bool*>(address) = *boolean;
        return {};
    }
    case ValueKind::Int32: {
        const core::Result<std::int64_t> integer =
            readInteger(value, std::numeric_limits<std::int32_t>::min(),
                        std::numeric_limits<std::int32_t>::max());
        if (!integer)
        {
            return std::unexpected(integer.error());
        }
        *static_cast<std::int32_t*>(address) = static_cast<std::int32_t>(*integer);
        return {};
    }
    case ValueKind::UInt32: {
        const core::Result<std::int64_t> integer =
            readInteger(value, 0, std::numeric_limits<std::uint32_t>::max());
        if (!integer)
        {
            return std::unexpected(integer.error());
        }
        *static_cast<std::uint32_t*>(address) = static_cast<std::uint32_t>(*integer);
        return {};
    }
    case ValueKind::Float: {
        const std::optional<double> number = serialization::asNumber(value);
        if (!number)
        {
            return core::makeError(core::ErrorCode::Parse, "expected a number");
        }
        *static_cast<float*>(address) = static_cast<float>(*number);
        return {};
    }
    case ValueKind::String: {
        const std::string* const text = serialization::asString(value);
        if (text == nullptr)
        {
            return core::makeError(core::ErrorCode::Parse, "expected a string");
        }
        *static_cast<std::string*>(address) = *text;
        return {};
    }
    case ValueKind::Vec2: {
        std::array<float, 2> xy{};
        if (core::Result<void> read = readNumbers(value, "vec2", xy); !read)
        {
            return read;
        }
        *static_cast<math::Vec2*>(address) = math::Vec2(xy[0], xy[1]);
        return {};
    }
    case ValueKind::Vec3: {
        std::array<float, 3> xyz{};
        if (core::Result<void> read = readNumbers(value, "vec3", xyz); !read)
        {
            return read;
        }
        *static_cast<math::Vec3*>(address) = math::Vec3(xyz[0], xyz[1], xyz[2]);
        return {};
    }
    case ValueKind::Vec4: {
        std::array<float, 4> xyzw{};
        if (core::Result<void> read = readNumbers(value, "vec4", xyzw); !read)
        {
            return read;
        }
        *static_cast<math::Vec4*>(address) = math::Vec4(xyzw[0], xyzw[1], xyzw[2], xyzw[3]);
        return {};
    }
    case ValueKind::Quat: {
        std::array<float, 4> xyzw{};
        if (core::Result<void> read = readNumbers(value, "quat", xyzw); !read)
        {
            return read;
        }
        *static_cast<math::Quat*>(address) = math::Quat(xyzw[3], xyzw[0], xyzw[1], xyzw[2]);
        return {};
    }
    case ValueKind::Uuid: {
        const core::Result<core::Uuid> uuid = readUuid(value);
        if (!uuid)
        {
            return std::unexpected(uuid.error());
        }
        *static_cast<core::Uuid*>(address) = *uuid;
        return {};
    }
    case ValueKind::AssetId: {
        const TextCall* const call = serialization::asCall(value, "asset");
        const core::Result<core::Uuid> uuid =
            call != nullptr && call->arguments.size() == 1
                ? readUuid(call->arguments.front())
                : core::makeError(core::ErrorCode::Parse, "not an asset reference");
        if (!uuid)
        {
            return core::makeError(core::ErrorCode::Parse, "expected asset(\"<uuid>\")");
        }
        *static_cast<asset::AssetId*>(address) = asset::AssetId{*uuid};
        return {};
    }
    }
    DEVEX_UNREACHABLE();
}

[[nodiscard]] std::unexpected<core::Error> errorAt(std::uint32_t line, const core::Error& error)
{
    return core::makeError(error.code, "line {}: {}", line, error.message);
}

void writeEntity(const Scene& scene, Entity entity, TextDocument& document)
{
    TextSection entitySection{.type = "entity"};
    entitySection.attributes.push_back({"uuid", TextValue(scene.uuid(entity).toString())});
    entitySection.attributes.push_back({"name", TextValue(scene.name(entity))});
    if (const Entity parent = scene.parent(entity); parent.isValid())
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
                {field.name, toText(field.kind, field.address(component))});
        }
        document.sections.push_back(std::move(componentSection));
    }

    for (Entity child = scene.firstChild(entity); child.isValid(); child = scene.nextSibling(child))
    {
        writeEntity(scene, child, document);
    }
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
        writeEntity(scene, root, document);
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

    struct PendingParent
    {
        Entity child;
        core::Uuid parent;
        std::uint32_t line = 0;
    };

    Scene scene;
    std::vector<PendingParent> pendingParents;
    Entity current;

    for (std::size_t index = 1; index < document->sections.size(); ++index)
    {
        const TextSection& section = document->sections[index];
        if (section.type == "entity")
        {
            const TextValue* const uuidValue = section.findAttribute("uuid");
            core::Result<core::Uuid> uuid =
                uuidValue != nullptr
                    ? readUuid(*uuidValue)
                    : core::makeError(core::ErrorCode::Parse, "an entity needs a uuid");
            if (!uuid)
            {
                return errorAt(section.line, uuid.error());
            }

            const TextValue* const nameValue = section.findAttribute("name");
            const std::string* const name =
                nameValue != nullptr ? serialization::asString(*nameValue) : nullptr;
            core::Result<Entity> entity = scene.createEntity(*uuid, name != nullptr ? *name : "");
            if (!entity)
            {
                return errorAt(section.line, entity.error());
            }
            current = *entity;

            if (const TextValue* const parentValue = section.findProperty("parent"))
            {
                core::Result<core::Uuid> parent = readUuid(*parentValue);
                if (!parent)
                {
                    return errorAt(section.line, parent.error());
                }
                pendingParents.push_back({current, *parent, section.line});
            }
        }
        else if (section.type == "component")
        {
            if (!current.isValid())
            {
                return core::makeError(core::ErrorCode::Parse,
                                       "line {}: a component must follow an entity", section.line);
            }
            const TextValue* const typeValue = section.findAttribute("type");
            const std::string* const typeName =
                typeValue != nullptr ? serialization::asString(*typeValue) : nullptr;
            if (typeName == nullptr)
            {
                return core::makeError(core::ErrorCode::Parse,
                                       "line {}: a component needs a type", section.line);
            }

            const ComponentType* const componentType = componentRegistry().find(*typeName);
            if (componentType == nullptr)
            {
                DEVEX_LOG_WARNING("Scene line {}: skipping unknown component type '{}'",
                                  section.line, *typeName);
                continue;
            }

            void* const component = componentType->emplace(scene, current);
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
                        fromText(field->kind, property.value, field->address(component));
                    !read)
                {
                    return core::makeError(core::ErrorCode::Parse, "line {}: {}.{}: {}",
                                           property.line, *typeName, property.key,
                                           read.error().message);
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
            return core::makeError(core::ErrorCode::NotFound, "line {}: parent {} does not exist",
                                   pending.line, pending.parent);
        }
        if (core::Result<void> parented = scene.setParent(pending.child, parent); !parented)
        {
            return errorAt(pending.line, parented.error());
        }
    }
    return scene;
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
