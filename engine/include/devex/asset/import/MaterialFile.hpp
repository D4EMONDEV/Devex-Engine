#pragma once

#include <devex/core/Export.hpp>

#include <devex/asset/MaterialData.hpp>
#include <devex/core/Error.hpp>

#include <string>
#include <string_view>

namespace devex::asset {

inline constexpr std::string_view materialExtension = ".dvxmat";

// Material written by hand or by tools. Every property is optional:
//
//     [material format=2]
//     base_color = vec4(1, 1, 1, 1)
//     base_color_texture = asset("6f1c2a9e-3b7d-4e21-9a55-0c8d7e4f1b23")
//     roughness = 0.8
//     alpha_mode = "mask"
//
// A material drawn by a shader of the project names it, and gives its uniforms in a section of their
// own: numbers, vec2(...) to vec4(...), true or false, and asset("...") for textures.
//
//     [material format=2]
//     shader = asset("0d6f5c1e-8a2b-4c3d-9e4f-5a6b7c8d9e0f")
//
//     [parameters]
//     speed = 1.5
//     tint = vec3(0.2, 0.5, 0.9)
//     noise = asset("6f1c2a9e-3b7d-4e21-9a55-0c8d7e4f1b23")
[[nodiscard]] DEVEX_API core::Result<MaterialData> parseMaterialFile(std::string_view text);
[[nodiscard]] DEVEX_API std::string writeMaterialFile(const MaterialData& material);

} // namespace devex::asset
