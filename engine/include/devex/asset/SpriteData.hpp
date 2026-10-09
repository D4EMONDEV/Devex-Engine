#pragma once

#include <devex/core/Export.hpp>

#include <devex/asset/AssetId.hpp>
#include <devex/core/Error.hpp>
#include <devex/math/Math.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Sprites: rectangles of a texture drawn flat in the world, which a texture cuts itself into as it
// imports, and the animations that show them one after the other.
namespace devex::asset {

inline constexpr std::string_view spriteFramesExtension = ".dvxframes";

// A rectangle of a texture, as large in the world as its pixels per unit make it.
struct DEVEX_API SpriteData
{
    AssetId texture;
    // The pixels it covers, from the top-left corner of the texture.
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // The size of the texture, which turns the pixels into texture coordinates.
    std::uint32_t textureWidth = 0;
    std::uint32_t textureHeight = 0;
    // How many pixels make a meter.
    float pixelsPerUnit = 100.0f;
    // Where its entity stands on it, from its bottom-left corner (0, 0) to its top-right one (1, 1).
    math::Vec2 pivot{0.5f, 0.5f};
    // The pixels a sliced sprite keeps at their size at each side: left, bottom, right and top.
    math::Vec4 border{0.0f};
    // The normal map of its texture, laid out as it, which the 2D lights read; invalid for none.
    AssetId normalTexture;

    // Its width and height in meters.
    [[nodiscard]] math::Vec2 size() const noexcept;
    // The texture coordinates of its top-left corner, then of its bottom-right corner.
    [[nodiscard]] math::Vec4 uvRect() const noexcept;

    bool operator==(const SpriteData&) const = default;
};

// A rectangle inside its texture, a positive number of pixels per unit, borders that fit.
[[nodiscard]] DEVEX_API core::Result<void> validate(const SpriteData& sprite);

// An animation of sprites: the frames it shows one after the other.
// A frame of a sprite animation that game code hears of when the animator comes to it: a step, a
// blow that lands.
struct DEVEX_API SpriteAnimationEvent
{
    // From 0.
    std::uint32_t frame = 0;
    std::string name;

    bool operator==(const SpriteAnimationEvent&) const = default;
};

struct DEVEX_API SpriteAnimationData
{
    std::string name;
    // Frames per second.
    float fps = 10.0f;
    // Starts again after its last frame; otherwise stays on it.
    bool loop = true;
    std::vector<AssetId> frames;
    // In the order of their frames.
    std::vector<SpriteAnimationEvent> events;

    bool operator==(const SpriteAnimationData&) const = default;
};

// The animations of a character, found by name: "idle", "run", "jump"...
struct DEVEX_API SpriteFramesData
{
    std::vector<SpriteAnimationData> animations;

    // Null when no animation has the name.
    [[nodiscard]] const SpriteAnimationData* find(std::string_view name) const noexcept;

    bool operator==(const SpriteFramesData&) const = default;
};

// Names that are not empty and differ, positive frame rates, named events on frames that exist.
[[nodiscard]] DEVEX_API core::Result<void> validate(const SpriteFramesData& frames);

} // namespace devex::asset
