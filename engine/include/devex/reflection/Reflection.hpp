#pragma once

#include <devex/core/Uuid.hpp>
#include <devex/math/Math.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
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
    // An enumeration with EnumNames, stored in one or four bytes and written by name.
    Enum,
};

[[nodiscard]] std::string_view toString(ValueKind kind) noexcept;

// Maps a C++ type to the value kind it is reflected as. Modules specialize it for their own
// value types.
template <typename T>
struct ValueTraits;

// Specialized with the names of an enumeration's values, which must be 0, 1, 2 and so on:
//     template <> struct EnumNames<Tonemapper> {
//         static constexpr std::array<std::string_view, 2> names{"agx", "none"};
//     };
template <typename T>
struct EnumNames;

template <typename T>
concept ReflectableEnum = std::is_enum_v<T> && (sizeof(T) == 1 || sizeof(T) == 4) && requires {
    { EnumNames<T>::names.size() } -> std::convertible_to<std::size_t>;
};

template <ReflectableEnum T>
struct ValueTraits<T>
{
    static constexpr ValueKind kind = ValueKind::Enum;
};

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
    // A Vec3 or Vec4 holding a linear color.
    bool color = false;
    // A float angle in radians, shown in degrees.
    bool angle = false;
    // A std::uint32_t index into the collision layers of the project, shown by name.
    bool physicsLayer = false;
};

struct FieldInfo
{
    std::string name;
    ValueKind kind = ValueKind::Bool;
    std::string assetType;
    bool color = false;
    bool angle = false;
    bool physicsLayer = false;
    // For enumerations: the name of each value, and the size of the stored value in bytes.
    std::vector<std::string_view> enumNames;
    std::uint8_t enumSize = 0;
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
        std::vector<std::string_view> enumNames;
        if constexpr (ReflectableEnum<Value>)
        {
            enumNames.assign(EnumNames<Value>::names.begin(), EnumNames<Value>::names.end());
        }
        m_info.fields.push_back({
            .name = std::move(name),
            .kind = ValueTraits<Value>::kind,
            .assetType = std::string(hints.assetType),
            .color = hints.color,
            .angle = hints.angle,
            .physicsLayer = hints.physicsLayer,
            .enumNames = std::move(enumNames),
            .enumSize = static_cast<std::uint8_t>(ReflectableEnum<Value> ? sizeof(Value) : 0),
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
// Reads or writes an enumeration field as the index of its value.
[[nodiscard]] std::uint32_t readEnumIndex(const FieldInfo& field, const void* address) noexcept;
void writeEnumIndex(const FieldInfo& field, void* address, std::uint32_t index) noexcept;

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
