#pragma once

#include <devex/core/Export.hpp>

#include <devex/math/Math.hpp>
#include <devex/reflection/Reflection.hpp>

#include <cstdint>
#include <vector>

// Light in the 2D plane, as Godot's: lights that shine on the sprites and the tilemaps around them
// in the XY plane of the world, their shadows cast by occluders, and a tint over the whole of it.
// Separate from the lights of the 3D scene, which shine on what is lit as a matte surface.
namespace devex::scene {

// Shines from its entity, fading out to its radius, on the sprites and tilemaps whose light mask
// shares a bit with its item mask. With shadows, the occluders whose mask shares a bit with its
// shadow mask hide it.
struct DEVEX_API PointLight2D
{
    // Linear colour, and how strong it is: 1 adds the colour of the sprite once.
    math::Vec3 color{1.0f, 0.85f, 0.6f};
    float energy = 1.0f;
    // Where it stops, in meters, and how fast it fades towards there: 1 evenly, more sooner.
    float radius = 4.0f;
    float falloff = 1.0f;
    // How far above the plane it stands, in meters: the normal maps of the sprites read it.
    float height = 0.5f;
    std::uint32_t itemMask = 1;
    bool shadows = false;
    std::uint32_t shadowMask = 1;
    // How far the edges of its shadows blur, in meters at a distance of one meter.
    float shadowSoftness = 0.05f;
};
DEVEX_DECLARE_ENGINE_REFLECTION(PointLight2D);

// Light from far away in one direction, as the sun or the moon gives: down the Y axis of its entity,
// turned with it. Its shadows reach as far as `shadowDistance` from the occluders.
struct DEVEX_API DirectionalLight2D
{
    math::Vec3 color{1.0f};
    float energy = 1.0f;
    // How high above the plane the light comes from, from 0 (grazing) to 1 (straight above): the
    // normal maps of the sprites read it.
    float height = 0.5f;
    std::uint32_t itemMask = 1;
    bool shadows = false;
    std::uint32_t shadowMask = 1;
    float shadowSoftness = 0.05f;
    float shadowDistance = 20.0f;
};
DEVEX_DECLARE_ENGINE_REFLECTION(DirectionalLight2D);

// The outline of what hides the 2D lights, in the XY plane of its entity: a closed polygon, or a
// line when it is open. It hides the lights whose shadow mask shares a bit with its mask.
struct DEVEX_API LightOccluder2D
{
    std::vector<math::Vec2> points{{-0.5f, -0.5f}, {0.5f, -0.5f}, {0.5f, 0.5f}, {-0.5f, 0.5f}};
    bool closed = true;
    std::uint32_t mask = 1;
};
DEVEX_DECLARE_ENGINE_REFLECTION(LightOccluder2D);

// Tints every sprite and tilemap of the scene, the first one found: night, a cave. The 2D lights
// add their light over it.
struct DEVEX_API CanvasModulate
{
    math::Vec3 color{0.3f, 0.3f, 0.45f};
};
DEVEX_DECLARE_ENGINE_REFLECTION(CanvasModulate);

} // namespace devex::scene
