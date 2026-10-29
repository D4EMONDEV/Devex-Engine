#pragma once

#include <devex/core/Export.hpp>

#include <devex/math/Math.hpp>
#include <devex/render/RenderWorld.hpp>

#include <span>

// The settings of the image where the camera stands: its own, under those of the post-process volumes
// around it, as Unity's volumes blend theirs.
namespace devex::render {

// How much a volume gives at a point: its weight inside its box, fading to nothing over its blend
// distance outside it; everywhere for a global one.
[[nodiscard]] DEVEX_API float postVolumeInfluence(const RenderPostVolume& volume, math::Vec3 point) noexcept;

// The camera with the settings of the volumes around where it stands, the lowest priority first: each
// setting a volume overrides moves towards the volume's by its influence. Without lens effects, the
// result has neither depth of field nor motion blur.
[[nodiscard]] DEVEX_API RenderCamera blendPostVolumes(const RenderCamera& camera, std::span<const RenderPostVolume> volumes) noexcept;

// The factors of the long, medium and short cones of the eye (LMS) that move the white balance by a
// temperature and a tint from -1 to 1, as Unity's white balance does; 1, 1, 1 for neither.
[[nodiscard]] DEVEX_API math::Vec3 whiteBalanceFactors(float temperature, float tint) noexcept;

} // namespace devex::render
