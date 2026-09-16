#pragma once

#include <devex/core/Assert.hpp>
#include <devex/scene/Entity.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace devex::scene {

// Sparse set mapping entities to dense storage indices, independent of the component type.
// Components of one type are stored contiguously; removing one moves the last into its slot.
class ComponentPoolBase
{
public:
    virtual ~ComponentPoolBase() = default;

    [[nodiscard]] bool contains(Entity entity) const noexcept
    {
        return denseIndexOf(entity) != absent;
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return m_entities.size();
    }

    // Entities owning a component, in storage order.
    [[nodiscard]] std::span<const Entity> entities() const noexcept
    {
        return m_entities;
    }

    virtual void remove(Entity entity) = 0;

protected:
    static constexpr std::uint32_t absent = std::numeric_limits<std::uint32_t>::max();

    [[nodiscard]] std::uint32_t denseIndexOf(Entity entity) const noexcept
    {
        if (entity.index >= m_sparse.size())
        {
            return absent;
        }
        const std::uint32_t dense = m_sparse[entity.index];
        return dense != absent && m_entities[dense] == entity ? dense : absent;
    }

    void insertEntity(Entity entity)
    {
        if (entity.index >= m_sparse.size())
        {
            m_sparse.resize(static_cast<std::size_t>(entity.index) + 1, absent);
        }
        m_sparse[entity.index] = static_cast<std::uint32_t>(m_entities.size());
        m_entities.push_back(entity);
    }

    // Returns the dense index that was freed, which now holds the former last entity.
    [[nodiscard]] std::uint32_t eraseEntity(Entity entity) noexcept
    {
        const std::uint32_t dense = denseIndexOf(entity);
        if (dense == absent)
        {
            return absent;
        }
        const Entity last = m_entities.back();
        m_entities[dense] = last;
        m_sparse[last.index] = dense;
        m_entities.pop_back();
        m_sparse[entity.index] = absent;
        return dense;
    }

private:
    std::vector<std::uint32_t> m_sparse;
    std::vector<Entity> m_entities;
};

template <typename T>
class ComponentPool final : public ComponentPoolBase
{
public:
    template <typename... Args>
    T& emplace(Entity entity, Args&&... args)
    {
        DEVEX_ASSERT_MSG(!contains(entity), "the entity already has this component");
        insertEntity(entity);
        return m_components.emplace_back(std::forward<Args>(args)...);
    }

    [[nodiscard]] T* find(Entity entity) noexcept
    {
        const std::uint32_t dense = denseIndexOf(entity);
        return dense == absent ? nullptr : &m_components[dense];
    }

    [[nodiscard]] const T* find(Entity entity) const noexcept
    {
        const std::uint32_t dense = denseIndexOf(entity);
        return dense == absent ? nullptr : &m_components[dense];
    }

    void remove(Entity entity) override
    {
        const std::uint32_t dense = eraseEntity(entity);
        if (dense == absent)
        {
            return;
        }
        if (dense + 1 != m_components.size())
        {
            m_components[dense] = std::move(m_components.back());
        }
        m_components.pop_back();
    }

private:
    std::vector<T> m_components;
};

} // namespace devex::scene
