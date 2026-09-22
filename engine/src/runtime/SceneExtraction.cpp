#include <devex/render/Photometry.hpp>
#include <devex/runtime/SceneExtraction.hpp>
#include <devex/scene/AnimationComponents.hpp>
#include <devex/scene/Components.hpp>

#include <cmath>
#include <cstdint>

namespace devex::runtime {

void extractScene(scene::Scene& scene, AssetManager& assets, render::RenderWorld& world)
{
    for ([[maybe_unused]] auto [entity, transform, camera] :
         scene.view<scene::WorldTransform, scene::Camera>())
    {
        if (camera.primary)
        {
            world.camera = {
                .view = math::inverse(transform.matrix),
                .verticalFov = camera.verticalFov,
                .nearPlane = camera.nearPlane,
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

} // namespace devex::runtime
