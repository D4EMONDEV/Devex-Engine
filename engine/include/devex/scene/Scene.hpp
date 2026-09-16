#pragma once

#include <devex/core/Assert.hpp>
#include <devex/core/Error.hpp>
#include <devex/core/Uuid.hpp>
#include <devex/scene/ComponentPool.hpp>
#include <devex/scene/Entity.hpp>
#include <devex/scene/View.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace devex::scene {

namespace detail {
[[nodiscard]] std::size_t nextComponentTypeIndex() noexcept;
} // namespace detail

// Position of the pool of T in every scene, assigned the first time the type is used.
template <typename T>
[[nodiscard]] std::size_t componentTypeIndex() noexcept
{
    static const std::size_t index = detail::nextComponentTypeIndex();
    return index;
}

// A world of entities organized in a hierarchy. Each entity has a UUID, a name and any number of
// components, at most one per type, stored contiguously per type.
class Scene
{
public:
    Scene();
    Scene(Scene&& other) noexcept;
    Scene& operator=(Scene&& other) noexcept;
    ~Scene();

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    // Creates a root entity with a new UUID.
    [[nodiscard]] Entity createEntity(std::string name = {});
    // Creates a root entity with a known UUID, which must be valid and unused in this scene.
    [[nodiscard]] core::Result<Entity> createEntity(core::Uuid uuid, std::string name);
    // Destroys the entity, its components and all its descendants.
    void destroyEntity(Entity entity);

    [[nodiscard]] bool isAlive(Entity entity) const noexcept;
    [[nodiscard]] std::size_t entityCount() const noexcept;
    // Returns an invalid entity when no entity has this UUID.
    [[nodiscard]] Entity findEntity(core::Uuid uuid) const noexcept;

    [[nodiscard]] core::Uuid uuid(Entity entity) const noexcept;
    [[nodiscard]] const std::string& name(Entity entity) const noexcept;
    void setName(Entity entity, std::string name);

    // Makes child the last child of parent, or a root when parent is invalid. The local
    // transform is kept. Fails when parent is child itself or one of its descendants.
    [[nodiscard]] core::Result<void> setParent(Entity child, Entity parent);
    [[nodiscard]] Entity parent(Entity entity) const noexcept;
    [[nodiscard]] Entity firstChild(Entity entity) const noexcept;
    [[nodiscard]] Entity nextSibling(Entity entity) const noexcept;
    // Roots are ordered like children, and walked with nextSibling.
    [[nodiscard]] Entity firstRoot() const noexcept;

    template <typename T, typename... Args>
    T& add(Entity entity, Args&&... args)
    {
        DEVEX_ASSERT_MSG(isAlive(entity), "cannot add a component to a destroyed entity");
        return pool<T>().emplace(entity, std::forward<Args>(args)...);
    }

    template <typename T>
    [[nodiscard]] T* tryGet(Entity entity) noexcept
    {
        ComponentPool<T>* const components = findPool<T>();
        return components == nullptr ? nullptr : components->find(entity);
    }

    template <typename T>
    [[nodiscard]] const T* tryGet(Entity entity) const noexcept
    {
        const ComponentPool<T>* const components = findPool<T>();
        return components == nullptr ? nullptr : components->find(entity);
    }

    template <typename T>
    [[nodiscard]] T& get(Entity entity) noexcept
    {
        T* const component = tryGet<T>(entity);
        DEVEX_ASSERT_MSG(component != nullptr, "the entity has no such component");
        return *component;
    }

    template <typename T>
    [[nodiscard]] const T& get(Entity entity) const noexcept
    {
        const T* const component = tryGet<T>(entity);
        DEVEX_ASSERT_MSG(component != nullptr, "the entity has no such component");
        return *component;
    }

    template <typename T>
    [[nodiscard]] bool has(Entity entity) const noexcept
    {
        return tryGet<T>(entity) != nullptr;
    }

    template <typename T>
    void remove(Entity entity)
    {
        if (ComponentPool<T>* const components = findPool<T>())
        {
            components->remove(entity);
        }
    }

    template <typename... Components>
    [[nodiscard]] View<Components...> view() noexcept
    {
        return View<Components...>(
            std::make_tuple(findPool<std::remove_const_t<Components>>()...));
    }

    // Computes the WorldTransform of every entity that has a Transform, parents first. Entities
    // without a Transform pass their parent's transform down to their children.
    void updateTransforms();

private:
    struct EntityRecord
    {
        core::Uuid uuid;
        std::string name;
        std::uint32_t generation = 0;
        bool alive = false;
        Entity parent;
        Entity firstChild;
        Entity lastChild;
        Entity previousSibling;
        Entity nextSibling;
    };

    template <typename T>
    [[nodiscard]] ComponentPool<T>* findPool() noexcept
    {
        const std::size_t index = componentTypeIndex<T>();
        return index < m_pools.size() ? static_cast<ComponentPool<T>*>(m_pools[index].get())
                                      : nullptr;
    }

    template <typename T>
    [[nodiscard]] const ComponentPool<T>* findPool() const noexcept
    {
        const std::size_t index = componentTypeIndex<T>();
        return index < m_pools.size() ? static_cast<const ComponentPool<T>*>(m_pools[index].get())
                                      : nullptr;
    }

    template <typename T>
    [[nodiscard]] ComponentPool<T>& pool()
    {
        const std::size_t index = componentTypeIndex<T>();
        if (index >= m_pools.size())
        {
            m_pools.resize(index + 1);
        }
        if (m_pools[index] == nullptr)
        {
            m_pools[index] = std::make_unique<ComponentPool<T>>();
        }
        return static_cast<ComponentPool<T>&>(*m_pools[index]);
    }

    [[nodiscard]] EntityRecord& record(Entity entity) noexcept;
    [[nodiscard]] const EntityRecord& record(Entity entity) const noexcept;
    void attach(Entity child, Entity parent) noexcept;
    void detach(Entity child) noexcept;

    std::vector<EntityRecord> m_entities;
    std::vector<std::uint32_t> m_freeIndices;
    std::vector<std::unique_ptr<ComponentPoolBase>> m_pools;
    std::unordered_map<core::Uuid, Entity> m_entitiesByUuid;
    Entity m_firstRoot;
    Entity m_lastRoot;
    std::size_t m_entityCount = 0;
};

} // namespace devex::scene
