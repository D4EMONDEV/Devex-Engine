#pragma once

#include <devex/core/Error.hpp>
#include <devex/reflection/Reflection.hpp>
#include <devex/scene/ComponentPool.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

// Component types described while the engine runs, such as the components of a game written in C#.
// The engine owns their memory, so that scenes, the inspector, prefabs and undo treat them exactly
// like the components declared in C++.
namespace devex::scene {

// A field of a component type described at runtime.
struct DynamicField
{
    std::string name;
    reflection::ValueKind kind = reflection::ValueKind::Float;
    // The asset type expected, a color, an angle or a collision layer; see reflection::FieldHints.
    std::string assetType;
    bool color = false;
    bool angle = false;
    bool physicsLayer = false;
    // For enumerations: the name of each value, in order.
    std::vector<std::string> enumNames;
};

// Where the fields of such a component live in the block of memory the engine allocates for it.
class DynamicComponentLayout
{
public:
    // Gives a new component the values its own language calls default, after the fields are built.
    using Initializer = std::function<void(void* component)>;

    // Fails when a field has no name, when two fields share one, or when a kind is unsupported.
    [[nodiscard]] static core::Result<std::shared_ptr<const DynamicComponentLayout>> create(
        std::string typeName, std::span<const DynamicField> fields, Initializer initialize = {});

    [[nodiscard]] const reflection::TypeInfo& type() const noexcept
    {
        return m_type;
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return m_size;
    }

    [[nodiscard]] std::size_t alignment() const noexcept
    {
        return m_alignment;
    }

    // The offset of each field in the block, in the order of type().fields.
    [[nodiscard]] std::span<const std::size_t> offsets() const noexcept
    {
        return m_offsets;
    }

    void construct(void* component) const;
    void destroy(void* component) const noexcept;
    void copy(void* destination, const void* source) const;

private:
    DynamicComponentLayout() = default;

    reflection::TypeInfo m_type;
    // The names of enumeration values, kept at stable addresses for the views in m_type.
    std::vector<std::unique_ptr<std::string>> m_enumNames;
    std::vector<std::size_t> m_offsets;
    std::vector<reflection::ValueKind> m_kinds;
    Initializer m_initialize;
    std::size_t m_size = 0;
    std::size_t m_alignment = 1;
};

// The components of one runtime-described type, stored like any other component pool.
class DynamicComponentPool final : public ComponentPoolBase
{
public:
    explicit DynamicComponentPool(std::shared_ptr<const DynamicComponentLayout> layout) noexcept;
    DynamicComponentPool(const DynamicComponentPool& other);
    DynamicComponentPool& operator=(const DynamicComponentPool&) = delete;
    ~DynamicComponentPool() override;

    [[nodiscard]] const DynamicComponentLayout& layout() const noexcept
    {
        return *m_layout;
    }

    // Adds a component with every field at its default value, or returns the one the entity has.
    [[nodiscard]] void* emplace(Entity entity);
    [[nodiscard]] void* find(Entity entity) noexcept;
    [[nodiscard]] const void* find(Entity entity) const noexcept;

    [[nodiscard]] std::unique_ptr<ComponentPoolBase> clone() const override;
    [[nodiscard]] const void* moduleAnchor() const noexcept override;
    void remove(Entity entity) override;

private:
    [[nodiscard]] void* at(std::size_t dense) noexcept;
    [[nodiscard]] const void* at(std::size_t dense) const noexcept;
    void reserve(std::size_t components);

    std::shared_ptr<const DynamicComponentLayout> m_layout;
    // Raw storage of size() * capacity bytes, aligned for the layout.
    std::byte* m_components = nullptr;
    std::size_t m_count = 0;
    std::size_t m_capacity = 0;
};

} // namespace devex::scene
