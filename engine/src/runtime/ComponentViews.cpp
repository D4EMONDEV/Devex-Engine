#include <devex/core/Hash.hpp>
#include <devex/runtime/ComponentViews.hpp>

#include <cctype>
#include <format>
#include <iterator>

namespace devex::runtime {
namespace {

using reflection::FieldInfo;
using reflection::TypeInfo;
using reflection::ValueKind;

// The name of the C# enumeration generated for an enumeration field.
[[nodiscard]] std::string enumTypeName(const TypeInfo& type, const FieldInfo& field)
{
    return type.name + pascalCase(field.name);
}

// The C# type of a value stored in place, or empty for strings and entities, reached through calls.
[[nodiscard]] std::string valueType(const TypeInfo& type, const FieldInfo& field)
{
    switch (field.kind)
    {
    case ValueKind::Bool:
        return "bool";
    case ValueKind::Int32:
        return "int";
    case ValueKind::UInt32:
        return "uint";
    case ValueKind::Float:
        return "float";
    case ValueKind::Vec2:
        return "Vec2";
    case ValueKind::Vec3:
        return "Vec3";
    case ValueKind::Vec4:
        return "Vec4";
    case ValueKind::Quat:
        return "Quat";
    case ValueKind::Uuid:
        return "Uuid";
    case ValueKind::AssetId:
        return "AssetId";
    case ValueKind::Enum:
        return enumTypeName(type, field);
    case ValueKind::String:
    case ValueKind::Entity:
        return {};
    }
    return {};
}

[[nodiscard]] bool isBindable(const TypeInfo& type)
{
    if (type.name == "Transform" || type.size == 0)
    {
        return false;
    }
    for (const FieldInfo& field : type.fields)
    {
        if (field.offset == FieldInfo::unknownOffset)
        {
            return false;
        }
        // Views name enumerations by their values, and only know one- and four-byte ones.
        if (field.kind == ValueKind::Enum && (field.enumNames.empty() || (field.enumSize != 1 && field.enumSize != 4)))
        {
            return false;
        }
    }
    return true;
}

void appendEnum(std::string& out, const TypeInfo& type, const FieldInfo& field)
{
    std::format_to(std::back_inserter(out), "/// <summary>The values of {}.{}.</summary>\npublic enum {} : {}\n{{\n",
                   type.name, field.name, enumTypeName(type, field), field.enumSize == 1 ? "byte" : "int");
    for (std::size_t index = 0; index < field.enumNames.size(); ++index)
    {
        std::string value = pascalCase(field.enumNames[index]);
        if (value.empty() || std::isdigit(static_cast<unsigned char>(value.front())) != 0)
        {
            value.insert(value.begin(), '_');
        }
        std::format_to(std::back_inserter(out), "    {} = {},\n", value, index);
    }
    out += "}\n\n";
}

void appendField(std::string& out, const TypeInfo& type, const FieldInfo& field, std::size_t index)
{
    std::string name = pascalCase(field.name);
    if (name == type.name)
    {
        // A member cannot be named after its type in C#.
        name += "Value";
    }
    std::format_to(std::back_inserter(out), "    /// <summary>The {} field.</summary>\n", field.name);
    if (field.list != nullptr)
    {
        const std::string list = field.kind == ValueKind::String   ? std::string("NativeStringList")
                                 : field.kind == ValueKind::Entity ? std::string("NativeEntityList")
                                                                   : std::format("NativeList<{}>", valueType(type, field));
        std::format_to(std::back_inserter(out), "    public {} {} => new(_component, ViewSupport.IndexOf<{}>(), {});\n",
                       list, name, type.name, index);
        return;
    }
    if (field.kind == ValueKind::String)
    {
        std::format_to(std::back_inserter(out),
                       "    public string {0}\n    {{\n        get => ViewSupport.ReadString(_component + {1});\n"
                       "        set => ViewSupport.WriteString(_component + {1}, value);\n    }}\n",
                       name, field.offset);
        return;
    }
    if (field.kind == ValueKind::Entity)
    {
        std::format_to(std::back_inserter(out),
                       "    public Entity {0}\n    {{\n        get => ViewSupport.ReadEntity(_component + {1});\n"
                       "        set => ViewSupport.WriteEntity(_component + {1}, value);\n    }}\n",
                       name, field.offset);
        return;
    }
    const std::string csharpType = valueType(type, field);
    std::format_to(std::back_inserter(out), "    public ref {0} {1} => ref *({0}*)(_component + {2});\n", csharpType, name,
                   field.offset);
}

void appendView(std::string& out, const TypeInfo& type)
{
    for (const FieldInfo& field : type.fields)
    {
        if (field.kind == ValueKind::Enum)
        {
            appendEnum(out, type, field);
        }
    }
    std::format_to(std::back_inserter(out),
                   "/// <summary>The {0} component of an entity, read and written in place: "
                   "<c>entity.Get&lt;{0}&gt;()</c>.</summary>\n"
                   "public readonly unsafe ref struct {0} : IComponentView<{0}>\n{{\n"
                   "    private readonly byte* _component;\n\n"
                   "    private {0}(void* component) => _component = (byte*)component;\n\n"
                   "    public static string TypeName => \"{0}\";\n\n"
                   "    public static ulong LayoutHash => 0x{1:016x}UL;\n\n"
                   "    public static {0} At(void* component) => new(component);\n",
                   type.name, componentLayoutHash(type));
    for (std::size_t index = 0; index < type.fields.size(); ++index)
    {
        out += '\n';
        appendField(out, type, type.fields[index], index);
    }
    out += "}\n\n";
}

} // namespace

std::uint64_t componentLayoutHash(const TypeInfo& type)
{
    std::string description = std::format("{}|{}", type.name, type.size);
    for (const FieldInfo& field : type.fields)
    {
        std::format_to(std::back_inserter(description), "|{}:{}:{}:{}:{}", field.name, reflection::toString(field.kind),
                       field.offset, field.list != nullptr, field.enumSize);
        for (const std::string_view value : field.enumNames)
        {
            std::format_to(std::back_inserter(description), ",{}", value);
        }
    }
    return core::hash64(description);
}

std::string generateComponentViews(std::span<const TypeInfo* const> types, std::string_view csharpNamespace,
                                   std::string_view origin)
{
    std::string out = std::format("// <auto-generated>\n// Views of the components of {}, generated by Devex from their "
                                  "reflection: do not edit.\n// </auto-generated>\n\n#nullable enable\n\nusing "
                                  "Devex;\nusing Devex.Internal;\n\n",
                                  origin);
    if (!csharpNamespace.empty())
    {
        std::format_to(std::back_inserter(out), "namespace {};\n\n", csharpNamespace);
    }
    for (const TypeInfo* const type : types)
    {
        if (type != nullptr && isBindable(*type))
        {
            appendView(out, *type);
        }
    }
    return out;
}

std::string pascalCase(std::string_view name)
{
    std::string result;
    result.reserve(name.size());
    bool upper = true;
    for (const char character : name)
    {
        if (character == '_' || character == ' ' || character == '-')
        {
            upper = true;
            continue;
        }
        result += upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(character))) : character;
        upper = false;
    }
    return result;
}

} // namespace devex::runtime
