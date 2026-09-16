#pragma once

#include <devex/reflection/Reflection.hpp>
#include <devex/scene/Entity.hpp>
#include <devex/scene/Scene.hpp>

#include <span>
#include <string_view>
#include <vector>

namespace devex::scene {

// Type-erased access to a reflected component type, used by generic code such as scene files.
struct ComponentType
{
    const reflection::TypeInfo* type = nullptr;
    // Adds a default-constructed component, or returns the one the entity already has.
    void* (*emplace)(Scene& scene, Entity entity) = nullptr;
    const void* (*find)(const Scene& scene, Entity entity) = nullptr;
    void (*remove)(Scene& scene, Entity entity) = nullptr;

    [[nodiscard]] std::string_view name() const noexcept
    {
        return type->name;
    }
};

// Component types known by name. Only registered components are saved and loaded.
class ComponentRegistry
{
public:
    // Registering the same type twice has no effect.
    template <typename T>
    void add()
    {
        const reflection::TypeInfo& info = reflection::typeInfo<T>();
        if (find(info.name) != nullptr)
        {
            return;
        }
        m_types.push_back({
            .type = &info,
            .emplace = [](Scene& scene, Entity entity) -> void* {
                if (T* const existing = scene.tryGet<T>(entity))
                {
                    return existing;
                }
                return &scene.add<T>(entity);
            },
            .find = [](const Scene& scene, Entity entity) -> const void* {
                return scene.tryGet<T>(entity);
            },
            .remove = [](Scene& scene, Entity entity) { scene.remove<T>(entity); },
        });
    }

    // The pointer stays valid until the next registration.
    [[nodiscard]] const ComponentType* find(std::string_view name) const noexcept;

    // In registration order, which is the order of components in saved files.
    [[nodiscard]] std::span<const ComponentType> types() const noexcept;

private:
    std::vector<ComponentType> m_types;
};

// The process-wide registry, which already contains the built-in components.
[[nodiscard]] ComponentRegistry& componentRegistry();

template <typename T>
void registerComponent()
{
    componentRegistry().add<T>();
}

} // namespace devex::scene
