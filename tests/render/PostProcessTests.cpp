#include <devex/render/PostProcess.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cstdint>

using Catch::Matchers::WithinAbs;
using devex::math::Vec3;
using devex::render::PostOverride;
using devex::render::RenderCamera;
using devex::render::RenderPostVolume;

namespace {

// A camera standing at a point, looking down -Z.
[[nodiscard]] RenderCamera cameraAt(Vec3 position)
{
    RenderCamera camera;
    camera.view = devex::math::inverse(devex::math::translate(devex::math::Mat4{1.0f}, position));
    return camera;
}

[[nodiscard]] std::uint32_t bits(PostOverride what)
{
    return static_cast<std::uint32_t>(what);
}

} // namespace

TEST_CASE("A volume gives its weight inside its box, less over its blend distance, and nothing beyond", "[render][post]")
{
    // A box of 4 m on a side at x = 10, scaled twice: 8 m on a side in the world.
    RenderPostVolume volume{.worldToBox = devex::math::inverse(devex::math::scale(
                                devex::math::translate(devex::math::Mat4{1.0f}, {10.0f, 0.0f, 0.0f}), Vec3{2.0f})),
                            .halfSize = Vec3{2.0f},
                            .scale = 2.0f,
                            .blendDistance = 2.0f,
                            .weight = 0.8f};
    CHECK_THAT(devex::render::postVolumeInfluence(volume, {10.0f, 0.0f, 0.0f}), WithinAbs(0.8, 1e-5));
    CHECK_THAT(devex::render::postVolumeInfluence(volume, {13.9f, 3.9f, 0.0f}), WithinAbs(0.8, 1e-5));
    // 1 m out of a face of the box, half the way to nothing.
    CHECK_THAT(devex::render::postVolumeInfluence(volume, {15.0f, 0.0f, 0.0f}), WithinAbs(0.4, 1e-4));
    CHECK(devex::render::postVolumeInfluence(volume, {16.5f, 0.0f, 0.0f}) == 0.0f);

    // Without a blend distance, the box has a hard edge; a global volume is everywhere.
    volume.blendDistance = 0.0f;
    CHECK(devex::render::postVolumeInfluence(volume, {14.5f, 0.0f, 0.0f}) == 0.0f);
    volume.global = true;
    CHECK_THAT(devex::render::postVolumeInfluence(volume, {-100.0f, 50.0f, 7.0f}), WithinAbs(0.8, 1e-5));
}

TEST_CASE("The volumes around the camera move what they override towards their settings", "[render][post]")
{
    RenderCamera camera = cameraAt({0.0f, 0.0f, 0.0f});
    camera.saturation = 1.0f;
    camera.bloom = 0.04f;

    // A global volume that greys the image and leaves the bloom alone, at half its weight.
    RenderPostVolume grey{.global = true, .weight = 0.5f, .overrides = bits(PostOverride::Saturation)};
    grey.settings.saturation = 0.0f;
    grey.settings.bloom = 1.0f;
    // A box the camera stands in, of a higher priority, that warms the image completely.
    RenderPostVolume warm{.worldToBox = devex::math::Mat4{1.0f},
                          .halfSize = Vec3{5.0f},
                          .scale = 1.0f,
                          .priority = 1,
                          .overrides = bits(PostOverride::WhiteBalance) | bits(PostOverride::Saturation)};
    warm.settings.temperature = 0.6f;
    warm.settings.saturation = 1.5f;

    const std::array volumes{grey, warm};
    const RenderCamera blended = devex::render::blendPostVolumes(camera, volumes);
    CHECK(blended.bloom == 0.04f);
    CHECK_THAT(blended.temperature, WithinAbs(0.6, 1e-5));
    // Halfway to grey, then all the way to the box's.
    CHECK_THAT(blended.saturation, WithinAbs(1.5, 1e-5));

    // Out of the box, only the global volume is left.
    const RenderCamera outside = devex::render::blendPostVolumes(cameraAt({20.0f, 0.0f, 0.0f}), volumes);
    CHECK_THAT(outside.saturation, WithinAbs(0.5, 1e-5));
    CHECK(outside.temperature == 0.0f);
}

TEST_CASE("A volume's lens comes in whole, and the editor's camera keeps a sharp, still image", "[render][post]")
{
    RenderCamera camera = cameraAt({0.0f, 0.0f, 0.0f});
    camera.focusDistance = 100.0f;
    RenderPostVolume lens{.global = true, .weight = 0.25f, .overrides = bits(PostOverride::DepthOfField) | bits(PostOverride::MotionBlur)};
    lens.settings.depthOfField = 1.0f;
    lens.settings.focusDistance = 3.0f;
    lens.settings.aperture = 1.8f;
    lens.settings.motionBlur = 1.0f;

    // The focus is the volume's from its first quarter on, not a quarter of the way from the camera's.
    const std::array volumes{lens};
    const RenderCamera blended = devex::render::blendPostVolumes(camera, volumes);
    CHECK_THAT(blended.depthOfField, WithinAbs(0.25, 1e-5));
    CHECK(blended.focusDistance == 3.0f);
    CHECK(blended.aperture == 1.8f);
    CHECK_THAT(blended.motionBlur, WithinAbs(0.25, 1e-5));

    camera.lensEffects = false;
    const RenderCamera editing = devex::render::blendPostVolumes(camera, volumes);
    CHECK(editing.depthOfField == 0.0f);
    CHECK(editing.motionBlur == 0.0f);
}

TEST_CASE("The white balance leaves white alone at rest, and warms or cools with the temperature", "[render][post]")
{
    const Vec3 rest = devex::render::whiteBalanceFactors(0.0f, 0.0f);
    CHECK_THAT(rest.x, WithinAbs(1.0, 2e-3));
    CHECK_THAT(rest.y, WithinAbs(1.0, 2e-3));
    CHECK_THAT(rest.z, WithinAbs(1.0, 2e-3));

    // Warmer: more of the long cones, less of the short ones; cooler, the other way round.
    const Vec3 warm = devex::render::whiteBalanceFactors(0.5f, 0.0f);
    CHECK(warm.x > warm.z);
    const Vec3 cool = devex::render::whiteBalanceFactors(-0.5f, 0.0f);
    CHECK(cool.z > cool.x);
    // A green tint lifts the medium cones over the others.
    const Vec3 green = devex::render::whiteBalanceFactors(0.0f, -0.5f);
    const Vec3 magenta = devex::render::whiteBalanceFactors(0.0f, 0.5f);
    CHECK(green.y / green.x > magenta.y / magenta.x);
}
