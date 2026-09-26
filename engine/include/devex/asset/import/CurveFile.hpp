#pragma once

#include <devex/asset/CurveData.hpp>
#include <devex/core/Error.hpp>

#include <string>
#include <string_view>

namespace devex::asset {

// A curve written by the editor, one key per section:
//
//     [curve format=1]
//
//     [key time=0 value=0 in=0 out=2]
//     [key time=1 value=1 in=0 out=0]
[[nodiscard]] core::Result<CurveData> parseCurveFile(std::string_view text);
[[nodiscard]] std::string writeCurveFile(const CurveData& curve);

} // namespace devex::asset
