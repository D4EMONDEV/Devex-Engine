#pragma once

#include <devex/asset/AnimatorData.hpp>
#include <devex/core/Error.hpp>

#include <string>
#include <string_view>

namespace devex::asset {

// A .dvxanimator file:
//
//     [animator format=1 entry="Locomotion"]
//     entry_position = vec2(-240, 0)
//
//     [parameter name="Speed" type="float" default=0]
//     [parameter name="Wave" type="trigger"]
//
//     [state name="Locomotion" blend="1d" parameter="Speed"]
//     position = vec2(0, 0)
//     clips = list(asset("..."), asset("..."))
//     thresholds = list(0, 1.6)
//
//     [transition from="Locomotion" to="Waving" duration=0.25]
//     conditions = list(trigger("Wave"), less("Speed", 0.1))
//
// Conditions are greater(), less(), equals() and not_equals() of a parameter and a number,
// is() of a bool parameter and true or false, and trigger() of a trigger. A transition without
// from leaves any state.
[[nodiscard]] core::Result<AnimatorData> parseAnimatorFile(std::string_view text);
[[nodiscard]] std::string writeAnimatorFile(const AnimatorData& animator);

} // namespace devex::asset
