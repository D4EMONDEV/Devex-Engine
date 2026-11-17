#include "ToolsState.hpp"

#include <devex/asset/MeshData.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/CsgComponents.hpp>
#include <devex/scene/PhysicsComponents.hpp>

#include <algorithm>
#include <format>
#include <string>

namespace devex::tools::detail {

namespace {

[[nodiscard]] bool isCsg(const scene::Scene& scene, scene::Entity entity)
{
    return scene.tryGet<scene::CsgBox>(entity) != nullptr || scene.tryGet<scene::CsgSphere>(entity) != nullptr ||
           scene.tryGet<scene::CsgCylinder>(entity) != nullptr || scene.tryGet<scene::CsgPath>(entity) != nullptr ||
           scene.tryGet<scene::CsgCombiner>(entity) != nullptr;
}

// A name that every system takes for a file.
[[nodiscard]] std::string fileName(std::string name)
{
    std::ranges::replace_if(name, [](char c) { return static_cast<unsigned char>(c) < 32 || std::string_view("<>:\"/\\|?*").contains(c); }, '_');
    return name;
}

} // namespace

void bakeCsgMesh(ToolsState& state, scene::Scene& scene, scene::Entity entity)
{
    // The solid the shape belongs to: its root, the highest of the shapes above it.
    scene::Entity root = entity;
    for (scene::Entity parent = scene.parent(root); parent.isValid() && isCsg(scene, parent); parent = scene.parent(parent))
    {
        root = parent;
    }
    const scene::GeneratedMesh* const generated = scene.tryGet<scene::GeneratedMesh>(root);
    const asset::MeshData* const built = generated != nullptr && state.meshes ? state.meshes(generated->mesh) : nullptr;
    if (built == nullptr || built->indices.empty())
    {
        DEVEX_LOG_WARNING("{} has no mesh to bake: it makes no solid yet", scene.name(root));
        return;
    }
    if (state.database == nullptr || state.scenePath.empty())
    {
        DEVEX_LOG_WARNING("Save the scene first: the mesh of {} is written next to it", scene.name(root));
        return;
    }
    asset::MeshData mesh;
    mesh.vertices = built->vertices;
    mesh.indices = built->indices;
    mesh.submeshes = built->submeshes;
    const std::filesystem::path file =
        state.scenePath.parent_path() /
        core::pathFromUtf8(fileName(std::format("{} {}", core::toUtf8(state.scenePath.stem()), scene.name(root))) + std::string(asset::meshExtension));
    if (core::Result<void> written = core::writeFileAtomically(file, asset::encodeMeshFile(mesh)); !written)
    {
        DEVEX_LOG_ERROR("Cannot bake {}: {}", scene.name(root), written.error());
        return;
    }
    const std::string resource = state.database->project().resourcePath(file);
    state.database->refresh();
    const std::optional<asset::AssetId> id = state.database->findByPath(resource);
    if (!id)
    {
        DEVEX_LOG_ERROR("{} is written, but outside the assets folder: move the scene there", resource);
        return;
    }
    // Imported at once, for the entity to draw it.
    state.database->waitForImports();

    // An entity that draws it beside the solid, where the solid is, with its collision; the solid stays, for the user to
    // delete once the mesh is right, as Bake Mesh Instance of Godot leaves it.
    const scene::Transform transform = scene.tryGet<scene::Transform>(root) != nullptr ? scene.get<scene::Transform>(root) : scene::Transform{};
    const bool collision = generated->collision;
    const scene::Entity parent = scene.parent(root);
    const std::string name = scene.name(root) + " Mesh";
    requestCreatePreset(state, parent.isValid() ? scene.uuid(parent) : core::Uuid{}, name.c_str(),
                        [&](scene::Scene& scratch, scene::Entity made) {
                            scratch.get<scene::Transform>(made) = transform;
                            scratch.add<scene::MeshRenderer>(made, scene::MeshRenderer{.mesh = *id});
                            if (collision)
                            {
                                scratch.add<scene::RigidBody>(made, scene::RigidBody{.type = scene::BodyType::Static});
                                scratch.add<scene::MeshCollider>(made, scene::MeshCollider{.mesh = *id});
                            }
                        });
    DEVEX_LOG_INFO("Baked {} into {}: {} triangles{}", scene.name(root), resource, mesh.indices.size() / 3,
                   collision ? ", with its collision" : "");
}

} // namespace devex::tools::detail
