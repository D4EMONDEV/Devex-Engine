#include <devex/animation/SpriteAnimation.hpp>
#include <devex/render/Photometry.hpp>
#include <devex/runtime/SceneExtraction.hpp>
#include <devex/scene/AnimationComponents.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/SpriteComponents.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace devex::runtime {
namespace {

// A point of a ribbon before it is turned into the points of the renderer.
struct RibbonPoint
{
    math::Vec3 position{0.0f};
    float width = 0.0f;
    math::Vec4 color{1.0f};
};

// Adds a ribbon through the points, from its head, as segments of the frame.
void addRibbon(render::RenderWorld& world, const std::vector<RibbonPoint>& points)
{
    if (points.size() < 2)
    {
        return;
    }
    float length = 0.0f;
    for (std::size_t index = 1; index < points.size(); ++index)
    {
        length += math::distance(points[index - 1].position, points[index].position);
    }
    const auto first = static_cast<std::uint32_t>(world.trailPoints.size());
    float travelled = 0.0f;
    for (std::size_t index = 0; index < points.size(); ++index)
    {
        if (index > 0)
        {
            travelled += math::distance(points[index - 1].position, points[index].position);
        }
        // Along the ribbon at this point, from the point before to the one after.
        const math::Vec3 towardsTail =
            points[std::min(index + 1, points.size() - 1)].position - points[index > 0 ? index - 1 : 0].position;
        const float span = math::length(towardsTail);
        world.trailPoints.push_back({
            .position = points[index].position,
            .width = points[index].width,
            .color = points[index].color,
            .direction = span > 1e-6f ? towardsTail / span : math::Vec3{0.0f, 0.0f, 1.0f},
            .u = length > 0.0f ? travelled / length : 0.0f,
        });
    }
    for (std::uint32_t segment = 0; segment + 1 < points.size(); ++segment)
    {
        world.trailSegments.push_back(first + segment);
    }
}

} // namespace

void extractSprites(scene::Scene& scene, AssetManager& assets, const asset::SortingSettings& sorting,
                    render::RenderWorld& world)
{
    for ([[maybe_unused]] auto [entity, transform, sprite] : scene.view<scene::WorldTransform, scene::SpriteRenderer>())
    {
        asset::AssetId shown = sprite.sprite;
        if (const scene::SpriteAnimator* const animator = scene.tryGet<scene::SpriteAnimator>(entity);
            animator != nullptr && animator->frames.isValid())
        {
            if (const std::shared_ptr<const asset::SpriteFramesData> frames = assets.spriteFrames(animator->frames))
            {
                if (const asset::AssetId frame = animation::spriteOf(*frames, *animator); frame.isValid())
                {
                    shown = frame;
                }
            }
        }
        const std::shared_ptr<const asset::SpriteData> data = shown.isValid() ? assets.sprite(shown) : nullptr;
        if (data == nullptr)
        {
            continue;
        }
        // Drawn once its texture is.
        const render::TextureHandle texture = assets.texture(data->texture);
        if (!texture.isValid())
        {
            continue;
        }
        const math::Vec2 natural = data->size();
        const float metersPerPixel = 1.0f / data->pixelsPerUnit;
        const bool simple = sprite.drawMode == scene::SpriteDrawMode::Simple;
        const float intensity = sprite.lit ? 1.0f : std::max(sprite.intensity, 0.0f);
        world.sprites.push_back({
            .transform = transform.matrix,
            .size = simple ? natural : math::max(sprite.size, math::Vec2{0.0f}),
            .pivot = data->pivot,
            .uvRect = data->uvRect(),
            .naturalSize = natural,
            .border = data->border * metersPerPixel,
            .color = math::Vec4(math::Vec3(sprite.color) * intensity, sprite.color.a),
            .texture = texture,
            .mode = static_cast<render::SpriteMode>(sprite.drawMode),
            .flipX = sprite.flipX,
            .flipY = sprite.flipY,
            .additive = sprite.blend == scene::SpriteBlend::Additive,
            .lit = sprite.lit,
            .layer = sorting.rank(sprite.sortingLayer),
            .order = sprite.order,
            .objectId = entity.index + 1,
        });
    }
}

void extractScene(scene::Scene& scene, AssetManager& assets, render::RenderWorld& world)
{
    for ([[maybe_unused]] auto [entity, transform, camera] :
         scene.view<scene::WorldTransform, scene::Camera>())
    {
        if (camera.primary)
        {
            world.camera = {
                .view = math::inverse(transform.matrix),
                .projection = static_cast<render::Projection>(camera.projection),
                .verticalFov = camera.verticalFov,
                .orthographicSize = std::max(camera.orthographicSize, 1e-4f),
                .nearPlane = camera.nearPlane,
                .farPlane = std::max(camera.farPlane, camera.nearPlane + 1e-3f),
                .autoExposure = camera.autoExposure,
                .ev100 = camera.ev100,
                .exposureCompensation = camera.exposureCompensation,
                .minEv100 = camera.minEv100,
                .maxEv100 = camera.maxEv100,
                .adaptationSpeed = camera.adaptationSpeed,
                .tonemapper = static_cast<render::Tonemapper>(camera.tonemapper),
                .antialiasing = static_cast<render::Antialiasing>(camera.antialiasing),
                .ambientOcclusion = camera.ambientOcclusion,
                .ambientOcclusionRadius = camera.ambientOcclusionRadius,
                .bloom = camera.bloom,
                .bloomThreshold = camera.bloomThreshold,
                .vignette = camera.vignette,
                .grain = camera.grain,
                .chromaticAberration = camera.chromaticAberration,
                .colorTable = camera.colorTable.isValid() ? assets.texture(camera.colorTable)
                                                          : render::TextureHandle{},
            };
            break;
        }
    }

    const auto forward = [](const math::Mat4& matrix) {
        const math::Vec3 direction = math::Mat3(matrix) * math::Vec3{0.0f, 0.0f, -1.0f};
        const float length = math::length(direction);
        return length > 0.0f ? direction / length : math::Vec3{0.0f, 0.0f, -1.0f};
    };

    const auto suns = scene.view<scene::WorldTransform, scene::DirectionalLight>();
    if (const auto first = suns.begin(); first != suns.end())
    {
        [[maybe_unused]] const auto [entity, transform, light] = *first;
        world.sun = {
            .direction = forward(transform.matrix),
            .illuminance = light.color * render::colorFromTemperature(light.temperature) * light.illuminance,
            .castShadows = light.castShadows,
            .shadowDistance = light.shadowDistance,
        };
    }

    for ([[maybe_unused]] auto [entity, transform, light] :
         scene.view<scene::WorldTransform, scene::PointLight>())
    {
        world.lights.push_back({
            .type = render::LightType::Point,
            .position = math::Vec3(transform.matrix[3]),
            .intensity = light.color * render::colorFromTemperature(light.temperature) *
                         render::luminousIntensityFromPower(light.intensity),
            .range = light.range,
            .castShadows = light.castShadows,
        });
    }
    for ([[maybe_unused]] auto [entity, transform, light] :
         scene.view<scene::WorldTransform, scene::SpotLight>())
    {
        world.lights.push_back({
            .type = render::LightType::Spot,
            .position = math::Vec3(transform.matrix[3]),
            .direction = forward(transform.matrix),
            .intensity = light.color * render::colorFromTemperature(light.temperature) *
                         render::luminousIntensityFromPower(light.intensity),
            .range = light.range,
            .innerAngle = light.innerAngle,
            .outerAngle = light.outerAngle,
            .castShadows = light.castShadows,
        });
    }

    const auto environments = scene.view<scene::Environment>();
    if (const auto first = environments.begin(); first != environments.end())
    {
        [[maybe_unused]] const auto [entity, environment] = *first;
        world.environment = {
            .sky = assets.texture(environment.sky),
            .color = environment.color,
            .intensity = environment.intensity,
            .rotation = environment.rotation,
        };
    }

    for ([[maybe_unused]] auto [entity, transform, renderer] :
         scene.view<scene::WorldTransform, scene::MeshRenderer>())
    {
        const LoadedMesh* const mesh = assets.mesh(renderer.mesh);
        if (mesh == nullptr)
        {
            continue;
        }
        const render::MaterialHandle override =
            renderer.material.isValid() ? assets.material(renderer.material)
                                        : render::MaterialHandle{};
        for (std::uint32_t submesh = 0; submesh < mesh->submeshMaterials.size(); ++submesh)
        {
            const asset::AssetId material = mesh->submeshMaterials[submesh];
            world.meshes.push_back({
                .mesh = mesh->handle,
                .submesh = submesh,
                .material = override.isValid() || !material.isValid() ? override
                                                                       : assets.material(material),
                .transform = transform.matrix,
                .objectId = entity.index + 1,
            });
        }
    }

    for ([[maybe_unused]] auto [entity, transform, renderer] :
         scene.view<scene::WorldTransform, scene::SkinnedMeshRenderer>())
    {
        const LoadedMesh* const mesh = assets.mesh(renderer.mesh);
        const asset::MeshData* const data = mesh != nullptr ? assets.meshData(renderer.mesh) : nullptr;
        if (mesh == nullptr || data == nullptr || data->inverseBind.empty())
        {
            continue;
        }
        // A bone matrix takes a vertex from the bind pose of the mesh to where its bone stands now.
        const auto firstBone = static_cast<std::uint32_t>(world.boneMatrices.size());
        for (std::size_t joint = 0; joint < data->inverseBind.size(); ++joint)
        {
            const scene::Entity bone =
                joint < renderer.bones.size() ? scene.resolve(renderer.bones[joint]) : scene::Entity{};
            const scene::WorldTransform* const boneTransform =
                bone.isValid() ? scene.tryGet<scene::WorldTransform>(bone) : nullptr;
            // A missing bone leaves its vertices where the entity stands.
            world.boneMatrices.push_back(boneTransform != nullptr
                                             ? boneTransform->matrix * data->inverseBind[joint]
                                             : transform.matrix);
        }

        const render::MaterialHandle override =
            renderer.material.isValid() ? assets.material(renderer.material) : render::MaterialHandle{};
        for (std::uint32_t submesh = 0; submesh < mesh->submeshMaterials.size(); ++submesh)
        {
            const asset::AssetId material = mesh->submeshMaterials[submesh];
            world.meshes.push_back({
                .mesh = mesh->handle,
                .submesh = submesh,
                .material = override.isValid() || !material.isValid() ? override
                                                                       : assets.material(material),
                // Bones reach the world on their own; vertices without weights follow the entity.
                .transform = transform.matrix,
                .firstBone = firstBone,
                .boneCount = static_cast<std::uint32_t>(data->inverseBind.size()),
                .objectId = entity.index + 1,
            });
        }
    }
}

void extractParticles(const particles::ParticleWorld& particles, AssetManager& assets, render::RenderWorld& world)
{
    std::vector<RibbonPoint> ribbon;
    const std::uint32_t trailCapacity = particles::trailPointsPerParticle();
    particles.forEachEmitter([&](const particles::EmitterView& emitter) {
        const scene::ParticleEmitter& settings = *emitter.settings;
        const bool local = settings.space == scene::ParticleSpace::Local;
        const math::Mat3 basis(emitter.transform);
        const auto toWorld = [&](math::Vec3 point) {
            return local ? math::Vec3(emitter.transform * math::Vec4(point, 1.0f)) : point;
        };
        const render::TextureHandle texture =
            settings.texture.isValid() ? assets.texture(settings.texture) : render::TextureHandle{};
        const bool stretched = settings.renderMode == scene::ParticleRenderMode::Stretched;
        render::ParticleDraw draw{
            .first = static_cast<std::uint32_t>(world.particles.size()),
            .count = static_cast<std::uint32_t>(emitter.particles.size()),
            .texture = texture,
            .blend = static_cast<render::ParticleBlend>(settings.blend),
            .facing = static_cast<render::ParticleFacing>(settings.renderMode),
            .sheetColumns = static_cast<std::uint32_t>(std::max(settings.sheetColumns, 1)),
            .sheetRows = static_cast<std::uint32_t>(std::max(settings.sheetRows, 1)),
            .lit = settings.lit,
            .softness = settings.softness,
        };
        math::Vec3 low{std::numeric_limits<float>::max()};
        math::Vec3 high{std::numeric_limits<float>::lowest()};
        for (const particles::Particle& particle : emitter.particles)
        {
            const math::Vec3 position = toWorld(particle.position);
            low = math::min(low, position);
            high = math::max(high, position);
            world.particles.push_back({
                .position = position,
                .size = particle.size,
                .color = particle.color,
                .stretch = stretched ? (local ? basis * particle.velocity : particle.velocity) * settings.stretch
                                     : math::Vec3{0.0f},
                .rotation = particle.rotation,
                .frame = particle.frame,
            });
        }
        // Batches are ordered by the middle of what they cover.
        draw.center = (low + high) * 0.5f;
        world.particleDraws.push_back(draw);

        if (!settings.trails || emitter.trailCounts.empty())
        {
            return;
        }
        render::ParticleDraw ribbons = draw;
        ribbons.ribbons = true;
        ribbons.texture = {};
        ribbons.first = static_cast<std::uint32_t>(world.trailSegments.size());
        const float life = std::max(settings.trailTime, 0.01f);
        for (const particles::Particle& particle : emitter.particles)
        {
            const std::uint8_t count = particle.trail < emitter.trailCounts.size() ? emitter.trailCounts[particle.trail] : 0;
            if (count == 0)
            {
                continue;
            }
            // From the particle back along the points it left, thinner and fainter with their age.
            const float width = particle.size * settings.trailWidth;
            ribbon.clear();
            ribbon.push_back({.position = toWorld(particle.position), .width = width, .color = particle.color});
            for (std::uint32_t point = 0; point < count; ++point)
            {
                const particles::TrailPoint& left = emitter.trails[particle.trail * trailCapacity + point];
                const float age = std::clamp((emitter.time - left.time) / life, 0.0f, 1.0f);
                if (age >= 1.0f)
                {
                    break;
                }
                ribbon.push_back({.position = toWorld(left.position),
                                  .width = width * (1.0f - age),
                                  .color = math::Vec4(math::Vec3(particle.color), particle.color.a * (1.0f - age))});
            }
            addRibbon(world, ribbon);
        }
        ribbons.count = static_cast<std::uint32_t>(world.trailSegments.size()) - ribbons.first;
        if (ribbons.count > 0)
        {
            world.particleDraws.push_back(ribbons);
        }
    });

    particles.forEachTrail([&](const particles::TrailView& trail) {
        const scene::TrailRenderer& settings = *trail.settings;
        const float life = std::max(settings.time, 0.001f);
        const auto at = [&](float age) {
            const float t = std::clamp(age / life, 0.0f, 1.0f);
            const math::Vec4 color = settings.color + (settings.endColor - settings.color) * t;
            return RibbonPoint{.width = settings.width + (settings.endWidth - settings.width) * t,
                               .color = math::Vec4(math::Vec3(color) * settings.intensity, color.a)};
        };
        ribbon.clear();
        RibbonPoint head = at(0.0f);
        head.position = trail.head;
        ribbon.push_back(head);
        for (const particles::TrailPoint& point : trail.points)
        {
            RibbonPoint next = at(trail.time - point.time);
            next.position = point.position;
            ribbon.push_back(next);
        }
        const auto first = static_cast<std::uint32_t>(world.trailSegments.size());
        addRibbon(world, ribbon);
        const auto count = static_cast<std::uint32_t>(world.trailSegments.size()) - first;
        if (count == 0)
        {
            return;
        }
        world.particleDraws.push_back({
            .ribbons = true,
            .first = first,
            .count = count,
            .texture = settings.texture.isValid() ? assets.texture(settings.texture) : render::TextureHandle{},
            .blend = static_cast<render::ParticleBlend>(settings.blend),
            .lit = settings.lit,
            .softness = settings.softness,
            .center = trail.head,
        });
    });
}

} // namespace devex::runtime
