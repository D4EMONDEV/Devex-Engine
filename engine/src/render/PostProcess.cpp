#include <devex/render/PostProcess.hpp>

#include <algorithm>
#include <cstdint>

namespace devex::render {

namespace {

// The cones of the eye seeing a colour of chromaticity x, y and luminance 1.
[[nodiscard]] math::Vec3 lmsOfChromaticity(float x, float y) noexcept
{
    const float X = x / y;
    const float Z = (1.0f - x - y) / y;
    return math::Vec3{0.7328f * X + 0.4296f - 0.1624f * Z, -0.7036f * X + 1.6975f + 0.0061f * Z, 0.0030f * X + 0.0136f + 0.9834f * Z};
}

} // namespace

float postVolumeInfluence(const RenderPostVolume& volume, math::Vec3 point) noexcept
{
    if (volume.global)
    {
        return volume.weight;
    }
    const math::Vec3 local = math::Vec3(volume.worldToBox * math::Vec4(point, 1.0f));
    const math::Vec3 outside = math::max(math::abs(local) - volume.halfSize, math::Vec3{0.0f});
    const float distance = math::length(outside) * volume.scale;
    if (distance <= 0.0f)
    {
        return volume.weight;
    }
    if (volume.blendDistance <= 0.0f)
    {
        return 0.0f;
    }
    return volume.weight * std::clamp(1.0f - distance / volume.blendDistance, 0.0f, 1.0f);
}

RenderCamera blendPostVolumes(const RenderCamera& camera, std::span<const RenderPostVolume> volumes) noexcept
{
    RenderCamera result = camera;
    const math::Vec3 point = math::Vec3(math::inverse(camera.view)[3]);
    for (const RenderPostVolume& volume : volumes)
    {
        const float amount = postVolumeInfluence(volume, point);
        if (amount <= 0.0f)
        {
            continue;
        }
        const auto overrides = [&volume](PostOverride what) { return (volume.overrides & static_cast<std::uint32_t>(what)) != 0; };
        const auto blend = [amount](auto& value, auto target) { value += (target - value) * amount; };
        const RenderCamera& settings = volume.settings;
        if (overrides(PostOverride::Exposure))
        {
            blend(result.exposureCompensation, settings.exposureCompensation);
        }
        if (overrides(PostOverride::Bloom))
        {
            blend(result.bloom, settings.bloom);
            blend(result.bloomThreshold, settings.bloomThreshold);
        }
        if (overrides(PostOverride::Vignette))
        {
            blend(result.vignette, settings.vignette);
        }
        if (overrides(PostOverride::Grain))
        {
            blend(result.grain, settings.grain);
        }
        if (overrides(PostOverride::ChromaticAberration))
        {
            blend(result.chromaticAberration, settings.chromaticAberration);
        }
        if (overrides(PostOverride::AmbientOcclusion))
        {
            blend(result.ambientOcclusion, settings.ambientOcclusion);
        }
        if (overrides(PostOverride::DepthOfField))
        {
            // Without depth of field so far, the lens is the volume's from the start: the focus does
            // not slide in from the camera's unused one.
            if (result.depthOfField <= 0.0f)
            {
                result.focusDistance = settings.focusDistance;
                result.aperture = settings.aperture;
                result.autoFocus = settings.autoFocus;
            }
            else
            {
                blend(result.focusDistance, settings.focusDistance);
                blend(result.aperture, settings.aperture);
                result.autoFocus = amount >= 0.5f ? settings.autoFocus : result.autoFocus;
            }
            blend(result.depthOfField, settings.depthOfField);
        }
        if (overrides(PostOverride::MotionBlur))
        {
            blend(result.motionBlur, settings.motionBlur);
        }
        if (overrides(PostOverride::Saturation))
        {
            blend(result.saturation, settings.saturation);
        }
        if (overrides(PostOverride::Contrast))
        {
            blend(result.contrast, settings.contrast);
        }
        if (overrides(PostOverride::ColorFilter))
        {
            blend(result.colorFilter, settings.colorFilter);
        }
        if (overrides(PostOverride::WhiteBalance))
        {
            blend(result.temperature, settings.temperature);
            blend(result.tint, settings.tint);
        }
    }
    if (!result.lensEffects)
    {
        result.depthOfField = 0.0f;
        result.motionBlur = 0.0f;
    }
    return result;
}

math::Vec3 whiteBalanceFactors(float temperature, float tint) noexcept
{
    // The white of reference moved along the daylight locus by the temperature, and across it by the
    // tint, from that of D65; the image is then balanced back to D65 from it.
    const float t1 = std::clamp(temperature, -1.0f, 1.0f) * 100.0f / 65.0f;
    const float t2 = std::clamp(tint, -1.0f, 1.0f) * 100.0f / 65.0f;
    const float x = 0.31271f - t1 * (t1 < 0.0f ? 0.1f : 0.05f);
    const float y = 2.87f * x - 3.0f * x * x - 0.27509507f + t2 * 0.05f;
    const math::Vec3 d65{0.949237f, 1.03542f, 1.08728f};
    return d65 / lmsOfChromaticity(x, y);
}

} // namespace devex::render
