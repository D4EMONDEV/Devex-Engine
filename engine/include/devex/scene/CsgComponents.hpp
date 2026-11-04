#pragma once

#include <devex/asset/AssetId.hpp>
#include <devex/core/Export.hpp>
#include <devex/math/Math.hpp>
#include <devex/reflection/Reflection.hpp>
#include <devex/scene/EntityRef.hpp>

#include <array>
#include <cstdint>
#include <string_view>

// Blocks that make a level, as Godot's CSG nodes: an entity with one of these and no parent with one is the
// root of a solid, drawn and collided with as one mesh. Those under it are added to it, or cut out of it.
namespace devex::scene {

enum class CsgOperation : std::uint8_t
{
    Union,
    Subtract,
    Intersect,
};

struct DEVEX_API CsgBox
{
    math::Vec3 size{1.0f};
    // What it does to what comes before it under the same parent; nothing for the root.
    CsgOperation operation = CsgOperation::Union;
    asset::AssetId material;
    // For the root: whether the solid has a collider.
    bool collision = true;
};

struct DEVEX_API CsgSphere
{
    float radius = 0.5f;
    std::uint32_t sides = 24;
    CsgOperation operation = CsgOperation::Union;
    asset::AssetId material;
    bool collision = true;
};

// Along Y.
struct DEVEX_API CsgCylinder
{
    float radius = 0.5f;
    float height = 1.0f;
    std::uint32_t sides = 24;
    CsgOperation operation = CsgOperation::Union;
    asset::AssetId material;
    bool collision = true;
};

// A flat section along a Path3D, its top on the curve: a road, a rail, a wall. It goes where the path goes,
// whatever its own Transform.
struct DEVEX_API CsgPath
{
    // The entity of the path; none means the parent of this entity.
    EntityRef path;
    float width = 2.0f;
    float depth = 0.2f;
    CsgOperation operation = CsgOperation::Union;
    asset::AssetId material;
    bool collision = true;
};

// Nothing of its own: it groups what is under it, to add or cut it out as one.
struct DEVEX_API CsgCombiner
{
    CsgOperation operation = CsgOperation::Union;
    bool collision = true;
};

// What the root of a solid or a grid makes while the game or the editor runs: not saved, as it comes back.
struct DEVEX_API GeneratedMesh
{
    asset::AssetId mesh;
    bool collision = true;
};

DEVEX_DECLARE_ENGINE_REFLECTION(CsgBox);
DEVEX_DECLARE_ENGINE_REFLECTION(CsgSphere);
DEVEX_DECLARE_ENGINE_REFLECTION(CsgCylinder);
DEVEX_DECLARE_ENGINE_REFLECTION(CsgPath);
DEVEX_DECLARE_ENGINE_REFLECTION(CsgCombiner);

} // namespace devex::scene

template <>
struct devex::reflection::EnumNames<devex::scene::CsgOperation>
{
    static constexpr std::array<std::string_view, 3> names{"union", "subtract", "intersect"};
};
