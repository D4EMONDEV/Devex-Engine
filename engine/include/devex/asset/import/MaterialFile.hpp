#pragma once

#include <devex/asset/MaterialData.hpp>
#include <devex/core/Error.hpp>

#include <string>
#include <string_view>

namespace devex::asset {

inline constexpr std::string_view materialExtension = ".dvxmat";

// Material written by hand or by tools. Every property is optional:
//
//     [material format=1]
//     base_color = vec4(1, 1, 1, 1)
//     base_color_texture = asset("6f1c2a9e-3b7d-4e21-9a55-0c8d7e4f1b23")
//     roughness = 0.8
//     alpha_mode = "mask"
[[nodiscard]] core::Result<MaterialData> parseMaterialFile(std::string_view text);
[[nodiscard]] std::string writeMaterialFile(const MaterialData& material);

} // namespace devex::asset
