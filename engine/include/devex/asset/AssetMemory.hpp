#pragma once

#include <devex/asset/AssetId.hpp>
#include <devex/asset/AssetType.hpp>

#include <cstddef>
#include <string>
#include <vector>

// What the assets a game has loaded take, in the memory of the process and in that of the GPU:
// what the developer of a game can make smaller.
namespace devex::asset {

struct AssetMemory
{
    AssetId id;
    std::string name;
    AssetType type = AssetType::Mesh;
    std::size_t cpuBytes = 0;
    std::size_t gpuBytes = 0;
};

struct MemoryByType
{
    AssetType type = AssetType::Mesh;
    std::size_t count = 0;
    std::size_t cpuBytes = 0;
    std::size_t gpuBytes = 0;
};

struct MemoryReport
{
    // One line per type that has something loaded, the heaviest first.
    std::vector<MemoryByType> types;
    // The heaviest assets, CPU and GPU together.
    std::vector<AssetMemory> largest;
};

} // namespace devex::asset
