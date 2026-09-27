#include <devex/asset/SpriteData.hpp>

#include <algorithm>
#include <cmath>

namespace devex::asset {

math::Vec2 SpriteData::size() const noexcept
{
    const float scale = pixelsPerUnit > 0.0f ? 1.0f / pixelsPerUnit : 0.0f;
    return math::Vec2{static_cast<float>(width), static_cast<float>(height)} * scale;
}

math::Vec4 SpriteData::uvRect() const noexcept
{
    const float textureX = static_cast<float>(std::max(textureWidth, 1u));
    const float textureY = static_cast<float>(std::max(textureHeight, 1u));
    return math::Vec4{static_cast<float>(x) / textureX, static_cast<float>(y) / textureY,
                      static_cast<float>(x + width) / textureX, static_cast<float>(y + height) / textureY};
}

core::Result<void> validate(const SpriteData& sprite)
{
    if (sprite.width == 0 || sprite.height == 0 || sprite.x + sprite.width > sprite.textureWidth ||
        sprite.y + sprite.height > sprite.textureHeight)
    {
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "the sprite {}x{} at ({}, {}) does not fit its texture of {}x{}", sprite.width,
                               sprite.height, sprite.x, sprite.y, sprite.textureWidth, sprite.textureHeight);
    }
    if (!std::isfinite(sprite.pixelsPerUnit) || sprite.pixelsPerUnit <= 0.0f)
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "a sprite needs a positive number of pixels per unit");
    }
    const math::Vec4 border = sprite.border;
    if (!(border.x >= 0.0f && border.y >= 0.0f && border.z >= 0.0f && border.w >= 0.0f) ||
        border.x + border.z > static_cast<float>(sprite.width) || border.y + border.w > static_cast<float>(sprite.height))
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "the borders of the sprite are larger than the sprite");
    }
    if (!std::isfinite(sprite.pivot.x) || !std::isfinite(sprite.pivot.y))
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "the pivot of the sprite is not a number");
    }
    return {};
}

const SpriteAnimationData* SpriteFramesData::find(std::string_view name) const noexcept
{
    const auto found = std::ranges::find(animations, name, &SpriteAnimationData::name);
    return found != animations.end() ? &*found : nullptr;
}

core::Result<void> validate(const SpriteFramesData& frames)
{
    for (std::size_t index = 0; index < frames.animations.size(); ++index)
    {
        const SpriteAnimationData& animation = frames.animations[index];
        if (animation.name.empty())
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "animation {} has no name", index + 1);
        }
        if (!std::isfinite(animation.fps) || animation.fps <= 0.0f)
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "the animation \"{}\" needs a positive frame rate",
                                   animation.name);
        }
        for (std::size_t other = 0; other < index; ++other)
        {
            if (frames.animations[other].name == animation.name)
            {
                return core::makeError(core::ErrorCode::InvalidArgument, "two animations are named \"{}\"",
                                       animation.name);
            }
        }
    }
    return {};
}

} // namespace devex::asset
