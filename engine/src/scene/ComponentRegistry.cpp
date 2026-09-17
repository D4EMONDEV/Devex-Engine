#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/Components.hpp>

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
    return std::erase_if(m_types, [name](const ComponentType& type) { return type.name() == name; }) > 0;
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

ComponentRegistry& componentRegistry()
{
    static ComponentRegistry registry = [] {
        ComponentRegistry builtins;
        builtins.add<Transform>();
        builtins.add<MeshRenderer>();
        builtins.add<Camera>();
        builtins.add<DirectionalLight>();
        builtins.add<PointLight>();
        builtins.add<SpotLight>();
        builtins.add<Environment>();
        return builtins;
    }();
    return registry;
}

} // namespace devex::scene
