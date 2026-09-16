#include <devex/scene/Components.hpp>
#include <devex/scene/Scene.hpp>

#include <atomic>
#include <utility>

namespace devex::scene {

namespace detail {

std::size_t nextComponentTypeIndex() noexcept
{
    static std::atomic<std::size_t> next{0};
    return next.fetch_add(1);
}

} // namespace detail

Scene::Scene() = default;
Scene::Scene(Scene&& other) noexcept = default;
Scene& Scene::operator=(Scene&& other) noexcept = default;
Scene::~Scene() = default;

Entity Scene::createEntity(std::string name)
{
    // A generated UUID cannot collide in practice, so creation cannot fail.
    core::Result<Entity> entity = createEntity(core::Uuid::generate(), std::move(name));
    DEVEX_ASSERT(entity.has_value());
    return entity.value_or(Entity{});
}

core::Result<Entity> Scene::createEntity(core::Uuid uuid, std::string name)
{
    if (uuid.isNil())
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "an entity needs a non-nil UUID");
    }
    if (m_entitiesByUuid.contains(uuid))
    {
        return core::makeError(core::ErrorCode::AlreadyExists, "entity {} already exists", uuid);
    }

    Entity entity;
    if (m_freeIndices.empty())
    {
        entity.index = static_cast<std::uint32_t>(m_entities.size());
        m_entities.emplace_back();
    }
    else
    {
        entity.index = m_freeIndices.back();
        m_freeIndices.pop_back();
    }

    EntityRecord& created = m_entities[entity.index];
    entity.generation = created.generation;
    created.uuid = uuid;
    created.name = std::move(name);
    created.alive = true;

    attach(entity, Entity{});
    m_entitiesByUuid.emplace(uuid, entity);
    ++m_entityCount;
    return entity;
}

void Scene::destroyEntity(Entity entity)
{
    if (!isAlive(entity))
    {
        return;
    }
    while (record(entity).firstChild.isValid())
    {
        destroyEntity(record(entity).firstChild);
    }

    detach(entity);
    for (const std::unique_ptr<ComponentPoolBase>& components : m_pools)
    {
        if (components != nullptr)
        {
            components->remove(entity);
        }
    }

    EntityRecord& destroyed = record(entity);
    m_entitiesByUuid.erase(destroyed.uuid);
    destroyed = EntityRecord{.generation = destroyed.generation + 1};
    m_freeIndices.push_back(entity.index);
    --m_entityCount;
}

bool Scene::isAlive(Entity entity) const noexcept
{
    return entity.index < m_entities.size() && m_entities[entity.index].alive &&
           m_entities[entity.index].generation == entity.generation;
}

std::size_t Scene::entityCount() const noexcept
{
    return m_entityCount;
}

Entity Scene::findEntity(core::Uuid uuid) const noexcept
{
    const auto found = m_entitiesByUuid.find(uuid);
    return found == m_entitiesByUuid.end() ? Entity{} : found->second;
}

core::Uuid Scene::uuid(Entity entity) const noexcept
{
    return record(entity).uuid;
}

const std::string& Scene::name(Entity entity) const noexcept
{
    return record(entity).name;
}

void Scene::setName(Entity entity, std::string name)
{
    record(entity).name = std::move(name);
}

core::Result<void> Scene::setParent(Entity child, Entity parent)
{
    if (!isAlive(child) || (parent.isValid() && !isAlive(parent)))
    {
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "cannot parent destroyed entities");
    }
    for (Entity ancestor = parent; ancestor.isValid(); ancestor = record(ancestor).parent)
    {
        if (ancestor == child)
        {
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "'{}' cannot become a descendant of itself", name(child));
        }
    }

    if (record(child).parent == parent)
    {
        return {};
    }
    detach(child);
    attach(child, parent);
    return {};
}

Entity Scene::parent(Entity entity) const noexcept
{
    return record(entity).parent;
}

Entity Scene::firstChild(Entity entity) const noexcept
{
    return record(entity).firstChild;
}

Entity Scene::nextSibling(Entity entity) const noexcept
{
    return record(entity).nextSibling;
}

Entity Scene::firstRoot() const noexcept
{
    return m_firstRoot;
}

void Scene::updateTransforms()
{
    struct Pending
    {
        Entity entity;
        math::Mat4 parentMatrix;
    };

    std::vector<Pending> pending;
    for (Entity root = m_firstRoot; root.isValid(); root = record(root).nextSibling)
    {
        pending.push_back({root, math::Mat4{1.0f}});
    }

    while (!pending.empty())
    {
        const auto [entity, parentMatrix] = pending.back();
        pending.pop_back();

        math::Mat4 matrix = parentMatrix;
        if (const Transform* const local = tryGet<Transform>(entity))
        {
            matrix = parentMatrix * local->matrix();
            if (WorldTransform* const world = tryGet<WorldTransform>(entity))
            {
                world->matrix = matrix;
            }
            else
            {
                add<WorldTransform>(entity, matrix);
            }
        }

        for (Entity child = record(entity).firstChild; child.isValid();
             child = record(child).nextSibling)
        {
            pending.push_back({child, matrix});
        }
    }
}

Scene::EntityRecord& Scene::record(Entity entity) noexcept
{
    DEVEX_ASSERT_MSG(isAlive(entity), "the entity was destroyed");
    return m_entities[entity.index];
}

const Scene::EntityRecord& Scene::record(Entity entity) const noexcept
{
    DEVEX_ASSERT_MSG(isAlive(entity), "the entity was destroyed");
    return m_entities[entity.index];
}

void Scene::attach(Entity child, Entity parent) noexcept
{
    Entity& first = parent.isValid() ? record(parent).firstChild : m_firstRoot;
    Entity& last = parent.isValid() ? record(parent).lastChild : m_lastRoot;

    EntityRecord& attached = record(child);
    attached.parent = parent;
    attached.previousSibling = last;
    attached.nextSibling = Entity{};

    if (last.isValid())
    {
        record(last).nextSibling = child;
    }
    else
    {
        first = child;
    }
    last = child;
}

void Scene::detach(Entity child) noexcept
{
    EntityRecord& detached = record(child);
    Entity& first = detached.parent.isValid() ? record(detached.parent).firstChild : m_firstRoot;
    Entity& last = detached.parent.isValid() ? record(detached.parent).lastChild : m_lastRoot;

    if (detached.previousSibling.isValid())
    {
        record(detached.previousSibling).nextSibling = detached.nextSibling;
    }
    else
    {
        first = detached.nextSibling;
    }
    if (detached.nextSibling.isValid())
    {
        record(detached.nextSibling).previousSibling = detached.previousSibling;
    }
    else
    {
        last = detached.previousSibling;
    }

    detached.parent = Entity{};
    detached.previousSibling = Entity{};
    detached.nextSibling = Entity{};
}

} // namespace devex::scene
