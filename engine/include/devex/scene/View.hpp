#pragma once

#include <devex/scene/ComponentPool.hpp>
#include <devex/scene/Entity.hpp>

#include <array>
#include <cstddef>
#include <tuple>
#include <type_traits>

namespace devex::scene {

// Iterates over the entities that have all the listed components, yielding
// std::tuple<Entity, Components&...>. Only the smallest pool is walked. Adding or removing
// components of these types while iterating invalidates the view.
template <typename... Components>
class View
{
public:
    using Pools = std::tuple<ComponentPool<std::remove_const_t<Components>>*...>;

    explicit View(Pools pools) noexcept
        : m_pools(pools)
    {
        const auto all = std::apply(
            [](auto*... pool) {
                return std::array<const ComponentPoolBase*, sizeof...(Components)>{pool...};
            },
            m_pools);
        for (const ComponentPoolBase* pool : all)
        {
            // A type that was never added has no pool: no entity can match.
            if (pool == nullptr)
            {
                m_lead = nullptr;
                return;
            }
            if (m_lead == nullptr || pool->size() < m_lead->size())
            {
                m_lead = pool;
            }
        }
    }

    class Iterator
    {
    public:
        using value_type = std::tuple<Entity, Components&...>;

        Iterator(const View* view, std::size_t index) noexcept
            : m_view(view)
            , m_index(index)
        {
            skipIncompleteEntities();
        }

        [[nodiscard]] value_type operator*() const noexcept
        {
            const Entity entity = m_view->m_lead->entities()[m_index];
            return value_type(
                entity,
                *std::get<ComponentPool<std::remove_const_t<Components>>*>(m_view->m_pools)
                     ->find(entity)...);
        }

        Iterator& operator++() noexcept
        {
            ++m_index;
            skipIncompleteEntities();
            return *this;
        }

        [[nodiscard]] bool operator==(const Iterator& other) const noexcept
        {
            return m_index == other.m_index;
        }

    private:
        void skipIncompleteEntities() noexcept
        {
            const std::size_t count = m_view->leadSize();
            while (m_index < count && !m_view->hasAll(m_view->m_lead->entities()[m_index]))
            {
                ++m_index;
            }
        }

        const View* m_view;
        std::size_t m_index;
    };

    [[nodiscard]] Iterator begin() const noexcept
    {
        return Iterator(this, 0);
    }

    [[nodiscard]] Iterator end() const noexcept
    {
        return Iterator(this, leadSize());
    }

private:
    [[nodiscard]] std::size_t leadSize() const noexcept
    {
        return m_lead == nullptr ? 0 : m_lead->size();
    }

    [[nodiscard]] bool hasAll(Entity entity) const noexcept
    {
        return std::apply([entity](auto*... pool) { return (pool->contains(entity) && ...); },
                          m_pools);
    }

    Pools m_pools;
    const ComponentPoolBase* m_lead = nullptr;
};

} // namespace devex::scene
