#pragma once

#include <devex/core/Export.hpp>

#include <devex/math/Math.hpp>

#include <cstdint>

// Cube maps as the renderer samples them: six faces in the order of Vulkan (+X, -X, +Y, -Y, +Z, -Z),
// and the cameras that capture them.
namespace devex::render {

// The direction through the center of a pixel of a face of a cube map of size × size pixels, from the
// top row down, as the GPU samples it.
[[nodiscard]] DEVEX_API math::Vec3 cubeFaceDirection(std::uint32_t face, std::uint32_t x, std::uint32_t y,
                                                     std::uint32_t size) noexcept;

// The view of a camera at a point that captures a face of a cube map: drawn at 90 degrees into a square
// image, it shows the face mirrored left to right, as cube maps are laid out for a viewer inside them.
[[nodiscard]] DEVEX_API math::Mat4 cubeFaceView(math::Vec3 position, std::uint32_t face) noexcept;

} // namespace devex::render
