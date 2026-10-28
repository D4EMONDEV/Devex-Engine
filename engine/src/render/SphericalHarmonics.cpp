#include <devex/render/CubeMaps.hpp>
#include <devex/render/SphericalHarmonics.hpp>

#include <cmath>
#include <numbers>

namespace devex::render {
namespace {

// The real basis of the first nine harmonics at a direction of unit length.
[[nodiscard]] std::array<float, 9> basis(math::Vec3 d) noexcept
{
    return {0.282095f,
            0.488603f * d.y,
            0.488603f * d.z,
            0.488603f * d.x,
            1.092548f * d.x * d.y,
            1.092548f * d.y * d.z,
            0.315392f * (3.0f * d.z * d.z - 1.0f),
            1.092548f * d.x * d.z,
            0.546274f * (d.x * d.x - d.y * d.y)};
}

} // namespace

IrradianceHarmonics projectIrradiance(std::span<const float> faces, std::uint32_t size) noexcept
{
    IrradianceHarmonics radiance{};
    if (size == 0 || faces.size() < std::size_t{size} * size * 18)
    {
        return radiance;
    }
    double total = 0.0;
    for (std::uint32_t face = 0; face < 6; ++face)
    {
        for (std::uint32_t y = 0; y < size; ++y)
        {
            for (std::uint32_t x = 0; x < size; ++x)
            {
                // The solid angle the pixel covers, smaller towards the corners of the face.
                const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
                const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
                const float solidAngle =
                    4.0f / (static_cast<float>(size * size) * std::pow(1.0f + u * u + v * v, 1.5f));
                const std::size_t pixel = ((std::size_t{face} * size + y) * size + x) * 3;
                const math::Vec3 light{faces[pixel], faces[pixel + 1], faces[pixel + 2]};
                const std::array<float, 9> weights = basis(cubeFaceDirection(face, x, y, size));
                for (std::size_t index = 0; index < 9; ++index)
                {
                    radiance[index] += light * (weights[index] * solidAngle);
                }
                total += solidAngle;
            }
        }
    }
    // The pixels cover the sphere a little less or more than 4 pi; the cosine lobe convolves the light,
    // band by band, and pi divides it.
    const float correction = static_cast<float>(4.0 * std::numbers::pi / total);
    constexpr std::array<float, 3> bands{1.0f, 2.0f / 3.0f, 1.0f / 4.0f};
    for (std::size_t index = 0; index < 9; ++index)
    {
        radiance[index] *= correction * bands[index == 0 ? 0 : index < 4 ? 1 : 2];
    }
    return radiance;
}

math::Vec3 evaluateIrradiance(const IrradianceHarmonics& harmonics, math::Vec3 normal) noexcept
{
    const float length = math::length(normal);
    const std::array<float, 9> weights = basis(length > 0.0f ? normal / length : math::Vec3{0.0f, 1.0f, 0.0f});
    math::Vec3 light{0.0f};
    for (std::size_t index = 0; index < 9; ++index)
    {
        light += harmonics[index] * weights[index];
    }
    return math::max(light, math::Vec3{0.0f});
}

} // namespace devex::render
