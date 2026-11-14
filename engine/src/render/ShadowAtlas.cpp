#include <devex/render/ShadowAtlas.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <optional>
#include <utility>

namespace devex::render {

namespace {

// Where each face of a cube around a point light looks, in the order the shaders expect.
struct CubeFace
{
    math::Vec3 direction;
    math::Vec3 up;
};

constexpr std::array<CubeFace, 6> cubeFaces{{
    {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
    {{-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
    {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
    {{0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
    {{0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
    {{0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}},
}};

} // namespace

ShadowAssignment assignShadowViews(std::span<const RenderLight> lights, const Frustum& camera, math::Vec3 cameraPosition,
                                   const ShadowAtlasLayout& atlas, const math::Mat4& clipCorrection)
{
    ShadowAssignment assigned;
    assigned.firstView.assign(lights.size(), -1);
    assigned.far.assign(lights.size(), 0.0f);
    const std::uint32_t cells = atlas.cells();
    if (cells == 0)
    {
        return assigned;
    }

    std::vector<std::pair<float, std::size_t>> ranked;
    for (std::size_t index = 0; index < lights.size(); ++index)
    {
        const RenderLight& light = lights[index];
        if (!light.castShadows)
        {
            continue;
        }
        const math::Aabb reach{light.position - math::Vec3{light.range}, light.position + math::Vec3{light.range}};
        if (!camera.intersects(reach))
        {
            continue;
        }
        const float power = std::max({light.intensity.r, light.intensity.g, light.intensity.b});
        const math::Vec3 toCamera = light.position - cameraPosition;
        const float distanceSquared = std::max(math::dot(toCamera, toCamera), 1.0f);
        ranked.emplace_back(power / distanceSquared, index);
    }
    std::ranges::sort(ranked, std::greater{}, &std::pair<float, std::size_t>::first);

    // A tile is one cell, or a block of two by two.
    std::vector<bool> taken(std::size_t{cells} * cells, false);
    const auto allocate = [&](std::uint32_t span) -> std::optional<std::pair<std::uint32_t, std::uint32_t>> {
        for (std::uint32_t y = 0; y + span <= cells; y += span)
        {
            for (std::uint32_t x = 0; x + span <= cells; x += span)
            {
                bool free = true;
                for (std::uint32_t dy = 0; dy < span && free; ++dy)
                {
                    for (std::uint32_t dx = 0; dx < span; ++dx)
                    {
                        free = free && !taken[(y + dy) * cells + x + dx];
                    }
                }
                if (!free)
                {
                    continue;
                }
                for (std::uint32_t dy = 0; dy < span; ++dy)
                {
                    for (std::uint32_t dx = 0; dx < span; ++dx)
                    {
                        taken[(y + dy) * cells + x + dx] = true;
                    }
                }
                return std::pair{x, y};
            }
        }
        return std::nullopt;
    };

    constexpr float nearPlane = 0.05f;
    const float atlasSize = static_cast<float>(atlas.size);
    for (std::size_t rank = 0; rank < ranked.size(); ++rank)
    {
        const std::size_t index = ranked[rank].second;
        const RenderLight& light = lights[index];
        const bool point = light.type == LightType::Point;
        const std::uint32_t span = rank < atlas.largeLights ? 2 : 1;
        const std::uint32_t views = point ? 6 : 1;

        // Every view of a light is placed, or none of it is.
        std::vector<std::pair<std::uint32_t, std::uint32_t>> placed;
        for (std::uint32_t view = 0; view < views; ++view)
        {
            const std::optional<std::pair<std::uint32_t, std::uint32_t>> cell = allocate(span);
            if (!cell)
            {
                break;
            }
            placed.push_back(*cell);
        }
        if (placed.size() < views)
        {
            // What it took back stays taken, which only costs the tiles of a light that did not fit.
            continue;
        }

        assigned.firstView[index] = static_cast<std::int32_t>(assigned.views.size());
        assigned.far[index] = std::max(light.range, 1e-3f);
        const float tileSize = static_cast<float>(span * atlas.cell);
        for (std::uint32_t view = 0; view < views; ++view)
        {
            const math::Vec3 direction =
                point ? cubeFaces[view].direction
                      : (math::length(light.direction) > 0.0f ? math::normalize(light.direction) : math::Vec3{0.0f, 0.0f, -1.0f});
            const math::Vec3 up =
                point ? cubeFaces[view].up : (std::abs(direction.y) > 0.99f ? math::Vec3{0.0f, 0.0f, 1.0f} : math::Vec3{0.0f, 1.0f, 0.0f});
            // A spot covers its whole cone, a point a quarter turn on each face.
            const float fov = point ? math::radians(90.0f) : std::clamp(light.outerAngle * 2.0f, math::radians(5.0f), math::radians(175.0f));
            const math::Mat4 look = math::lookAt(light.position, light.position + direction, up);
            const math::Mat4 projection = math::perspective(fov, 1.0f, nearPlane, std::max(light.range, nearPlane * 2.0f));
            const auto [cellX, cellY] = placed[view];
            assigned.views.push_back({
                .viewProjection = clipCorrection * projection * look,
                .tile = {static_cast<float>(cellX * atlas.cell) / atlasSize, static_cast<float>(cellY * atlas.cell) / atlasSize,
                         tileSize / atlasSize, tileSize / atlasSize},
            });
        }
    }
    return assigned;
}

} // namespace devex::render
