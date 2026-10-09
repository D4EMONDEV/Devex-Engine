#pragma once

#include <devex/core/Export.hpp>

#include <devex/asset/AnimationData.hpp>
#include <devex/asset/SpriteData.hpp>
#include <devex/core/Error.hpp>
#include <devex/serialization/Text.hpp>

#include <span>
#include <vector>

// The events of animations as files write them: list(event(0.42, "step"), event(0.92, "step")), a
// time in seconds for the clips of models, in their import settings, and a frame for the animations
// of sprites, in their .dvxframes.
namespace devex::asset {

[[nodiscard]] DEVEX_API core::Result<std::vector<AnimationEvent>> readAnimationEvents(const serialization::TextValue& value);
[[nodiscard]] DEVEX_API serialization::TextValue writeAnimationEvents(std::span<const AnimationEvent> events);
[[nodiscard]] DEVEX_API core::Result<std::vector<SpriteAnimationEvent>> readSpriteAnimationEvents(const serialization::TextValue& value);
[[nodiscard]] DEVEX_API serialization::TextValue writeSpriteAnimationEvents(std::span<const SpriteAnimationEvent> events);

} // namespace devex::asset
