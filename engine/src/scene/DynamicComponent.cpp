#include <devex/asset/AssetId.hpp>
#include <devex/core/Assert.hpp>
#include <devex/scene/DynamicComponent.hpp>

#include <algorithm>
#include <cstring>
#include <new>
#include <utility>

namespace devex::scene {
namespace {

using reflection::ValueKind;

struct KindTraits
{
    std::size_t size = 0;
    std::size_t alignment = 1;
};

[[nodiscard]] KindTraits traitsOf(ValueKind kind, std::uint8_t enumSize) noexcept
{
    switch (kind)
    {
    case ValueKind::Bool:
        return {sizeof(bool), alignof(bool)};
    case ValueKind::Int32:
        return {sizeof(std::int32_t), alignof(std::int32_t)};
    case ValueKind::UInt32:
        return {sizeof(std::uint32_t), alignof(std::uint32_t)};
    case ValueKind::Float:
        return {sizeof(float), alignof(float)};
    case ValueKind::String:
        return {sizeof(std::string), alignof(std::string)};
    case ValueKind::Vec2:
        return {sizeof(math::Vec2), alignof(math::Vec2)};
    case ValueKind::Vec3:
        return {sizeof(math::Vec3), alignof(math::Vec3)};
    case ValueKind::Vec4:
        return {sizeof(math::Vec4), alignof(math::Vec4)};
    case ValueKind::Quat:
        return {sizeof(math::Quat), alignof(math::Quat)};
    case ValueKind::Uuid:
        return {sizeof(core::Uuid), alignof(core::Uuid)};
    case ValueKind::AssetId:
        return {sizeof(asset::AssetId), alignof(asset::AssetId)};
    case ValueKind::Enum:
        return {enumSize, enumSize};
    }
    return {};
}

void constructField(ValueKind kind, void* field) noexcept
{
    if (kind == ValueKind::String)
    {
        new (field) std::string();
        return;
    }
    if (kind == ValueKind::Quat)
    {
        new (field) math::Quat(1.0f, 0.0f, 0.0f, 0.0f);
        return;
    }
    // Every other kind reads as zero: false, 0, the nil UUID, an invalid asset.
}

void destroyField(ValueKind kind, void* field) noexcept
{
    if (kind == ValueKind::String)
    {
        static_cast<std::string*>(field)->~basic_string();
    }
}

void copyField(ValueKind kind, void* destination, const void* source, std::size_t size)
{
    if (kind == ValueKind::String)
    {
        *static_cast<std::string*>(destination) = *static_cast<const std::string*>(source);
        return;
    }
    std::memcpy(destination, source, size);
}

} // namespace

core::Result<std::shared_ptr<const DynamicComponentLayout>> DynamicComponentLayout::create(
    std::string typeName, std::span<const DynamicField> fields, Initializer initialize)
{
    if (typeName.empty())
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "a component type needs a name");
    }
    std::shared_ptr<DynamicComponentLayout> layout(new DynamicComponentLayout());
    layout->m_type.name = std::move(typeName);
    layout->m_initialize = std::move(initialize);
    for (const DynamicField& field : fields)
    {
        if (field.name.empty())
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "a field of {} has no name", layout->m_type.name);
        }
        if (layout->m_type.findField(field.name) != nullptr)
        {
            return core::makeError(core::ErrorCode::AlreadyExists, "{} has two fields named '{}'", layout->m_type.name,
                                   field.name);
        }
        const auto enumSize = static_cast<std::uint8_t>(field.kind == ValueKind::Enum ? 4 : 0);
        const KindTraits traits = traitsOf(field.kind, enumSize);
        if (traits.size == 0)
        {
            return core::makeError(core::ErrorCode::Unsupported, "{}.{} has an unsupported type", layout->m_type.name,
                                   field.name);
        }
        if (field.kind == ValueKind::Enum && field.enumNames.empty())
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "the enumeration {}.{} has no values",
                                   layout->m_type.name, field.name);
        }

        const std::size_t offset = (layout->m_size + traits.alignment - 1) / traits.alignment * traits.alignment;
        layout->m_size = offset + traits.size;
        layout->m_alignment = std::max(layout->m_alignment, traits.alignment);
        layout->m_offsets.push_back(offset);
        layout->m_kinds.push_back(field.kind);

        reflection::FieldInfo info{
            .name = field.name,
            .kind = field.kind,
            .assetType = field.assetType,
            .color = field.color,
            .angle = field.angle,
            .physicsLayer = field.physicsLayer,
            .enumSize = enumSize,
            .access = [offset](void* component) -> void* { return static_cast<std::byte*>(component) + offset; },
        };
        // The names outlive the layout, which owns them.
        layout->m_type.fields.push_back(std::move(info));
    }

    // Enumeration names are views into strings the layout keeps at a stable address.
    for (std::size_t index = 0; index < fields.size(); ++index)
    {
        for (const std::string& name : fields[index].enumNames)
        {
            layout->m_enumNames.push_back(std::make_unique<std::string>(name));
            layout->m_type.fields[index].enumNames.push_back(*layout->m_enumNames.back());
        }
    }
    layout->m_size = (layout->m_size + layout->m_alignment - 1) / layout->m_alignment * layout->m_alignment;
    return layout;
}

void DynamicComponentLayout::construct(void* component) const
{
    std::memset(component, 0, m_size);
    for (std::size_t index = 0; index < m_kinds.size(); ++index)
    {
        constructField(m_kinds[index], static_cast<std::byte*>(component) + m_offsets[index]);
    }
    if (m_initialize)
    {
        m_initialize(component);
    }
}

void DynamicComponentLayout::destroy(void* component) const noexcept
{
    for (std::size_t index = 0; index < m_kinds.size(); ++index)
    {
        destroyField(m_kinds[index], static_cast<std::byte*>(component) + m_offsets[index]);
    }
}

void DynamicComponentLayout::copy(void* destination, const void* source) const
{
    for (std::size_t index = 0; index < m_kinds.size(); ++index)
    {
        const std::size_t offset = m_offsets[index];
        const std::size_t next = index + 1 < m_offsets.size() ? m_offsets[index + 1] : m_size;
        copyField(m_kinds[index], static_cast<std::byte*>(destination) + offset,
                  static_cast<const std::byte*>(source) + offset, next - offset);
    }
}

DynamicComponentPool::DynamicComponentPool(std::shared_ptr<const DynamicComponentLayout> layout) noexcept
    : m_layout(std::move(layout))
{
}

DynamicComponentPool::DynamicComponentPool(const DynamicComponentPool& other)
    : ComponentPoolBase(other)
    , m_layout(other.m_layout)
{
    reserve(other.m_count);
    for (std::size_t index = 0; index < other.m_count; ++index)
    {
        m_layout->construct(at(index));
        m_layout->copy(at(index), other.at(index));
    }
    m_count = other.m_count;
}

DynamicComponentPool::~DynamicComponentPool()
{
    for (std::size_t index = 0; index < m_count; ++index)
    {
        m_layout->destroy(at(index));
    }
    ::operator delete(m_components, std::align_val_t{m_layout->alignment()});
}

void* DynamicComponentPool::at(std::size_t dense) noexcept
{
    return m_components + dense * m_layout->size();
}

const void* DynamicComponentPool::at(std::size_t dense) const noexcept
{
    return m_components + dense * m_layout->size();
}

void DynamicComponentPool::reserve(std::size_t components)
{
    if (components <= m_capacity)
    {
        return;
    }
    const std::size_t capacity = std::max(components, std::max<std::size_t>(m_capacity * 2, 8));
    auto* const memory = static_cast<std::byte*>(
        ::operator new(capacity * m_layout->size(), std::align_val_t{m_layout->alignment()}));
    for (std::size_t index = 0; index < m_count; ++index)
    {
        void* const destination = memory + index * m_layout->size();
        m_layout->construct(destination);
        m_layout->copy(destination, at(index));
        m_layout->destroy(at(index));
    }
    ::operator delete(m_components, std::align_val_t{m_layout->alignment()});
    m_components = memory;
    m_capacity = capacity;
}

void* DynamicComponentPool::emplace(Entity entity)
{
    if (void* const existing = find(entity))
    {
        return existing;
    }
    reserve(m_count + 1);
    insertEntity(entity);
    void* const component = at(m_count);
    m_layout->construct(component);
    ++m_count;
    return component;
}

void* DynamicComponentPool::find(Entity entity) noexcept
{
    const std::uint32_t dense = denseIndexOf(entity);
    return dense == absent ? nullptr : at(dense);
}

const void* DynamicComponentPool::find(Entity entity) const noexcept
{
    const std::uint32_t dense = denseIndexOf(entity);
    return dense == absent ? nullptr : at(dense);
}

std::unique_ptr<ComponentPoolBase> DynamicComponentPool::clone() const
{
    return std::make_unique<DynamicComponentPool>(*this);
}

const void* DynamicComponentPool::moduleAnchor() const noexcept
{
    // The engine owns these pools, whatever module described the type.
    static const char anchor = 0;
    return &anchor;
}

void DynamicComponentPool::remove(Entity entity)
{
    const std::uint32_t dense = eraseEntity(entity);
    if (dense == absent)
    {
        return;
    }
    if (dense + 1 != m_count)
    {
        m_layout->copy(at(dense), at(m_count - 1));
    }
    m_layout->destroy(at(m_count - 1));
    --m_count;
}

} // namespace devex::scene
