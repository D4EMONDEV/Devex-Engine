#pragma once

#include <devex/core/Uuid.hpp>
#include <devex/reflection/Reflection.hpp>

#include <compare>
#include <cstddef>
#include <functional>

namespace devex::asset {

// Identifies an asset independently of its file path, so that renaming or moving the file keeps
// references valid. Written as asset("6f1c2a9e-...") in .dvx* files.
struct AssetId
{
    core::Uuid uuid;

    [[nodiscard]] static AssetId generate()
    {
        return {core::Uuid::generate()};
    }

    [[nodiscard]] constexpr bool isValid() const noexcept
    {
        return !uuid.isNil();
    }

    constexpr auto operator<=>(const AssetId&) const noexcept = default;
};

// Assets provided by the engine itself. Their identifiers can never collide with generated ones,
// whose version bits are set.
namespace builtin {

inline constexpr AssetId cubeMesh{core::Uuid::fromParts(0, 1)};
inline constexpr AssetId sphereMesh{core::Uuid::fromParts(0, 2)};
inline constexpr AssetId planeMesh{core::Uuid::fromParts(0, 3)};

} // namespace builtin

} // namespace devex::asset

template <>
struct devex::reflection::ValueTraits<devex::asset::AssetId>
{
    static constexpr ValueKind kind = ValueKind::AssetId;
};

template <>
struct std::hash<devex::asset::AssetId>
{
    [[nodiscard]] std::size_t operator()(const devex::asset::AssetId& id) const noexcept
    {
        return std::hash<devex::core::Uuid>()(id.uuid);
    }
};
