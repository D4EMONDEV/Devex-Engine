#pragma once

#include <devex/core/Uuid.hpp>
#include <devex/reflection/Reflection.hpp>

#include <functional>

namespace devex::scene {

// A component field that refers to another entity of the same scene. It holds the UUID of the
// entity, so that it survives saving, the copy of a scene that plays, undo and prefab instances,
// whose references between their own entities follow them to the UUIDs of each instance.
// Scene::resolve gives the entity, which is invalid when the reference is empty or the entity gone.
struct EntityRef
{
    core::Uuid uuid;

    [[nodiscard]] bool isNil() const noexcept
    {
        return uuid.isNil();
    }

    bool operator==(const EntityRef&) const = default;
};

} // namespace devex::scene

template <>
struct devex::reflection::ValueTraits<devex::scene::EntityRef>
{
    static constexpr ValueKind kind = ValueKind::Entity;
};

template <>
struct std::hash<devex::scene::EntityRef>
{
    [[nodiscard]] std::size_t operator()(const devex::scene::EntityRef& reference) const noexcept
    {
        return std::hash<devex::core::Uuid>()(reference.uuid);
    }
};
