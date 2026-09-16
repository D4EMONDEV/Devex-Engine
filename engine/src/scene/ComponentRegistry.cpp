#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/Components.hpp>

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
        return builtins;
    }();
    return registry;
}

} // namespace devex::scene
