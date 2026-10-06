#pragma once

#include <devex/core/Export.hpp>

#include <devex/asset/TilesetData.hpp>
#include <devex/core/Error.hpp>

#include <string>
#include <string_view>

namespace devex::asset {

// A tileset written by the editor: its terrain sets, each followed by its terrains, then one section
// per tile; occluder, frames, fps, data, terrains and probability only when set. The bits of a tile
// are the terrains of its sides and corners, counterclockwise from the right (see TileNeighbor), -1
// for none:
//
//     [tileset format=2]
//
//     [terrain_set mode="sides" mirror_x=true mirror_y=false]
//
//     [terrain name="Ground" color=vec4(0.4, 0.7, 0.25, 1)]
//
//     [tile id=1 sprite=asset("6f1c2a9e-3b7d-4e21-9a55-0c8d7e4f1b23") collision="full" terrain_set=0 terrain=0]
//     bits = list(0, -1, -1, -1, 0, -1, 0, -1)
//
//     [tile id=2 sprite=asset("b41e7c02-9d3a-4f6e-8c11-5a2e9b7d0f44") collision="none"]
//     frames = list(asset("..."), asset("..."))
//     fps = 6
//     data = "water"
//     probability = 0.5
//
// Files of format 1, without terrains, still read.
[[nodiscard]] DEVEX_API core::Result<TilesetData> parseTilesetFile(std::string_view text);
[[nodiscard]] DEVEX_API std::string writeTilesetFile(const TilesetData& tileset);

} // namespace devex::asset
