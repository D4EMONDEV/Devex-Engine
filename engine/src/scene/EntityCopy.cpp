#include <devex/scene/EntityCopy.hpp>

#include "SceneText.hpp"

#include <devex/scene/Prefab.hpp>
#include <devex/serialization/Text.hpp>

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace devex::scene {
namespace {

using serialization::TextCall;
using serialization::TextDocument;
using serialization::TextSection;
using serialization::TextValue;

constexpr std::string_view headerType = "entities";

using UuidMap = std::unordered_map<core::Uuid, core::Uuid>;

// Makes the entity("<uuid>") values that name a copied entity name its copy, in lists as well.
void remapReferences(TextValue& value, const UuidMap& map)
{
    auto* const call = std::get_if<TextCall>(&value);
    if (call == nullptr)
    {
        return;
    }
    if (call->name == "entity" && call->arguments.size() == 1)
    {
        if (const core::Result<core::Uuid> uuid = detail::readUuid(call->arguments.front()))
        {
            if (const auto found = map.find(*uuid); found != map.end())
            {
                call->arguments.front() = TextValue(found->second.toString());
            }
        }
        return;
    }
    for (TextValue& argument : call->arguments)
    {
        remapReferences(argument, map);
    }
}

// Replaces a UUID written as a string, when the map has it.
void remapUuid(TextValue& value, const UuidMap& map)
{
    if (const core::Result<core::Uuid> uuid = detail::readUuid(value))
    {
        if (const auto found = map.find(*uuid); found != map.end())
        {
            value = TextValue(found->second.toString());
        }
    }
}

} // namespace

std::string saveEntityTrees(const Scene& scene, std::span<const Entity> roots)
{
    TextDocument document;
    TextSection& header = document.sections.emplace_back();
    header.type = std::string(headerType);
    header.attributes.push_back({"format", TextValue(entityCopyFormatVersion)});

    const auto isUnderOtherRoot = [&](Entity entity) {
        for (Entity ancestor = scene.parent(entity); ancestor.isValid(); ancestor = scene.parent(ancestor))
        {
            if (std::ranges::find(roots, ancestor) != roots.end())
            {
                return true;
            }
        }
        return false;
    };
    for (const Entity root : roots)
    {
        if (!scene.isAlive(root) || isUnderOtherRoot(root))
        {
            continue;
        }
        // As saveEntityTree writes it: an entity inside an instance is written as ordinary entities.
        const PrefabEntity* const fromPrefab = scene.tryGet<PrefabEntity>(root);
        const Entity unpacked = fromPrefab != nullptr && fromPrefab->instance != root ? fromPrefab->instance : Entity{};
        detail::writeEntity(scene, root, false, document, unpacked);
    }
    return serialization::writeText(document);
}

bool isEntityCopy(std::string_view text) noexcept
{
    const auto start = std::ranges::find_if(text, [](char character) {
        return !std::isspace(static_cast<unsigned char>(character));
    });
    const std::string_view rest = text.substr(static_cast<std::size_t>(start - text.begin()));
    return rest.starts_with("[entities ") || rest.starts_with("[entities]");
}

core::Result<std::vector<EntityTreeCopy>> copyEntityTrees(std::string_view text)
{
    core::Result<TextDocument> document = serialization::parseText(text);
    if (!document)
    {
        return std::unexpected(document.error());
    }
    if (document->sections.empty() || document->sections.front().type != headerType)
    {
        return core::makeError(core::ErrorCode::Parse, "the text is not a copy of entities");
    }
    const TextValue* const format = document->sections.front().findAttribute("format");
    if (const std::optional<std::int64_t> version = format != nullptr ? serialization::asInteger(*format) : std::nullopt;
        !version || *version > entityCopyFormatVersion)
    {
        return core::makeError(core::ErrorCode::Parse, "the copy of entities has an unknown format");
    }

    // A new UUID for every entity written, then for the entities their prefab instances make, which
    // derive from the instance.
    UuidMap map;
    for (const TextSection& section : document->sections)
    {
        if (section.type != "entity")
        {
            continue;
        }
        const TextValue* const uuid = section.findAttribute("uuid");
        const core::Result<core::Uuid> read = uuid != nullptr ? detail::readUuid(*uuid)
                                                              : core::makeError(core::ErrorCode::Parse, "an entity needs a uuid");
        if (!read)
        {
            return detail::errorAt(section.line, read.error());
        }
        map.emplace(*read, core::Uuid::generate());
    }
    detail::PrefabStack stack;
    const core::Result<detail::FlatScene> flat = detail::flattenSections(*document, 1, PrefabLoading::Resolve, stack);
    if (!flat)
    {
        return std::unexpected(flat.error());
    }
    for (const detail::FlatEntity& entity : flat->entities)
    {
        if (!entity.owned || map.contains(entity.uuid))
        {
            continue;
        }
        if (const auto instance = map.find(entity.instance); instance != map.end())
        {
            map.emplace(entity.uuid, derivePrefabUuid(instance->second, entity.source));
        }
    }

    // Each root starts a tree: the entity sections that name no parent.
    std::vector<EntityTreeCopy> trees;
    TextDocument tree;
    const auto finishTree = [&] {
        if (!tree.sections.empty())
        {
            trees.back().text = serialization::writeText(tree);
            tree.sections.clear();
        }
    };
    for (std::size_t index = 1; index < document->sections.size(); ++index)
    {
        TextSection section = std::move(document->sections[index]);
        if (section.type == "entity")
        {
            if (TextValue* const uuid = [&]() -> TextValue* {
                    for (serialization::TextProperty& attribute : section.attributes)
                    {
                        if (attribute.key == "uuid")
                        {
                            return &attribute.value;
                        }
                    }
                    return nullptr;
                }())
            {
                remapUuid(*uuid, map);
            }
            bool hasParent = false;
            for (serialization::TextProperty& property : section.properties)
            {
                if (property.key == "parent")
                {
                    remapUuid(property.value, map);
                    hasParent = true;
                }
            }
            if (!hasParent)
            {
                finishTree();
                const TextValue* const uuid = section.findAttribute("uuid");
                const TextValue* const name = section.findAttribute("name");
                const std::string* const nameText = name != nullptr ? serialization::asString(*name) : nullptr;
                trees.push_back({.root = uuid != nullptr ? detail::readUuid(*uuid).value_or(core::Uuid{}) : core::Uuid{},
                                 .name = nameText != nullptr ? *nameText : std::string()});
            }
        }
        else if (trees.empty())
        {
            return core::makeError(core::ErrorCode::Parse, "line {}: a section must follow an entity", section.line);
        }
        // Components and overrides refer to entities in their values; the targets of overrides
        // name entities of the prefab, which do not change.
        for (serialization::TextProperty& property : section.properties)
        {
            if (property.key != "parent")
            {
                remapReferences(property.value, map);
            }
        }
        section.line = 0;
        tree.sections.push_back(std::move(section));
    }
    finishTree();
    if (trees.empty())
    {
        return core::makeError(core::ErrorCode::Parse, "the copy holds no entity");
    }
    return trees;
}

} // namespace devex::scene
