#pragma once

#include <devex/asset/AssetId.hpp>
#include <devex/math/Math.hpp>
#include <devex/reflection/Reflection.hpp>

#include <array>
#include <cstdint>
#include <string_view>

// Components provided by the engine. Game code defines its own the same way: a plain struct and
// its reflection, then registerComponent<T>() to make it saveable.
namespace devex::scene {

// Position, rotation and scale relative to the parent entity.
struct Transform
{
    math::Vec3 position{0.0f};
    math::Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    math::Vec3 scale{1.0f};

    [[nodiscard]] math::Mat4 matrix() const noexcept
    {
        return math::composeTrs({position, rotation, scale});
    }
};
DEVEX_DECLARE_REFLECTION(Transform);

// Transform relative to the world, computed by Scene::updateTransforms. It is never saved.
struct WorldTransform
{
    math::Mat4 matrix{1.0f};
};

// Draws a mesh asset. Each submesh uses its own material unless `material` replaces them all.
struct MeshRenderer
{
    asset::AssetId mesh;
    asset::AssetId material;
};
DEVEX_DECLARE_REFLECTION(MeshRenderer);

// How the jagged edges of the image are smoothed.
enum class Antialiasing : std::uint8_t
{
    None,
    // Every frame is drawn a fraction of a pixel aside and mixed with the previous ones, which
    // smooths the edges without drawing anything twice.
    Temporal,
};

// Maps high dynamic range scene colors to the display.
enum class Tonemapper : std::uint8_t
{
    // Desaturates bright saturated colors towards white without shifting their hue.
    AgX,
    // Keeps base colors faithful under normal lighting.
    PbrNeutral,
    // Contrasted filmic look, which shifts saturated hues.
    Aces,
    // Clamps: for debugging.
    None,
};

// Perspective camera looking along -Z of its entity. The renderer uses the first primary camera.
// Exposure follows photographic units: an EV100 of 15 suits direct sunlight, 7 a lit interior.
struct Camera
{
    float verticalFov = math::radians(60.0f);
    float nearPlane = 0.1f;
    bool primary = true;
    // Adapts the exposure to the average luminance of the image, between the limits below.
    bool autoExposure = true;
    // The exposure used when automatic exposure is off, and the starting point when it is on.
    float ev100 = 14.0f;
    // Added to the exposure, in stops: positive values brighten the image.
    float exposureCompensation = 0.0f;
    float minEv100 = -2.0f;
    float maxEv100 = 18.0f;
    // How fast automatic exposure converges, as a rate per second.
    float adaptationSpeed = 1.5f;
    Tonemapper tonemapper = Tonemapper::AgX;
    Antialiasing antialiasing = Antialiasing::Temporal;

    // Darkens the ambient light where a surface sits in a corner; 0 turns it off.
    float ambientOcclusion = 1.0f;
    // How far, in meters, a surface looks around itself for what hides the sky.
    float ambientOcclusionRadius = 0.5f;
    // How much of the halo of very bright things is added back to the image; 0 turns it off.
    float bloom = 0.05f;
    // The brightness, after exposure, a pixel must pass before it glows.
    float bloomThreshold = 1.0f;
    // Darkens the corners of the image, from 0 to 1.
    float vignette = 0.0f;
    // Adds noise to the image, as film does.
    float grain = 0.0f;
    // Pulls red and blue apart towards the edges, as a lens does.
    float chromaticAberration = 0.0f;
    // A table of colors, kept as a strip of squares, applied to the image once it is tonemapped.
    // The texture must be imported without the sRGB encoding.
    asset::AssetId colorTable;
};
DEVEX_DECLARE_REFLECTION(Camera);

// Sunlight travelling along -Z of its entity.
struct DirectionalLight
{
    // Linear color, multiplied by the color of the temperature.
    math::Vec3 color{1.0f};
    // In kelvins; 6500 is neutral white, lower is warmer, higher is cooler.
    float temperature = 6500.0f;
    // In lux: about 100000 for direct sunlight, 400 at sunrise.
    float illuminance = 100000.0f;
    // Cascaded shadow maps; only the first directional light casts shadows.
    bool castShadows = true;
    // Distance from the camera, in meters, beyond which nothing is shadowed.
    float shadowDistance = 80.0f;
};
DEVEX_DECLARE_REFLECTION(DirectionalLight);

// Light emitted in every direction from the position of its entity.
struct PointLight
{
    math::Vec3 color{1.0f};
    float temperature = 6500.0f;
    // Luminous power in lumens: about 800 for a household bulb.
    float intensity = 800.0f;
    // Distance in meters at which the light fades out entirely.
    float range = 10.0f;
    // Casts a shadow all around it, which costs six views of the shadow atlas. Lights that do not
    // fit in the atlas keep their light and lose their shadow.
    bool castShadows = false;
};
DEVEX_DECLARE_REFLECTION(PointLight);

// Light emitted in a cone along -Z of its entity.
struct SpotLight
{
    math::Vec3 color{1.0f};
    float temperature = 6500.0f;
    // Luminous power in lumens, as if the light shone in every direction.
    float intensity = 800.0f;
    float range = 10.0f;
    // Half angles of the fully lit cone and of the cone where the light ends.
    float innerAngle = math::radians(20.0f);
    float outerAngle = math::radians(30.0f);
    // Casts a shadow inside its cone, which costs one view of the shadow atlas.
    bool castShadows = false;
};
DEVEX_DECLARE_REFLECTION(SpotLight);

// The sky around the scene, which also lights it and appears in reflections. The renderer uses
// the first environment.
struct Environment
{
    // An equirectangular high dynamic range texture; without one, the sky is a uniform color.
    asset::AssetId sky;
    // Linear color of the uniform sky, and tint of the texture.
    math::Vec3 color{1.0f};
    // Luminance of a texel of value 1, in nits: about 10000 for a daylight sky.
    float intensity = 8000.0f;
    // Rotation of the sky around the vertical axis.
    float rotation = 0.0f;
};
DEVEX_DECLARE_REFLECTION(Environment);

} // namespace devex::scene

template <>
struct devex::reflection::EnumNames<devex::scene::Tonemapper>
{
    static constexpr std::array<std::string_view, 4> names{"agx", "pbr_neutral", "aces", "none"};
};

template <>
struct devex::reflection::EnumNames<devex::scene::Antialiasing>
{
    static constexpr std::array<std::string_view, 2> names{"none", "temporal"};
};
