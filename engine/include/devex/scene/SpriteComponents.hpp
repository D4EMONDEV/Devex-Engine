#pragma once

#include <devex/core/Export.hpp>

#include <devex/asset/AssetId.hpp>
#include <devex/math/Math.hpp>
#include <devex/reflection/Reflection.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace devex::scene {

// How a sprite fills its rectangle.
enum class SpriteDrawMode : std::uint8_t
{
    // At the size of its pixels.
    Simple,
    // At the size of the component, its borders kept and its middle stretched: panels, platforms.
    Sliced,
    // At the size of the component, its borders kept and its middle repeated: floors, walls.
    Tiled,
};

// How a sprite covers what is behind it.
enum class SpriteBlend : std::uint8_t
{
    // By its alpha.
    Alpha,
    // Adding its light: glows, lights, magic.
    Additive,
};

// Draws a sprite flat in the XY plane of its entity, facing +Z, towards a camera looking down -Z.
// Sprites draw after the opaque surfaces, over each other by sorting layer, then by order, then
// from the farthest to the nearest.
struct DEVEX_API SpriteRenderer
{
    asset::AssetId sprite;
    // Multiplies the sprite: its tint and its opacity.
    math::Vec4 color{1.0f};
    // Colors above 1 shine into the bloom; lit sprites ignore it.
    float intensity = 1.0f;
    // Mirrors the sprite around its pivot.
    bool flipX = false;
    bool flipY = false;
    SpriteDrawMode drawMode = SpriteDrawMode::Simple;
    // Sliced and tiled sprites: the rectangle they fill, in meters.
    math::Vec2 size{1.0f};
    // The sorting layer of the project it draws in; an empty or unknown name is "Default".
    std::string sortingLayer = "Default";
    // Higher orders draw over lower ones within a layer.
    std::int32_t order = 0;
    SpriteBlend blend = SpriteBlend::Alpha;
    // Lit as a matte surface by the sun, the sky and the lights; otherwise shows its colors as they
    // are, whatever the exposure.
    bool lit = false;
    // Out of reach of the 2D lights and of the CanvasModulate: an interface in the world, a glow.
    bool unshaded = false;
    // The 2D lights whose item mask shares a bit with it shine on it.
    std::uint32_t lightMask = 1;
    // A material whose canvas_item shader draws the sprite; none draws it as it is.
    asset::AssetId material;
};
DEVEX_DECLARE_ENGINE_REFLECTION(SpriteRenderer);

// Shows the frames of an animation of sprite frames on the SpriteRenderer of its entity, one after the
// other while the game plays. It chooses what the renderer shows, in the editor too: its frame poses
// the sprite.
struct DEVEX_API SpriteAnimator
{
    asset::AssetId frames;
    // The animation it plays, by name; empty plays the first one. Naming another one starts it from
    // its first frame.
    std::string animation;
    // Multiplies the frame rate of the animation; negative plays it backwards.
    float speed = 1.0f;
    // Moves from frame to frame. An animation that does not loop stops on its last frame, and turns
    // this off.
    bool playing = true;
    // The frame shown, from 0.
    std::int32_t frame = 0;

    // What the animation last played, and the time spent on the frame, in seconds. Never saved.
    std::string playedAnimation;
    float frameTime = 0.0f;
};
DEVEX_DECLARE_ENGINE_REFLECTION(SpriteAnimator);

} // namespace devex::scene

template <>
struct devex::reflection::EnumNames<devex::scene::SpriteDrawMode>
{
    static constexpr std::array<std::string_view, 3> names{"simple", "sliced", "tiled"};
};

template <>
struct devex::reflection::EnumNames<devex::scene::SpriteBlend>
{
    static constexpr std::array<std::string_view, 2> names{"alpha", "additive"};
};
