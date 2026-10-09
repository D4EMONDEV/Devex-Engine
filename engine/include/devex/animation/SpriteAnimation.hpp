#pragma once

#include <devex/core/Export.hpp>

#include <devex/animation/AnimationWorld.hpp>
#include <devex/asset/AssetId.hpp>
#include <devex/asset/SpriteData.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/scene/SpriteComponents.hpp>

#include <functional>
#include <memory>
#include <string_view>
#include <vector>

// The frame by frame animation of sprites: SpriteAnimator components moving through the frames of
// their sprite frames while the game plays.
namespace devex::animation {

// Finds the sprite frames an animator names; null when they cannot be loaded.
using SpriteFramesSource = std::function<std::shared_ptr<const asset::SpriteFramesData>(asset::AssetId)>;

// The animation an animator plays: the one it names, or the first one when it names none. Null
// when the frames have no such animation.
[[nodiscard]] DEVEX_API const asset::SpriteAnimationData* animationOf(const asset::SpriteFramesData& frames,
                                                                      const scene::SpriteAnimator& animator) noexcept;

// The sprite an animator shows: the frame it is on, kept within its animation. Invalid when its
// animation is unknown or empty.
[[nodiscard]] DEVEX_API asset::AssetId spriteOf(const asset::SpriteFramesData& frames, const scene::SpriteAnimator& animator) noexcept;

// Moves an animator through its animation by a duration. Naming another animation starts it from
// its first frame; a paused animator stays where it is; an animation that does not loop stops on
// its last frame (its first, backwards) and turns playing off. The names of the events of the frames
// it comes to go into `passed`, and those of the first frame of an animation that starts.
DEVEX_API void advance(scene::SpriteAnimator& animator, const asset::SpriteAnimationData& animation, float seconds,
                       std::vector<std::string_view>* passed = nullptr);

// Advances every SpriteAnimator of the scene, with the events they pass.
DEVEX_API void updateSpriteAnimators(scene::Scene& scene, const SpriteFramesSource& frames, float seconds,
                                     std::vector<FiredAnimationEvent>* fired = nullptr);

} // namespace devex::animation
