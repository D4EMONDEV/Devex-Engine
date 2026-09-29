#pragma once

#include <devex/core/Export.hpp>

#include <devex/asset/SpriteData.hpp>
#include <devex/core/Error.hpp>

#include <string>
#include <string_view>

namespace devex::asset {

// The animations of sprites written by the editor, one section each, with the sprites they show:
//
//     [frames format=1]
//
//     [animation name="run" fps=12 loop=true]
//     frames = list(asset("6f1c2a9e-3b7d-4e21-9a55-0c8d7e4f1b23"), asset("b41e7c02-9d3a-4f6e-8c11-5a2e9b7d0f44"))
[[nodiscard]] DEVEX_API core::Result<SpriteFramesData> parseSpriteFramesFile(std::string_view text);
[[nodiscard]] DEVEX_API std::string writeSpriteFramesFile(const SpriteFramesData& frames);

} // namespace devex::asset
