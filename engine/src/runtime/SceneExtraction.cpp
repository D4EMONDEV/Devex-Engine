#include <devex/runtime/SceneExtraction.hpp>
#include <devex/scene/Components.hpp>

namespace devex::runtime {

void extractScene(scene::Scene& scene, AssetManager& assets, render::RenderWorld& world)
{
    for ([[maybe_unused]] auto [entity, transform, camera] :
         scene.view<scene::WorldTransform, scene::Camera>())
    {
        if (camera.primary)
        {
            world.camera.view = math::inverse(transform.matrix);
            world.camera.verticalFov = camera.verticalFov;
            world.camera.nearPlane = camera.nearPlane;
            break;
        }
    }

    const auto lights = scene.view<scene::WorldTransform, scene::DirectionalLight>();
    if (const auto first = lights.begin(); first != lights.end())
    {
        [[maybe_unused]] const auto [entity, transform, light] = *first;
        world.lightDirection = math::Mat3(transform.matrix) * math::Vec3{0.0f, 0.0f, -1.0f};
        world.ambient = light.ambient;
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
            });
        }
    }
}

} // namespace devex::runtime
