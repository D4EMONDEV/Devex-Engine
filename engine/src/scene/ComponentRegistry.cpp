#include <devex/scene/AnimationComponents.hpp>
#include <devex/scene/AudioComponents.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/PhysicsComponents.hpp>

#include <vector>

namespace devex::scene {

const ComponentType* ComponentRegistry::find(std::string_view name) const noexcept
{
    for (const ComponentType& type : m_types)
    {
        if (type.name() == name)
        {
            return &type;
        }
    }
    return nullptr;
}

bool ComponentRegistry::remove(std::string_view name)
{
    if (std::erase_if(m_types, [name](const ComponentType& type) { return type.name() == name; }) == 0)
    {
        return false;
    }
    ++m_generation;
    return true;
}

const ComponentType* ComponentRegistry::findByIndex(std::size_t index) const noexcept
{
    for (const ComponentType& type : m_types)
    {
        if (type.index == index)
        {
            return &type;
        }
    }
    return nullptr;
}

std::span<const ComponentType> ComponentRegistry::types() const noexcept
{
    return m_types;
}

bool ComponentRegistry::addDynamic(std::shared_ptr<const DynamicComponentLayout> layout)
{
    const std::string_view name = layout->type().name;
    if (find(name) != nullptr)
    {
        return false;
    }
    // Types described at runtime get their index from their name, so that a scene loaded before the
    // type came back finds the same pool.
    const std::size_t index = detail::componentTypeIndex(std::string("dynamic:") + std::string(name));
    m_types.push_back({
        .type = &layout->type(),
        .index = index,
        .emplace = [index, layout](Scene& scene, Entity entity) -> void* {
            return scene.dynamicPool(index, layout).emplace(entity);
        },
        .find = [index](const Scene& scene, Entity entity) -> const void* {
            const DynamicComponentPool* const pool = scene.dynamicPool(index);
            return pool != nullptr ? pool->find(entity) : nullptr;
        },
        .remove = [index](Scene& scene, Entity entity) {
            if (DynamicComponentPool* const pool = scene.dynamicPool(index))
            {
                pool->remove(entity);
            }
        },
        .layout = std::move(layout),
    });
    ++m_generation;
    return true;
}

ComponentRegistry& componentRegistry()
{
    static ComponentRegistry registry = [] {
        ComponentRegistry builtins;
        builtins.add<Transform>();
        builtins.add<MeshRenderer>();
        builtins.add<SkinnedMeshRenderer>();
        builtins.add<Animator>();
        builtins.add<Camera>();
        builtins.add<DirectionalLight>();
        builtins.add<PointLight>();
        builtins.add<SpotLight>();
        builtins.add<Environment>();
        builtins.add<RigidBody>();
        builtins.add<BoxCollider>();
        builtins.add<SphereCollider>();
        builtins.add<CapsuleCollider>();
        builtins.add<CylinderCollider>();
        builtins.add<MeshCollider>();
        builtins.add<CharacterController>();
        builtins.add<AudioSource>();
        builtins.add<AudioListener>();
        return builtins;
    }();
    return registry;
}

} // namespace devex::scene
