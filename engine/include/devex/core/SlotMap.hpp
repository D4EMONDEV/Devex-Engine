#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace devex::core {

// Refers to a value stored in a SlotMap. The generation makes a handle stale once its value is
// removed, even when the slot is reused by another value.
template <typename Tag>
struct Handle
{
    static constexpr std::uint32_t invalidIndex = std::numeric_limits<std::uint32_t>::max();

    std::uint32_t index = invalidIndex;
    std::uint32_t generation = 0;

    [[nodiscard]] constexpr bool isValid() const noexcept
    {
        return index != invalidIndex;
    }

    bool operator==(const Handle&) const = default;
};

// Stores values in reusable slots addressed by generational handles, so that engine resources
// can be referred to without pointers that dangle.
template <typename T, typename Tag = T>
class SlotMap
{
public:
    using HandleType = Handle<Tag>;

    [[nodiscard]] HandleType insert(T value)
    {
        std::uint32_t index = 0;
        if (m_freeSlots.empty())
        {
            index = static_cast<std::uint32_t>(m_slots.size());
            m_slots.emplace_back();
        }
        else
        {
            index = m_freeSlots.back();
            m_freeSlots.pop_back();
        }
        Slot& slot = m_slots[index];
        slot.value.emplace(std::move(value));
        ++m_size;
        return {index, slot.generation};
    }

    [[nodiscard]] T* find(HandleType handle) noexcept
    {
        return isLive(handle) ? &*m_slots[handle.index].value : nullptr;
    }

    [[nodiscard]] const T* find(HandleType handle) const noexcept
    {
        return isLive(handle) ? &*m_slots[handle.index].value : nullptr;
    }

    [[nodiscard]] bool contains(HandleType handle) const noexcept
    {
        return isLive(handle);
    }

    // Removes and returns the value, or nothing when the handle is stale.
    std::optional<T> remove(HandleType handle)
    {
        if (!isLive(handle))
        {
            return std::nullopt;
        }
        Slot& slot = m_slots[handle.index];
        std::optional<T> removed = std::move(slot.value);
        slot.value.reset();
        ++slot.generation;
        m_freeSlots.push_back(handle.index);
        --m_size;
        return removed;
    }

    // Calls function(handle, value) for every stored value, in slot order.
    template <typename Function>
    void forEach(Function&& function) const
    {
        for (std::uint32_t index = 0; index < m_slots.size(); ++index)
        {
            if (const Slot& slot = m_slots[index]; slot.value.has_value())
            {
                function(HandleType{index, slot.generation}, *slot.value);
            }
        }
    }

    // One past the largest slot index ever used: every handle index is below it.
    [[nodiscard]] std::size_t slotCount() const noexcept
    {
        return m_slots.size();
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return m_size;
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return m_size == 0;
    }

private:
    struct Slot
    {
        std::optional<T> value;
        std::uint32_t generation = 0;
    };

    [[nodiscard]] bool isLive(HandleType handle) const noexcept
    {
        return handle.index < m_slots.size() && m_slots[handle.index].value.has_value() &&
               m_slots[handle.index].generation == handle.generation;
    }

    std::vector<Slot> m_slots;
    std::vector<std::uint32_t> m_freeSlots;
    std::size_t m_size = 0;
};

} // namespace devex::core
