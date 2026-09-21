#include <devex/core/Assert.hpp>
#include <devex/scene/AnimationComponents.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/ModelInstantiation.hpp>

#include <vector>

namespace devex::scene {

Entity instantiateModel(Scene& scene, const asset::ModelData& model, const std::string& rootName,
                        Entity parent)
{
    const Entity root = scene.createEntity(rootName);
    scene.add<Transform>(root);
    if (parent.isValid())
    {
        const core::Result<void> attached = scene.setParent(root, parent);
        DEVEX_ASSERT_MSG(attached.has_value(), "a new entity can always be attached");
    }

    std::vector<Entity> entities;
    entities.reserve(model.nodes.size());
    for (const asset::ModelNode& node : model.nodes)
    {
        const Entity entity = scene.createEntity(node.name);
        scene.add<Transform>(entity, Transform{node.translation, node.rotation, node.scale});
        if (node.mesh.isValid() && node.skin < 0)
        {
            scene.add<MeshRenderer>(entity, MeshRenderer{.mesh = node.mesh});
        }
        else if (node.mesh.isValid())
        {
            // The bones are filled once every node has its entity.
            scene.add<SkinnedMeshRenderer>(entity, SkinnedMeshRenderer{.mesh = node.mesh});
        }
        // Parents come before their children, so the parent entity already exists.
        const bool hasParent = node.parent >= 0 && static_cast<std::size_t>(node.parent) < entities.size();
        const core::Result<void> attached =
            scene.setParent(entity, hasParent ? entities[static_cast<std::size_t>(node.parent)] : root);
        DEVEX_ASSERT_MSG(attached.has_value(), "model nodes list their parents first");
        entities.push_back(entity);
    }

    // Skinned meshes follow the entities of the joints of their skin.
    for (std::size_t index = 0; index < model.nodes.size(); ++index)
    {
        const asset::ModelNode& node = model.nodes[index];
        SkinnedMeshRenderer* const renderer = scene.tryGet<SkinnedMeshRenderer>(entities[index]);
        if (renderer == nullptr || node.skin < 0 ||
            static_cast<std::size_t>(node.skin) >= model.skins.size())
        {
            continue;
        }
        for (const std::int32_t joint : model.skins[static_cast<std::size_t>(node.skin)].joints)
        {
            const bool known = joint >= 0 && static_cast<std::size_t>(joint) < entities.size();
            renderer->bones.push_back(known ? EntityRef{scene.uuid(entities[static_cast<std::size_t>(joint)])}
                                            : EntityRef{});
        }
    }
    return root;
}

} // namespace devex::scene
