#pragma once

// Which lights of a frame get a shadow, and where in the atlas: the same for every renderer, whatever its API.
#include <devex/core/Export.hpp>
#include <devex/math/Math.hpp>
#include <devex/render/Culling.hpp>
#include <devex/render/RenderWorld.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace devex::render {

// The atlas the views of shadows share: square cells, and blocks of two by two for the lights at the top of the list.
struct ShadowAtlasLayout
{
    std::uint32_t size = 4096;
    std::uint32_t cell = 512;
    std::size_t largeLights = 2;

    [[nodiscard]] constexpr std::uint32_t cells() const noexcept { return cell == 0 ? 0 : size / cell; }
};

// One view of a shadow: from where it looks, and the part of the atlas it draws into, in texture coordinates.
struct ShadowView
{
    math::Mat4 viewProjection{1.0f};
    math::Vec4 tile{0.0f};
};

struct ShadowAssignment
{
    std::vector<ShadowView> views;
    // For each light, its first view, or -1 without a shadow; a point light owns the six that follow, one a face.
    std::vector<std::int32_t> firstView;
    // For each light with a shadow, how far its views reach.
    std::vector<float> far;
};

// The brightest and nearest lights first, among those that cast shadows and that the camera sees: a light that fills the
// screen deserves its tile more than one that lights a corner far away. A light whose views do not all fit keeps its light
// and loses its shadow. `clipCorrection` turns the projection into the clip space of the API, which Vulkan points Y down in.
[[nodiscard]] DEVEX_API ShadowAssignment assignShadowViews(std::span<const RenderLight> lights, const Frustum& camera,
                                                           math::Vec3 cameraPosition, const ShadowAtlasLayout& atlas,
                                                           const math::Mat4& clipCorrection);

} // namespace devex::render
