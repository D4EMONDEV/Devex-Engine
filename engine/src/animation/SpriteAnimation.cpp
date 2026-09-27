#include <devex/animation/SpriteAnimation.hpp>

#include <algorithm>
#include <cmath>

namespace devex::animation {

const asset::SpriteAnimationData* animationOf(const asset::SpriteFramesData& frames,
                                              const scene::SpriteAnimator& animator) noexcept
{
    if (animator.animation.empty())
    {
        return frames.animations.empty() ? nullptr : &frames.animations.front();
    }
    return frames.find(animator.animation);
}

asset::AssetId spriteOf(const asset::SpriteFramesData& frames, const scene::SpriteAnimator& animator) noexcept
{
    const asset::SpriteAnimationData* const animation = animationOf(frames, animator);
    if (animation == nullptr || animation->frames.empty())
    {
        return {};
    }
    const auto last = static_cast<std::int32_t>(animation->frames.size()) - 1;
    return animation->frames[static_cast<std::size_t>(std::clamp(animator.frame, 0, last))];
}

void advance(scene::SpriteAnimator& animator, const asset::SpriteAnimationData& animation, float seconds) noexcept
{
    // Naming another animation starts it over; the first one the animator sees keeps its frame, which
    // the scene may have posed.
    if (animator.playedAnimation != animation.name)
    {
        if (!animator.playedAnimation.empty())
        {
            animator.frame = animator.speed < 0.0f ? static_cast<std::int32_t>(animation.frames.size()) - 1 : 0;
        }
        animator.playedAnimation = animation.name;
        animator.frameTime = 0.0f;
    }
    const auto count = static_cast<std::int32_t>(animation.frames.size());
    if (count == 0)
    {
        return;
    }
    animator.frame = std::clamp(animator.frame, 0, count - 1);
    if (!animator.playing || animator.speed == 0.0f || !(seconds > 0.0f) || !(animation.fps > 0.0f))
    {
        return;
    }
    const float frameLength = 1.0f / animation.fps;
    const std::int32_t step = animator.speed > 0.0f ? 1 : -1;
    animator.frameTime += seconds * std::abs(animator.speed);
    // A long frame, as after a hitch, skips frames rather than looping one at a time.
    const auto steps = static_cast<std::int64_t>(std::floor(animator.frameTime / frameLength));
    animator.frameTime -= static_cast<float>(steps) * frameLength;
    if (steps == 0)
    {
        return;
    }
    const std::int64_t target = animator.frame + step * steps;
    if (animation.loop)
    {
        animator.frame = static_cast<std::int32_t>(((target % count) + count) % count);
        return;
    }
    if (target >= count || target < 0)
    {
        animator.frame = target >= count ? count - 1 : 0;
        animator.playing = false;
        animator.frameTime = 0.0f;
        return;
    }
    animator.frame = static_cast<std::int32_t>(target);
}

void updateSpriteAnimators(scene::Scene& scene, const SpriteFramesSource& frames, float seconds)
{
    for ([[maybe_unused]] auto [entity, animator] : scene.view<scene::SpriteAnimator>())
    {
        const std::shared_ptr<const asset::SpriteFramesData> data =
            animator.frames.isValid() && frames ? frames(animator.frames) : nullptr;
        if (data == nullptr)
        {
            continue;
        }
        if (const asset::SpriteAnimationData* const animation = animationOf(*data, animator))
        {
            advance(animator, *animation, seconds);
        }
    }
}

} // namespace devex::animation
