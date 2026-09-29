#pragma once

#include <devex/core/Export.hpp>

#include <devex/asset/TilesetData.hpp>
#include <devex/core/Error.hpp>

#include <string>
#include <string_view>

namespace devex::asset {

// A tileset written by the editor, one section per tile; frames, fps and data only when set:
//
//     [tileset format=1]
//
//     [tile id=1 sprite=asset("6f1c2a9e-3b7d-4e21-9a55-0c8d7e4f1b23") collision="full"]
//
//     [tile id=2 sprite=asset("b41e7c02-9d3a-4f6e-8c11-5a2e9b7d0f44") collision="none"]
//     frames = list(asset("..."), asset("..."))
//     fps = 6
//     data = "water"
[[nodiscard]] DEVEX_API core::Result<TilesetData> parseTilesetFile(std::string_view text);
[[nodiscard]] DEVEX_API std::string writeTilesetFile(const TilesetData& tileset);

} // namespace devex::asset
