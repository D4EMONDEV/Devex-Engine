#include "FramePreparation.hpp"

#include <devex/render/Light2D.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <utility>

namespace devex::render {

void gatherLights2D(const RenderWorld& world, math::Vec2 cameraPosition, const math::Mat4& viewProjection, std::vector<GpuLight2D>& lights,
                    std::vector<float>& shadows)
{
    lights.clear();
    shadows.clear();
    // What the camera sees of the plane of the sprites: lights beyond it light nothing seen, and the shadows of directional
    // lights cover it. A camera looking along the plane keeps a wide square.
    std::pair<math::Vec2, math::Vec2> view{cameraPosition - math::Vec2{100.0f}, cameraPosition + math::Vec2{100.0f}};
    if (const std::optional<std::pair<math::Vec2, math::Vec2>> seen = visiblePlane2D(viewProjection))
    {
        view = *seen;
    }
    std::uint32_t shadowed = 0;
    const auto shadowSpan = [&shadows, &shadowed]() -> std::span<float> {
        ++shadowed;
        shadows.resize(shadows.size() + shadow2DResolution);
        return std::span(shadows).last(shadow2DResolution);
    };
    for (const RenderLight2D& light : world.lights2D)
    {
        if (lights.size() >= maxLights2D)
        {
            break;
        }
        GpuLight2D gpu{.color = math::Vec4(light.color, std::max(light.falloff, 0.01f)), .itemMask = light.itemMask};
        const bool casts = light.shadows && shadowed < maxShadowedLights2D;
        if (!light.directional)
        {
            const float radius = std::max(light.radius, 0.0f);
            if (radius <= 0.0f || light.position.x + radius < view.first.x || light.position.y + radius < view.first.y ||
                light.position.x - radius > view.second.x || light.position.y - radius > view.second.y)
            {
                continue;
            }
            gpu.kind = 0;
            gpu.position = math::Vec4(light.position, std::max(light.height, 0.0f), radius);
            gpu.shadow = math::Vec4(std::max(light.shadowSoftness, 0.0f), 0.0f, 0.0f, 0.0f);
            if (casts)
            {
                gpu.shadowOffset = static_cast<std::int32_t>(shadows.size());
                pointShadow2D(light.position, radius, world.occluders2D, light.shadowMask, shadowSpan());
            }
        }
        else
        {
            const DirectionalShadowFrame2D frame =
                directionalShadowFrame(light.direction, view.first, view.second, casts ? std::max(light.shadowDistance, 0.0f) : 0.0f);
            gpu.kind = 1;
            gpu.position = math::Vec4(frame.origin, std::clamp(light.height, 0.0f, 1.0f), frame.width);
            gpu.direction = math::Vec4(frame.direction, frame.across);
            gpu.shadow = math::Vec4(std::max(light.shadowSoftness, 0.0f), std::max(light.shadowDistance, 0.0f), frame.length, 0.0f);
            if (casts)
            {
                gpu.shadowOffset = static_cast<std::int32_t>(shadows.size());
                directionalShadow2D(frame, world.occluders2D, light.shadowMask, shadowSpan());
            }
        }
        lights.push_back(gpu);
    }
}

math::Mat4 projectionMatrix(const RenderCamera& camera, float aspectRatio, const math::Mat4& clipCorrection) noexcept
{
    if (camera.projection == Projection::Orthographic)
    {
        return clipCorrection * math::orthographicReverseZ(camera.orthographicSize, aspectRatio, camera.nearPlane, camera.farPlane);
    }
    return clipCorrection * math::perspectiveReverseZ(camera.verticalFov, aspectRatio, camera.nearPlane);
}

ViewMatrices prepareView(const RenderCamera& camera, math::Extent2D extent, math::Vec2 jitter, const math::Mat4& clipCorrection)
{
    const float aspectRatio = static_cast<float>(extent.width) / static_cast<float>(std::max(extent.height, 1U));
    const math::Mat4 projection = projectionMatrix(camera, aspectRatio, clipCorrection);
    // Shift clip XY by a fraction of clip W so the offset on the screen does not depend on depth, for both projections.
    // Changing the Z column directly only works for a perspective: in an orthographic view it multiplies the jitter by the
    // distance from the camera.
    const math::Mat4 jittered = math::translate(math::Mat4{1.0f}, math::Vec3{-jitter.x, -jitter.y, 0.0f}) * projection;
    ViewMatrices view;
    view.viewProjection = jittered * camera.view;
    view.unjitteredViewProjection = projection * camera.view;
    view.inverseViewProjection = math::inverse(view.unjitteredViewProjection);
    math::Mat4 rotationOnly = camera.view;
    rotationOnly[3] = math::Vec4{0.0f, 0.0f, 0.0f, 1.0f};
    RenderCamera skyCamera = camera;
    skyCamera.projection = Projection::Perspective;
    skyCamera.verticalFov = camera.projection == Projection::Orthographic ? math::radians(60.0f) : camera.verticalFov;
    view.skyViewProjection = projectionMatrix(skyCamera, aspectRatio, clipCorrection) * rotationOnly;
    view.cameraPosition = math::Vec3(math::inverse(camera.view)[3]);
    return view;
}

std::uint32_t instanceParametersOf(const RenderWorld& world, std::uint32_t first) noexcept
{
    return first < world.instanceParameters.size() ? first : noInstanceParameters;
}

PackedAtmosphere packAtmosphere(const RenderWorld& world, float cameraHeight)
{
    const RenderAtmosphere& air = world.atmosphere;
    const RenderFog& fog = world.fog;
    PackedAtmosphere packed;
    GpuAtmosphere& gpu = packed.gpu;
    gpu.bottomRadius = std::max(air.planetRadius, 1.0f);
    gpu.topRadius = gpu.bottomRadius + std::max(air.height, 1.0f);
    gpu.rayleighHeight = air.rayleighHeight;
    gpu.mieHeight = air.mieHeight;
    gpu.rayleighScattering = air.rayleighScattering;
    gpu.mieScattering = air.mieScattering;
    gpu.ozoneAbsorption = air.ozoneAbsorption;
    gpu.mieAbsorption = air.mieAbsorption;
    gpu.groundAlbedo = air.groundAlbedo;
    gpu.mieAnisotropy = std::clamp(air.mieAnisotropy, -0.99f, 0.99f);
    // The camera stands in the air, a meter above the ground at least; the ground of the scene is the planet's.
    packed.altitude = std::clamp(cameraHeight * 0.001f, 0.001f, gpu.topRadius - gpu.bottomRadius - 0.01f);
    gpu.cameraRadius = gpu.bottomRadius + packed.altitude;
    const float sunLength = math::length(world.sun.direction);
    gpu.sunDirection = sunLength > 0.0f ? -world.sun.direction / sunLength : math::Vec3{0.0f, 1.0f, 0.0f};
    gpu.sunIlluminance = world.sun.illuminance * air.intensity;
    gpu.sunCosRadius = std::cos(std::clamp(air.sunDiskSize, 0.0f, 0.5f) * 0.5f);
    gpu.sunDiskIntensity = air.sunDiskIntensity;
    gpu.aerialScale = std::max(air.aerialPerspective, 0.0f);
    gpu.aerialDistance = 32.0f;
    gpu.fogColor = fog.color;
    gpu.fogDensity = fog.enabled ? std::max(fog.density, 0.0f) : 0.0f;
    gpu.fogHeight = fog.height;
    gpu.fogFalloff = std::max(fog.falloff, 0.01f);
    gpu.fogStart = std::max(fog.startDistance, 0.0f);
    gpu.fogMaxOpacity = std::clamp(fog.maxOpacity, 0.0f, 1.0f);
    gpu.fogSunScattering = std::max(fog.sunScattering, 0.0f);
    gpu.fogSunAnisotropy = std::clamp(fog.sunAnisotropy, -0.99f, 0.99f);
    gpu.fogSkyBlend = std::clamp(fog.skyBlend, 0.0f, 1.0f);
    return packed;
}

void packLights(const RenderWorld& world, std::vector<GpuLight>& lights)
{
    lights.clear();
    for (const RenderLight& light : world.lights)
    {
        GpuLight gpuLight{
            .position = light.position,
            .range = std::max(light.range, 1e-3f),
            .direction = math::length(light.direction) > 0.0f ? math::normalize(light.direction) : math::Vec3{0.0f, 0.0f, -1.0f},
            .intensity = light.intensity,
        };
        if (light.type == LightType::Spot)
        {
            const float cosOuter = std::cos(std::max(light.outerAngle, 1e-3f));
            const float cosInner = std::cos(std::clamp(light.innerAngle, 0.0f, light.outerAngle));
            gpuLight.spotScale = 1.0f / std::max(cosInner - cosOuter, 1e-4f);
            gpuLight.spotOffset = -cosOuter * gpuLight.spotScale;
        }
        lights.push_back(gpuLight);
    }
}

void packParticles(const RenderWorld& world, math::Vec3 camera, std::vector<GpuParticle>& packed, std::vector<GpuTrailPoint>& points)
{
    const std::vector<RenderParticle>& particles = world.particles;
    packed.resize(particles.size());
    std::vector<std::pair<float, std::uint32_t>> order;
    const auto convert = [](const RenderParticle& particle) {
        return GpuParticle{
            .position = particle.position,
            .size = particle.size,
            .color = particle.color,
            .stretch = particle.stretch,
            .rotation = particle.rotation,
            .frame = particle.frame,
        };
    };
    for (const ParticleDraw& draw : world.particleDraws)
    {
        if (draw.ribbons || draw.first >= particles.size())
        {
            continue;
        }
        const std::uint32_t end = std::min<std::uint32_t>(draw.first + draw.count, static_cast<std::uint32_t>(particles.size()));
        if (draw.blend == ParticleBlend::Additive)
        {
            for (std::uint32_t index = draw.first; index < end; ++index)
            {
                packed[index] = convert(particles[index]);
            }
            continue;
        }
        order.clear();
        for (std::uint32_t index = draw.first; index < end; ++index)
        {
            const math::Vec3 toCamera = particles[index].position - camera;
            order.emplace_back(math::dot(toCamera, toCamera), index);
        }
        std::ranges::sort(order, std::greater{}, &std::pair<float, std::uint32_t>::first);
        std::uint32_t slot = draw.first;
        for (const auto& [distance, index] : order)
        {
            static_cast<void>(distance);
            packed[slot++] = convert(particles[index]);
        }
    }

    points.resize(world.trailPoints.size());
    for (std::size_t index = 0; index < world.trailPoints.size(); ++index)
    {
        const RenderTrailPoint& point = world.trailPoints[index];
        points[index] = GpuTrailPoint{
            .position = point.position,
            .width = point.width,
            .color = point.color,
            .direction = point.direction,
            .u = point.u,
        };
    }
}

void packUi(const RenderWorld& world, bool tools, PackedUi& packed)
{
    packed.vertices.clear();
    packed.indices.clear();
    packed.surfaceStarts.clear();
    const auto vertexOf = [](const UiVertex& vertex) { return GpuUiVertex{.position = vertex.position, .uv = vertex.uv, .color = vertex.color}; };
    for (const UiVertex& vertex : world.uiVertices)
    {
        packed.vertices.push_back(vertexOf(vertex));
    }
    packed.indices.assign(world.uiIndices.begin(), world.uiIndices.end());
    const auto append = [&](std::span<const UiVertex> layerVertices, std::span<const std::uint32_t> layerIndices) {
        const auto vertexBase = static_cast<std::uint32_t>(packed.vertices.size());
        const auto first = static_cast<std::uint32_t>(packed.indices.size());
        for (const UiVertex& vertex : layerVertices)
        {
            packed.vertices.push_back(vertexOf(vertex));
        }
        for (const std::uint32_t index : layerIndices)
        {
            packed.indices.push_back(vertexBase + index);
        }
        return first;
    };
    // The surfaces and the layer of the tools are drawn only with the tools.
    if (tools)
    {
        for (const UiSurface& surface : world.uiSurfaces)
        {
            packed.surfaceStarts.push_back(append(surface.vertices, surface.indices));
        }
    }
    packed.toolsStart = tools ? append(world.toolsVertices, world.toolsIndices) : static_cast<std::uint32_t>(packed.indices.size());
}

namespace {

[[nodiscard]] math::Aabb boxOf(const math::Mat4& transform, math::Vec2 low, math::Vec2 high)
{
    math::Aabb bounds;
    for (const float x : {low.x, high.x})
    {
        for (const float y : {low.y, high.y})
        {
            bounds.add(math::Vec3(transform * math::Vec4(x, y, 0.0f, 1.0f)));
        }
    }
    return bounds;
}

} // namespace

void gatherSprites(const RenderWorld& world, const Frustum& camera, const SpriteResources& resources, SpriteFrame& frame)
{
    frame.batches.clear();
    frame.sprites.clear();
    frame.outlined.clear();
    frame.shaders.clear();
    for (std::uint32_t index = 0; index < world.sprites.size(); ++index)
    {
        const RenderSprite& sprite = world.sprites[index];
        // The box of the four corners, flipped or not: the flip mirrors around the pivot.
        const math::Vec2 low = -sprite.pivot * sprite.size;
        const math::Vec2 high = (math::Vec2{1.0f} - sprite.pivot) * sprite.size;
        const math::Vec2 extent = math::max(math::abs(low), math::abs(high));
        if (!camera.intersects(boxOf(sprite.transform, -extent, extent)))
        {
            continue;
        }
        const math::Vec2 middle = (low + high) * 0.5f;
        const math::Vec3 center = math::Vec3(sprite.transform * math::Vec4(middle, 0.0f, 1.0f));
        frame.batches.push_back({.layer = sprite.layer, .order = sprite.order, .distance = resources.sortDistance(center), .source = index, .count = 1});
    }
    // A tilemap sorts as a whole, from the middle of its tiles.
    for (std::uint32_t index = 0; index < world.tilemaps.size(); ++index)
    {
        const RenderTilemap& tilemap = world.tilemaps[index];
        const std::uint32_t end = std::min<std::uint32_t>(tilemap.firstTile + tilemap.tileCount, static_cast<std::uint32_t>(world.tiles.size()));
        if (tilemap.firstTile >= end)
        {
            continue;
        }
        math::IVec2 low{std::numeric_limits<std::int32_t>::max()};
        math::IVec2 high{std::numeric_limits<std::int32_t>::lowest()};
        for (std::uint32_t tile = tilemap.firstTile; tile < end; ++tile)
        {
            low = math::min(low, world.tiles[tile].cell);
            high = math::max(high, world.tiles[tile].cell + math::IVec2{1});
        }
        const math::Vec2 lowCorner = math::Vec2(low) * tilemap.cellSize;
        const math::Vec2 highCorner = math::Vec2(high) * tilemap.cellSize;
        if (!camera.intersects(boxOf(tilemap.transform, lowCorner, highCorner)))
        {
            continue;
        }
        const math::Vec3 center = math::Vec3(tilemap.transform * math::Vec4((lowCorner + highCorner) * 0.5f, 0.0f, 1.0f));
        frame.batches.push_back(
            {.layer = tilemap.layer, .order = tilemap.order, .distance = resources.sortDistance(center), .tilemap = true, .source = index});
    }
    std::ranges::stable_sort(frame.batches, [](const SpriteBatch& a, const SpriteBatch& b) {
        if (a.layer != b.layer)
        {
            return a.layer < b.layer;
        }
        if (a.order != b.order)
        {
            return a.order < b.order;
        }
        return a.distance > b.distance;
    });

    for (SpriteBatch& batch : frame.batches)
    {
        batch.first = static_cast<std::uint32_t>(frame.sprites.size());
        if (!batch.tilemap)
        {
            const RenderSprite& sprite = world.sprites[batch.source];
            std::uint32_t flags = static_cast<std::uint32_t>(sprite.mode) & 3U;
            flags |= (sprite.flipX ? spriteFlipX : 0U) | (sprite.flipY ? spriteFlipY : 0U);
            flags |= (sprite.additive ? spriteAdditive : 0U) | (sprite.lit ? spriteLit : 0U);
            flags |= sprite.unshaded ? spriteUnshaded : 0U;
            GpuSprite& gpu = frame.sprites.emplace_back(GpuSprite{
                .world = sprite.transform,
                .uvRect = sprite.uvRect,
                .color = sprite.color,
                .border = sprite.border,
                .size = sprite.size,
                .pivot = sprite.pivot,
                .naturalSize = sprite.naturalSize,
                .texture = resources.textureSlot(sprite.texture),
                .flags = flags,
                .objectId = sprite.objectId,
                .lightMask = sprite.lightMask,
                .normalTexture = resources.textureSlot(sprite.normalTexture),
                .material = resources.materialIndex(sprite.material),
                .instanceParameters = instanceParametersOf(world, sprite.instanceParameters),
            });
            // A sprite whose texture is still loading waits for it rather than showing a plain rectangle.
            if (sprite.texture.isValid() && gpu.texture == noParticleTexture)
            {
                gpu.color.a = 0.0f;
            }
            frame.outlined.push_back(sprite.outlined ? 1 : 0);
            frame.shaders.push_back(resources.canvasShader(sprite.material));
            continue;
        }
        // The tiles the camera sees, each filling its cell.
        const RenderTilemap& tilemap = world.tilemaps[batch.source];
        const std::uint32_t end = std::min<std::uint32_t>(tilemap.firstTile + tilemap.tileCount, static_cast<std::uint32_t>(world.tiles.size()));
        for (std::uint32_t index = tilemap.firstTile; index < end; ++index)
        {
            const RenderTile& tile = world.tiles[index];
            const math::Vec2 corner = math::Vec2(tile.cell) * tilemap.cellSize;
            if (!camera.intersects(boxOf(tilemap.transform, corner, corner + tilemap.cellSize)))
            {
                continue;
            }
            GpuSprite& gpu = frame.sprites.emplace_back(GpuSprite{
                .world = tilemap.transform * math::translate(math::Mat4{1.0f}, math::Vec3(corner, 0.0f)),
                .uvRect = tile.uvRect,
                .color = tilemap.color,
                .size = tilemap.cellSize,
                .pivot = math::Vec2{0.0f},
                .naturalSize = tilemap.cellSize,
                .texture = resources.textureSlot(tile.texture),
                .flags = (tilemap.lit ? spriteLit : 0U) | (tilemap.unshaded ? spriteUnshaded : 0U),
                .objectId = tilemap.objectId,
                .lightMask = tilemap.lightMask,
                .normalTexture = resources.textureSlot(tile.normalTexture),
                .material = resources.materialIndex(tilemap.material),
                .instanceParameters = instanceParametersOf(world, tilemap.instanceParameters),
            });
            if (tile.texture.isValid() && gpu.texture == noParticleTexture)
            {
                gpu.color.a = 0.0f;
            }
            frame.outlined.push_back(tilemap.outlined ? 1 : 0);
            frame.shaders.push_back(resources.canvasShader(tilemap.material));
        }
        batch.count = static_cast<std::uint32_t>(frame.sprites.size()) - batch.first;
    }
}

} // namespace devex::render
