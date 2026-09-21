#include <devex/asset/AssetId.hpp>
#include <devex/core/Assert.hpp>
#include <devex/scene/DynamicComponent.hpp>
#include <devex/scene/EntityRef.hpp>

#include <algorithm>
#include <cstring>
#include <new>
#include <utility>

namespace devex::scene {

namespace detail {

// How a list field of one element type is built, destroyed, copied and edited.
struct DynamicListStorage
{
    std::size_t size = 0;
    std::size_t alignment = 1;
    const reflection::ListOps* operations = nullptr;
    void (*construct)(void* list) = nullptr;
    void (*destroy)(void* list) noexcept = nullptr;
    void (*copy)(void* destination, const void* source) = nullptr;
};

} // namespace detail

namespace {

using reflection::ValueKind;

template <typename Element>
[[nodiscard]] const detail::DynamicListStorage& listStorage() noexcept
{
    using List = std::vector<Element>;
    static const detail::DynamicListStorage storage{
        .size = sizeof(List),
        .alignment = alignof(List),
        .operations = &reflection::listOps<Element>(),
        .construct = [](void* list) { new (list) List(); },
        .destroy = [](void* list) noexcept { static_cast<List*>(list)->~List(); },
        .copy = [](void* destination, const void* source) {
            *static_cast<List*>(destination) = *static_cast<const List*>(source);
        },
    };
    return storage;
}

// The storage of a list of values of the kind; null for kinds that cannot be listed.
[[nodiscard]] const detail::DynamicListStorage* listStorageOf(ValueKind kind) noexcept
{
    switch (kind)
    {
    case ValueKind::Bool:
        return nullptr;
    case ValueKind::Int32:
        return &listStorage<std::int32_t>();
    case ValueKind::UInt32:
        return &listStorage<std::uint32_t>();
    case ValueKind::Float:
        return &listStorage<float>();
    case ValueKind::String:
        return &listStorage<std::string>();
    case ValueKind::Vec2:
        return &listStorage<math::Vec2>();
    case ValueKind::Vec3:
        return &listStorage<math::Vec3>();
    case ValueKind::Vec4:
        return &listStorage<math::Vec4>();
    case ValueKind::Quat:
        return &listStorage<math::Quat>();
    case ValueKind::Uuid:
        return &listStorage<core::Uuid>();
    case ValueKind::AssetId:
        return &listStorage<asset::AssetId>();
    case ValueKind::Enum:
        // Enumerations described at runtime are stored in four bytes.
        return &listStorage<std::uint32_t>();
    case ValueKind::Entity:
        return &listStorage<EntityRef>();
    }
    return nullptr;
}

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
    case ValueKind::Entity:
        return {sizeof(EntityRef), alignof(EntityRef)};
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
        const detail::DynamicListStorage* const list = field.list ? listStorageOf(field.kind) : nullptr;
        if (field.list && list == nullptr)
        {
            return core::makeError(core::ErrorCode::Unsupported, "{}.{} is a list of an unsupported type",
                                   layout->m_type.name, field.name);
        }
        const KindTraits traits = list != nullptr ? KindTraits{list->size, list->alignment}
                                                  : traitsOf(field.kind, enumSize);
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
        layout->m_lists.push_back(list);

        reflection::FieldInfo info{
            .name = field.name,
            .kind = field.kind,
            .assetType = field.assetType,
            .color = field.color,
            .angle = field.angle,
            .physicsLayer = field.physicsLayer,
            .audioGroup = field.audioGroup,
            .enumSize = enumSize,
            .access = [offset](void* component) -> void* { return static_cast<std::byte*>(component) + offset; },
            .list = list != nullptr ? list->operations : nullptr,
            .offset = offset,
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
    layout->m_type.size = layout->m_size;
    layout->m_type.alignment = layout->m_alignment;
    return layout;
}

void DynamicComponentLayout::construct(void* component) const
{
    std::memset(component, 0, m_size);
    for (std::size_t index = 0; index < m_kinds.size(); ++index)
    {
        void* const field = static_cast<std::byte*>(component) + m_offsets[index];
        if (m_lists[index] != nullptr)
        {
            m_lists[index]->construct(field);
        }
        else
        {
            constructField(m_kinds[index], field);
        }
    }
}

void DynamicComponentLayout::initialize(void* component) const
{
    if (m_initialize)
    {
        m_initialize(component);
    }
}

void DynamicComponentLayout::destroy(void* component) const noexcept
{
    for (std::size_t index = 0; index < m_kinds.size(); ++index)
    {
        void* const field = static_cast<std::byte*>(component) + m_offsets[index];
        if (m_lists[index] != nullptr)
        {
            m_lists[index]->destroy(field);
        }
        else
        {
            destroyField(m_kinds[index], field);
        }
    }
}

void DynamicComponentLayout::copy(void* destination, const void* source) const
{
    for (std::size_t index = 0; index < m_kinds.size(); ++index)
    {
        const std::size_t offset = m_offsets[index];
        void* const to = static_cast<std::byte*>(destination) + offset;
        const void* const from = static_cast<const std::byte*>(source) + offset;
        if (m_lists[index] != nullptr)
        {
            m_lists[index]->copy(to, from);
            continue;
        }
        const std::size_t size = traitsOf(m_kinds[index], m_type.fields[index].enumSize).size;
        copyField(m_kinds[index], to, from, size);
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
    m_layout->initialize(component);
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
