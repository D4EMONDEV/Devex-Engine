#pragma once

#include <devex/asset/ThemeData.hpp>
#include <devex/core/Error.hpp>

#include <string>
#include <string_view>

namespace devex::asset {

// A theme written by hand or by the editor: one section per style and component, one line per
// value it sets. A style that touches several components is written once per component.
//
//     [theme format=1]
//
//     [style name="panel" component="UiImage"]
//     color = vec4(0.10, 0.10, 0.12, 0.92)
//     corner_radius = 12
//
//     [style name="title" component="UiText"]
//     size = 34
//     color = vec4(1, 1, 1, 1)
[[nodiscard]] core::Result<ThemeData> parseThemeFile(std::string_view text);
[[nodiscard]] std::string writeThemeFile(const ThemeData& theme);

} // namespace devex::asset
