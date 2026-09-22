#pragma once

#include <devex/core/Assert.hpp>
#include <devex/core/Error.hpp>
#include <devex/core/Uuid.hpp>
#include <devex/scene/ComponentPool.hpp>
#include <devex/scene/DynamicComponent.hpp>
#include <devex/scene/Entity.hpp>
#include <devex/scene/EntityRef.hpp>
#include <devex/scene/View.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <typeinfo>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace devex::scene {

namespace detail {
// The index of a component type identified by its decorated name, assigned on first use. The
// engine keeps these indices, so that the editor and game modules agree on them.
[[nodiscard]] std::size_t componentTypeIndex(std::string_view typeKey);

template <typename T>
[[nodiscard]] const char* typeKey() noexcept
{
#ifdef _MSC_VER
    // Decorated names tell apart types of unnamed namespaces in different source files.
    return typeid(T).raw_name();
#else
    return typeid(T).name();
#endif
}
} // namespace detail

// Position of the pool of T in every scene, assigned the first time the type is used.
template <typename T>
[[nodiscard]] std::size_t componentTypeIndex()
{
    static const std::size_t index = detail::componentTypeIndex(detail::typeKey<T>());
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

    // A copy with the same entities, handles, UUIDs, names, hierarchy and components, so that
    // handles of this scene also refer to the copy, as when the editor plays a scene. Every
    // component type must be copyable.
    [[nodiscard]] Scene clone() const;

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
    // The live entity stored at this index, or an invalid entity. Rendering identifies the entity
    // of each pixel by index, for picking.
    [[nodiscard]] Entity entityAtIndex(std::uint32_t index) const noexcept;

    [[nodiscard]] core::Uuid uuid(Entity entity) const noexcept;
    // The entity a reference names, or an invalid entity when it is empty or names no entity.
    [[nodiscard]] Entity resolve(EntityRef reference) const noexcept;
    // A reference to the entity, empty when the entity is not alive.
    [[nodiscard]] EntityRef reference(Entity entity) const noexcept;
    [[nodiscard]] const std::string& name(Entity entity) const noexcept;
    void setName(Entity entity, std::string name);

    // Makes child a child of parent, or a root when parent is invalid, placed just before the
    // sibling `before`, or last when `before` is invalid. The local transform is kept. Fails when
    // parent is child itself or one of its descendants, or when `before` is not a child of
    // parent. Keeps the current position when parent is unchanged and `before` is invalid.
    [[nodiscard]] core::Result<void> setParent(Entity child, Entity parent, Entity before = {});
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

    // Reading a scene that cannot change, such as the one a system only looks at. Every component
    // of the view is then const.
    template <typename... Components>
    [[nodiscard]] View<const Components...> view() const noexcept
    {
        return View<const Components...>(std::make_tuple(
            const_cast<ComponentPool<std::remove_const_t<Components>>*>(
                findPool<std::remove_const_t<Components>>())...));
    }

    // Computes the WorldTransform of every entity that has a Transform, parents first. Entities
    // without a Transform pass their parent's transform down to their children.
    void updateTransforms();

    // The pool of a component type described while the engine runs, such as a C# component,
    // created with the layout when the scene has none yet.
    [[nodiscard]] DynamicComponentPool& dynamicPool(std::size_t typeIndex,
                                                    const std::shared_ptr<const DynamicComponentLayout>& layout);
    // Null when the scene has no component of the type.
    [[nodiscard]] DynamicComponentPool* dynamicPool(std::size_t typeIndex) noexcept;
    [[nodiscard]] const DynamicComponentPool* dynamicPool(std::size_t typeIndex) const noexcept;

    // The pools of component types, by componentTypeIndex; null where a type has no pool yet.
    [[nodiscard]] std::size_t componentPoolCount() const noexcept;
    [[nodiscard]] const ComponentPoolBase* componentPool(std::size_t typeIndex) const noexcept;
    // Destroys a pool with all its components, as before unloading the module that created it.
    void destroyComponentPool(std::size_t typeIndex) noexcept;

private:
    struct EntityRecord
    {
        core::Uuid uuid;
        std::string name;
        // From 1, so that a live entity never has the null handle of C#, all zeros.
        std::uint32_t generation = 1;
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
    void attach(Entity child, Entity parent, Entity before) noexcept;
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
