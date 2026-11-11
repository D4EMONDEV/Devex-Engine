#include "RemoteInspection.hpp"

#include <devex/core/File.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/FieldValue.hpp>
#include <devex/serialization/Text.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <span>
#include <unordered_set>
#include <utility>

namespace devex::runtime::detail
{
namespace
{

using namespace tools::detail;
using namespace serialization;

constexpr std::size_t maxEntities = 4096;
constexpr std::size_t maxComponents = 128;
constexpr std::size_t maxProperties = 512;
constexpr std::size_t maxNameBytes = 160;
constexpr std::size_t maxValueBytes = 384;
constexpr std::size_t maxSnapshotBytes = 4 * 1024 * 1024;

std::string shortened(std::string_view value, std::size_t limit)
{
    if (value.size() <= limit)
        return std::string(value);
    std::size_t end = limit - 3;
    while (end != 0 && (static_cast<unsigned char>(value[end]) & 0xc0) == 0x80)
        --end;
    return std::string(value.substr(0, end)) + "...";
}

bool hasNonFiniteNumber(const TextValue& value)
{
    if (const auto* number = std::get_if<double>(&value))
        return !std::isfinite(*number);
    const auto* call = std::get_if<TextCall>(&value);
    return call != nullptr && std::ranges::any_of(call->arguments, hasNonFiniteNumber);
}

std::string propertyValue(const reflection::FieldInfo& field, const void* address)
{
    if (field.list != nullptr)
        return std::format("{} elements", field.list->size(address));
    if (field.kind == reflection::ValueKind::String)
        return shortened(*static_cast<const std::string*>(address), maxValueBytes);
    const auto value = scene::writeFieldValue(field, address);
    return hasNonFiniteNumber(value) ? "Non-finite number"
                                     : shortened(formatValue(value), maxValueBytes);
}

void captureComponents(TextDocument& document, const scene::Scene& scene, scene::Entity entity)
{
    const auto inspected = document.sections.size();
    document.sections.push_back(
        {.type = "inspected",
         .attributes = {{"name", shortened(scene.name(entity), maxNameBytes)}}});
    std::size_t components = 0;
    std::size_t properties = 0;
    bool limited = false;
    for (const auto& type : scene::componentRegistry().types())
    {
        const void* const value = type.find(scene, entity);
        if (value == nullptr)
            continue;
        if (components == maxComponents || properties == maxProperties)
        {
            limited = true;
            break;
        }
        ++components;
        TextSection section{.type = "component",
                            .attributes = {{"name", shortened(type.name(), maxNameBytes)}}};
        for (const auto& field : type.type->fields)
        {
            if (field.hidden)
                continue;
            if (properties == maxProperties)
            {
                limited = true;
                break;
            }
            ++properties;
            section.properties.push_back(
                {"field", makeCall("property", {shortened(field.name, maxNameBytes),
                                                propertyValue(field, field.address(value))})});
        }
        document.sections.push_back(std::move(section));
    }
    document.sections[inspected].attributes.push_back({"limited", limited});
}

bool flag(const TextSection& section, std::string_view key)
{
    const auto* value = section.findAttribute(key);
    return value != nullptr && asBool(*value).value_or(false);
}

std::optional<std::int64_t> integer(const TextSection& section, std::string_view key)
{
    const auto* value = section.findAttribute(key);
    return value != nullptr ? asInteger(*value) : std::nullopt;
}

} // namespace

core::Result<void> writeRemoteInspection(const std::filesystem::path& path,
                                         const scene::Scene& scene, std::int64_t serial,
                                         core::Uuid selected)
{
    TextDocument document{
        {{.type = "remote_scene",
          .attributes = {{"serial", serial}, {"selection", selected.toString()}}}}};
    // Walk the hierarchy without recursion or a copy of the game world.
    scene::Entity entity = scene.firstRoot();
    std::int64_t depth = 0;
    for (std::size_t count = 0; entity.isValid() && count < maxEntities; ++count)
    {
        document.sections.push_back(
            {.type = "entity",
             .attributes = {{"id", scene.uuid(entity).toString()},
                            {"name", shortened(scene.name(entity), maxNameBytes)},
                            {"depth", depth}}});
        if (const auto child = scene.firstChild(entity); child.isValid())
        {
            entity = child;
            ++depth;
            continue;
        }
        while (entity.isValid() && !scene.nextSibling(entity).isValid())
        {
            entity = scene.parent(entity);
            --depth;
        }
        if (entity.isValid())
            entity = scene.nextSibling(entity);
    }
    document.sections.front().attributes.push_back({"limited", entity.isValid()});
    if (const auto inspected = scene.findEntity(selected); inspected.isValid())
        captureComponents(document, scene, inspected);

    const std::string snapshot = writeText(document);
    if (snapshot.size() > maxSnapshotBytes)
        return core::makeError(core::ErrorCode::InvalidState,
                               "The remote scene snapshot is too large");
    return core::writeFileAtomically(path, std::as_bytes(std::span(snapshot)));
}

core::Result<RemoteSceneSnapshot> readRemoteInspection(const std::filesystem::path& path,
                                                       std::int64_t serial, core::Uuid selected)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const auto size = file.tellg();
    if (size <= 0 || size > static_cast<std::streamoff>(maxSnapshotBytes))
        return core::makeError(core::ErrorCode::Io,
                               "The remote scene snapshot is missing or too large");
    std::string snapshot(static_cast<std::size_t>(size), '\0');
    file.seekg(0);
    if (!file.read(snapshot.data(), static_cast<std::streamsize>(snapshot.size())))
        return core::makeError(core::ErrorCode::Io, "Cannot read the remote scene snapshot");

    const auto invalid = []
    { return core::makeError(core::ErrorCode::Parse, "Invalid remote scene snapshot"); };
    const auto document = parseText(snapshot);
    if (!document || document->sections.empty())
        return invalid();
    const auto& header = document->sections.front();
    const auto* selection = stringAttribute(header, "selection");
    if (header.type != "remote_scene" || integer(header, "serial") != serial ||
        selection == nullptr || core::Uuid::parse(*selection) != selected)
        return invalid();

    RemoteSceneSnapshot result{.selection = selected, .treeLimited = flag(header, "limited")};
    std::unordered_set<core::Uuid> ids;
    std::int64_t lastDepth = -1;
    std::size_t properties = 0;
    for (const auto& section : std::span(document->sections).subspan(1))
    {
        const auto* name = stringAttribute(section, "name");
        if (name == nullptr || name->size() > maxNameBytes)
            return invalid();
        if (section.type == "entity" && !result.selectedName &&
            result.entities.size() < maxEntities)
        {
            const auto* id = stringAttribute(section, "id");
            const auto parsed = id != nullptr ? core::Uuid::parse(*id) : std::nullopt;
            const auto depth = integer(section, "depth");
            if (!parsed || parsed->isNil() || !ids.insert(*parsed).second || !depth || *depth < 0 ||
                *depth > lastDepth + 1)
                return invalid();
            result.entities.push_back({*parsed, *name, static_cast<std::uint32_t>(*depth)});
            lastDepth = *depth;
        }
        else if (section.type == "inspected" && !selected.isNil() && !result.selectedName)
        {
            result.selectedName = *name;
            result.propertiesLimited = flag(section, "limited");
        }
        else if (section.type == "component" && result.selectedName &&
                 result.components.size() < maxComponents)
        {
            RemoteComponent component{.name = *name};
            for (const auto& property : section.properties)
            {
                const auto* field = asCall(property.value, "property");
                if (property.key != "field" || ++properties > maxProperties || field == nullptr ||
                    field->arguments.size() != 2)
                    return invalid();
                const auto* label = asString(field->arguments[0]);
                const auto* value = asString(field->arguments[1]);
                if (label == nullptr || label->size() > maxNameBytes || value == nullptr ||
                    value->size() > maxValueBytes)
                    return invalid();
                component.properties.push_back({*label, *value});
            }
            result.components.push_back(std::move(component));
        }
        else
            return invalid();
    }
    return result;
}

} // namespace devex::runtime::detail
