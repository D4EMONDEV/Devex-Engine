#pragma once

#include <devex/core/Uuid.hpp>
#include <devex/math/Math.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Describes the fields of engine and game types so that generic code (serialization, the editor
// inspector, script bindings) can read and write them without knowing the types.
namespace devex::reflection {

enum class ValueKind : std::uint8_t
{
    Bool,
    Int32,
    UInt32,
    Float,
    String,
    Vec2,
    Vec3,
    Vec4,
    Quat,
    Uuid,
    // asset::AssetId, whose ValueTraits specialization lives in the Asset module.
    AssetId,
};

[[nodiscard]] std::string_view toString(ValueKind kind) noexcept;

// Maps a C++ type to the value kind it is reflected as. Modules specialize it for their own
// value types.
template <typename T>
struct ValueTraits;

template <typename T>
concept ReflectableValue = requires {
    { ValueTraits<T>::kind } -> std::convertible_to<ValueKind>;
};

template <>
struct ValueTraits<bool>
{
    static constexpr ValueKind kind = ValueKind::Bool;
};

template <>
struct ValueTraits<std::int32_t>
{
    static constexpr ValueKind kind = ValueKind::Int32;
};

template <>
struct ValueTraits<std::uint32_t>
{
    static constexpr ValueKind kind = ValueKind::UInt32;
};

template <>
struct ValueTraits<float>
{
    static constexpr ValueKind kind = ValueKind::Float;
};

template <>
struct ValueTraits<std::string>
{
    static constexpr ValueKind kind = ValueKind::String;
};

template <>
struct ValueTraits<math::Vec2>
{
    static constexpr ValueKind kind = ValueKind::Vec2;
};

template <>
struct ValueTraits<math::Vec3>
{
    static constexpr ValueKind kind = ValueKind::Vec3;
};

template <>
struct ValueTraits<math::Vec4>
{
    static constexpr ValueKind kind = ValueKind::Vec4;
};

template <>
struct ValueTraits<math::Quat>
{
    static constexpr ValueKind kind = ValueKind::Quat;
};

template <>
struct ValueTraits<core::Uuid>
{
    static constexpr ValueKind kind = ValueKind::Uuid;
};

// Optional details about a field, for tools.
struct FieldHints
{
    // For asset references: the asset type expected, such as "mesh". Empty accepts any type.
    std::string_view assetType;
};

struct FieldInfo
{
    std::string name;
    ValueKind kind = ValueKind::Bool;
    std::string assetType;
    // Returns the address of the field inside an object of the reflected type.
    std::function<void*(void* object)> access;

    // The field has the C++ type associated with its kind, such as math::Vec3 for Vec3.
    [[nodiscard]] void* address(void* object) const
    {
        return access(object);
    }

    [[nodiscard]] const void* address(const void* object) const
    {
        return access(const_cast<void*>(object));
    }
};

struct TypeInfo
{
    std::string name;
    std::vector<FieldInfo> fields;

    [[nodiscard]] const FieldInfo* findField(std::string_view fieldName) const noexcept
    {
        for (const FieldInfo& field : fields)
        {
            if (field.name == fieldName)
            {
                return &field;
            }
        }
        return nullptr;
    }
};

template <typename T>
class TypeBuilder
{
public:
    TypeBuilder& setName(std::string name)
    {
        m_info.name = std::move(name);
        return *this;
    }

    template <ReflectableValue Value>
    TypeBuilder& field(std::string name, Value T::* member, FieldHints hints = {})
    {
        m_info.fields.push_back({
            .name = std::move(name),
            .kind = ValueTraits<Value>::kind,
            .assetType = std::string(hints.assetType),
            .access = [member](void* object) -> void* {
                return &(static_cast<T*>(object)->*member);
            },
        });
        return *this;
    }

    [[nodiscard]] TypeInfo build() &&
    {
        return std::move(m_info);
    }

private:
    TypeInfo m_info;
};

// Returns the description registered with DEVEX_REFLECT for T.
template <typename T>
[[nodiscard]] const TypeInfo& typeInfo()
{
    static const TypeInfo info = [] {
        TypeBuilder<T> builder;
        devexReflect(builder);
        return std::move(builder).build();
    }();
    return info;
}

} // namespace devex::reflection

// Declares the reflection of Type, in the namespace of Type, typically in its header.
#define DEVEX_DECLARE_REFLECTION(Type)                                                             \
    void devexReflect(::devex::reflection::TypeBuilder<Type>& type)

// Defines the reflection of Type, in the namespace of Type, followed by a body that registers
// its fields on `type`:
//     DEVEX_REFLECT(Transform) { type.field("position", &Transform::position); }
#define DEVEX_REFLECT(Type)                                                                        \
    static void devexReflectFields(::devex::reflection::TypeBuilder<Type>& type);                  \
    void devexReflect(::devex::reflection::TypeBuilder<Type>& type)                                \
    {                                                                                              \
        type.setName(#Type);                                                                       \
        devexReflectFields(type);                                                                  \
    }                                                                                              \
    static void devexReflectFields(::devex::reflection::TypeBuilder<Type>& type)
