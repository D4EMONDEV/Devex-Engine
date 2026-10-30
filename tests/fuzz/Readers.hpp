#pragma once

#include <devex/asset/Artifact.hpp>
#include <devex/scene/SceneSerializer.hpp>

#include <cstddef>
#include <cstdlib>
#include <span>
#include <string_view>

namespace devex::tests {

// These invariants stay active in optimized fuzz builds. libFuzzer keeps the offending input.
inline void checkTerrain(std::span<const std::byte> bytes)
{
    const auto terrain = asset::decodeTerrain(bytes);
    if (terrain)
    {
        const auto restored = asset::decodeTerrain(asset::encodeTerrain(*terrain));
        if (!restored || *restored != *terrain)
        {
            std::abort();
        }
    }
}

inline void checkMesh(std::span<const std::byte> bytes)
{
    const auto mesh = asset::decodeMesh(bytes);
    if (mesh)
    {
        const auto encoded = asset::encodeMesh(*mesh);
        const auto restored = asset::decodeMesh(encoded);
        if (!restored || asset::encodeMesh(*restored) != encoded)
        {
            std::abort();
        }
    }
}

inline void checkScene(std::span<const std::byte> bytes)
{
    const std::string_view text{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    // No project or asset loader: prefab references remain unresolved, without file access.
    auto loaded = scene::loadScene(text, scene::PrefabLoading::KeepUnresolved);
    if (loaded)
    {
        const auto saved = scene::saveScene(*loaded);
        const auto restored = scene::loadScene(saved, scene::PrefabLoading::KeepUnresolved);
        if (!restored || scene::saveScene(*restored) != saved)
        {
            std::abort();
        }
    }
}

} // namespace devex::tests
