#include <devex/render/Atmosphere.hpp>

#include <algorithm>
#include <cmath>

namespace devex::render {

namespace {

// The first distance along a ray to a sphere around the origin; negative when it misses it.
[[nodiscard]] float raySphereNearest(math::Vec3 origin, math::Vec3 direction, float radius) noexcept
{
    const float b = math::dot(origin, direction);
    const float c = math::dot(origin, origin) - radius * radius;
    const float discriminant = b * b - c;
    if (discriminant < 0.0f)
    {
        return -1.0f;
    }
    const float root = std::sqrt(discriminant);
    const float near = -b - root;
    const float far = -b + root;
    return near >= 0.0f ? near : (far >= 0.0f ? far : -1.0f);
}

} // namespace

math::Vec3 atmosphereTransmittance(const RenderAtmosphere& air, float altitude, float cosZenith) noexcept
{
    const float bottom = air.planetRadius;
    const float top = air.planetRadius + air.height;
    // A meter above the ground at least: on it, a ray going up would meet it at once.
    const math::Vec3 origin{0.0f, bottom + std::clamp(altitude, 0.001f, std::max(air.height, 0.002f)), 0.0f};
    const float c = std::clamp(cosZenith, -1.0f, 1.0f);
    const math::Vec3 direction{std::sqrt(std::max(1.0f - c * c, 0.0f)), c, 0.0f};
    if (raySphereNearest(origin, direction, bottom) >= 0.0f)
    {
        return math::Vec3{0.0f};
    }
    const float distance = std::max(raySphereNearest(origin, direction, top), 0.0f);
    // The extinction of the molecules, the aerosols and the ozone, as the shaders sum it.
    constexpr int samples = 40;
    math::Vec3 depth{0.0f};
    for (int index = 0; index < samples; ++index)
    {
        const math::Vec3 position = origin + direction * ((static_cast<float>(index) + 0.5f) / samples * distance);
        const float height = std::max(math::length(position) - bottom, 0.0f);
        const float rayleigh = std::exp(-height / std::max(air.rayleighHeight, 1e-3f));
        const float mie = std::exp(-height / std::max(air.mieHeight, 1e-3f));
        const float ozone = std::clamp(1.0f - std::abs(height - 25.0f) / 15.0f, 0.0f, 1.0f);
        depth += air.rayleighScattering * rayleigh + math::Vec3{(air.mieScattering + air.mieAbsorption) * mie} + air.ozoneAbsorption * ozone;
    }
    depth *= distance / samples;
    return math::Vec3{std::exp(-depth.x), std::exp(-depth.y), std::exp(-depth.z)};
}

} // namespace devex::render
