#pragma once

#include <devex/reflection/Reflection.hpp>
#include <devex/scene/DynamicComponent.hpp>
#include <devex/scene/Entity.hpp>
#include <devex/scene/Scene.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace devex::scene {

// Type-erased access to a reflected component type, used by generic code such as scene files.
struct ComponentType
{
    const reflection::TypeInfo* type = nullptr;
    // The componentTypeIndex of the type.
    std::size_t index = 0;
    // Adds a default-constructed component, or returns the one the entity already has.
    std::function<void*(Scene& scene, Entity entity)> emplace;
    std::function<const void*(const Scene& scene, Entity entity)> find;
    // The same component, to write into; null when the entity does not carry it. Nothing is
    // added: a style only touches what an element already has.
    std::function<void*(Scene& scene, Entity entity)> findMutable;
    std::function<void(Scene& scene, Entity entity)> remove;
    // The layout of a type described while the engine runs, such as a C# component; null for the
    // types declared in C++.
    std::shared_ptr<const DynamicComponentLayout> layout;

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
            .index = componentTypeIndex<T>(),
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
            .findMutable = [](Scene& scene, Entity entity) -> void* {
                return scene.tryGet<T>(entity);
            },
            .remove = [](Scene& scene, Entity entity) { scene.remove<T>(entity); },
        });
        ++m_generation;
    }

    // Registers a component type described while the engine runs, with the memory the engine gives
    // it. Registering a name twice has no effect and returns false.
    bool addDynamic(std::shared_ptr<const DynamicComponentLayout> layout);

    // Forgets a type, as when the game module that registered it is unloaded. Returns whether the
    // type was registered.
    bool remove(std::string_view name);

    // The pointer stays valid until the next registration or removal.
    [[nodiscard]] const ComponentType* find(std::string_view name) const noexcept;
    [[nodiscard]] const ComponentType* findByIndex(std::size_t index) const noexcept;

    // In registration order, which is the order of components in saved files.
    [[nodiscard]] std::span<const ComponentType> types() const noexcept;

    // Changes with every registration and removal, so that code caching types by name, such as C#
    // views, knows when to look them up again.
    [[nodiscard]] std::uint64_t generation() const noexcept
    {
        return m_generation;
    }

private:
    std::vector<ComponentType> m_types;
    std::uint64_t m_generation = 0;
};

// The process-wide registry, which already contains the built-in components.
[[nodiscard]] ComponentRegistry& componentRegistry();

template <typename T>
void registerComponent()
{
    componentRegistry().add<T>();
}

} // namespace devex::scene
