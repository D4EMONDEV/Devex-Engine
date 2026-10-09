#include <devex/asset/ShaderGraphData.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <format>
#include <functional>
#include <set>
#include <utility>

namespace devex::asset {
namespace {

using Type = ShaderParameterType;

// ---- Built-ins ----

// A built-in a node reads or writes in a function of a kind.
struct Builtin
{
    ShaderKind kind;
    ShaderFunction function;
    std::string_view name;
    Type type;
    bool readable;
    bool writable;
    // What an Input node reads, and what an Output node writes, when it is not the name itself.
    std::string_view read = {};
    std::string_view write = {};
    // The label of the port of the output node.
    std::string_view label = {};
};

using K = ShaderKind;
using F = ShaderFunction;

constexpr std::array builtins{
    // Spatial, vertex().
    Builtin{K::Spatial, F::Vertex, "VERTEX", Type::Float3, true, true, {}, {}, "Vertex"},
    Builtin{K::Spatial, F::Vertex, "NORMAL", Type::Float3, true, true, {}, {}, "Normal"},
    Builtin{K::Spatial, F::Vertex, "TANGENT", Type::Float3, true, false},
    Builtin{K::Spatial, F::Vertex, "BINORMAL", Type::Float3, true, false},
    Builtin{K::Spatial, F::Vertex, "UV", Type::Float2, true, true, {}, {}, "UV"},
    Builtin{K::Spatial, F::Vertex, "COLOR", Type::Float4, true, true, {}, {}, "Color"},
    Builtin{K::Spatial, F::Vertex, "TIME", Type::Float, true, false},
    Builtin{K::Spatial, F::Vertex, "VERTEX_ID", Type::Int, true, false, "int(VERTEX_ID)"},
    Builtin{K::Spatial, F::Vertex, "CAMERA_POSITION_WORLD", Type::Float3, true, false},
    Builtin{K::Spatial, F::Vertex, "VIEWPORT_SIZE", Type::Float2, true, false},
    // Spatial, fragment().
    Builtin{K::Spatial, F::Fragment, "ALBEDO", Type::Float3, false, true, {}, {}, "Albedo"},
    Builtin{K::Spatial, F::Fragment, "ALPHA", Type::Float, false, true, {}, {}, "Alpha"},
    Builtin{K::Spatial, F::Fragment, "METALLIC", Type::Float, false, true, {}, {}, "Metallic"},
    Builtin{K::Spatial, F::Fragment, "ROUGHNESS", Type::Float, false, true, {}, {}, "Roughness"},
    Builtin{K::Spatial, F::Fragment, "SPECULAR", Type::Float, false, true, {}, {}, "Specular"},
    Builtin{K::Spatial, F::Fragment, "EMISSION", Type::Float3, false, true, {}, {}, "Emission"},
    Builtin{K::Spatial, F::Fragment, "AO", Type::Float, false, true, {}, {}, "AO"},
    Builtin{K::Spatial, F::Fragment, "NORMAL_MAP", Type::Float3, false, true, {}, {}, "Normal Map"},
    Builtin{K::Spatial, F::Fragment, "NORMAL_MAP_DEPTH", Type::Float, false, true, {}, {}, "Normal Map Depth"},
    Builtin{K::Spatial, F::Fragment, "ALPHA_SCISSOR_THRESHOLD", Type::Float, false, true, {}, {}, "Alpha Scissor"},
    Builtin{K::Spatial, F::Fragment, "NORMAL", Type::Float3, true, true, {}, {}, "Normal"},
    Builtin{K::Spatial, F::Fragment, "VERTEX", Type::Float3, true, false},
    Builtin{K::Spatial, F::Fragment, "TANGENT", Type::Float3, true, false},
    Builtin{K::Spatial, F::Fragment, "BINORMAL", Type::Float3, true, false},
    Builtin{K::Spatial, F::Fragment, "UV", Type::Float2, true, false},
    Builtin{K::Spatial, F::Fragment, "COLOR", Type::Float4, true, false},
    Builtin{K::Spatial, F::Fragment, "VIEW", Type::Float3, true, false},
    Builtin{K::Spatial, F::Fragment, "TIME", Type::Float, true, false},
    Builtin{K::Spatial, F::Fragment, "FRAGCOORD", Type::Float4, true, false},
    Builtin{K::Spatial, F::Fragment, "SCREEN_UV", Type::Float2, true, false},
    Builtin{K::Spatial, F::Fragment, "FRONT_FACING", Type::Bool, true, false},
    Builtin{K::Spatial, F::Fragment, "CAMERA_POSITION_WORLD", Type::Float3, true, false},
    Builtin{K::Spatial, F::Fragment, "VIEWPORT_SIZE", Type::Float2, true, false},
    // Spatial, light(): what each light adds.
    Builtin{K::Spatial, F::Light, "DIFFUSE_LIGHT", Type::Float3, false, true, {}, {}, "Diffuse Light"},
    Builtin{K::Spatial, F::Light, "SPECULAR_LIGHT", Type::Float3, false, true, {}, {}, "Specular Light"},
    Builtin{K::Spatial, F::Light, "NORMAL", Type::Float3, true, false},
    Builtin{K::Spatial, F::Light, "VIEW", Type::Float3, true, false},
    Builtin{K::Spatial, F::Light, "LIGHT", Type::Float3, true, false},
    Builtin{K::Spatial, F::Light, "LIGHT_COLOR", Type::Float3, true, false},
    Builtin{K::Spatial, F::Light, "ATTENUATION", Type::Float, true, false},
    Builtin{K::Spatial, F::Light, "LIGHT_IS_DIRECTIONAL", Type::Bool, true, false},
    Builtin{K::Spatial, F::Light, "ALBEDO", Type::Float3, true, false},
    Builtin{K::Spatial, F::Light, "METALLIC", Type::Float, true, false},
    Builtin{K::Spatial, F::Light, "ROUGHNESS", Type::Float, true, false},
    Builtin{K::Spatial, F::Light, "UV", Type::Float2, true, false},
    Builtin{K::Spatial, F::Light, "TIME", Type::Float, true, false},
    // Canvas items.
    Builtin{K::CanvasItem, F::Vertex, "VERTEX", Type::Float2, true, true, {}, {}, "Vertex"},
    Builtin{K::CanvasItem, F::Vertex, "UV", Type::Float2, true, true, {}, {}, "UV"},
    Builtin{K::CanvasItem, F::Vertex, "COLOR", Type::Float4, true, true, {}, {}, "Color"},
    Builtin{K::CanvasItem, F::Vertex, "TIME", Type::Float, true, false},
    Builtin{K::CanvasItem, F::Vertex, "INSTANCE_ID", Type::Int, true, false, "int(INSTANCE_ID)"},
    Builtin{K::CanvasItem, F::Vertex, "VIEWPORT_SIZE", Type::Float2, true, false},
    Builtin{K::CanvasItem, F::Fragment, "COLOR", Type::Float3, false, true, {}, "COLOR.rgb", "Color"},
    Builtin{K::CanvasItem, F::Fragment, "ALPHA", Type::Float, false, true, {}, "COLOR.a", "Alpha"},
    Builtin{K::CanvasItem, F::Fragment, "NORMAL_MAP", Type::Float3, true, true, {}, {}, "Normal Map"},
    Builtin{K::CanvasItem, F::Fragment, "NORMAL_MAP_DEPTH", Type::Float, false, true, {}, {}, "Normal Map Depth"},
    Builtin{K::CanvasItem, F::Fragment, "UV", Type::Float2, true, false},
    Builtin{K::CanvasItem, F::Fragment, "COLOR ", Type::Float4, true, false, "COLOR"},
    Builtin{K::CanvasItem, F::Fragment, "TEXTURE", Type::Texture, true, false},
    Builtin{K::CanvasItem, F::Fragment, "NORMAL_TEXTURE", Type::Texture, true, false},
    Builtin{K::CanvasItem, F::Fragment, "TEXTURE_PIXEL_SIZE", Type::Float2, true, false},
    Builtin{K::CanvasItem, F::Fragment, "TIME", Type::Float, true, false},
    Builtin{K::CanvasItem, F::Fragment, "SCREEN_UV", Type::Float2, true, false},
    Builtin{K::CanvasItem, F::Fragment, "FRAGCOORD", Type::Float4, true, false},
    Builtin{K::CanvasItem, F::Fragment, "INSTANCE_ID", Type::Int, true, false, "int(INSTANCE_ID)"},
    Builtin{K::CanvasItem, F::Fragment, "VIEWPORT_SIZE", Type::Float2, true, false},
    // Particles.
    Builtin{K::Particles, F::Vertex, "VERTEX", Type::Float3, true, true, {}, {}, "Vertex"},
    Builtin{K::Particles, F::Vertex, "COLOR", Type::Float4, true, true, {}, {}, "Color"},
    Builtin{K::Particles, F::Vertex, "UV", Type::Float2, true, true, {}, {}, "UV"},
    Builtin{K::Particles, F::Vertex, "INSTANCE_ID", Type::Int, true, false, "int(INSTANCE_ID)"},
    Builtin{K::Particles, F::Vertex, "PARTICLE_SIZE", Type::Float, true, false},
    Builtin{K::Particles, F::Vertex, "TIME", Type::Float, true, false},
    Builtin{K::Particles, F::Vertex, "CAMERA_POSITION_WORLD", Type::Float3, true, false},
    Builtin{K::Particles, F::Fragment, "COLOR", Type::Float3, false, true, {}, "COLOR.rgb", "Color"},
    Builtin{K::Particles, F::Fragment, "ALPHA", Type::Float, false, true, {}, "COLOR.a", "Alpha"},
    Builtin{K::Particles, F::Fragment, "COLOR ", Type::Float4, true, false, "COLOR"},
    Builtin{K::Particles, F::Fragment, "UV", Type::Float2, true, false},
    Builtin{K::Particles, F::Fragment, "VERTEX", Type::Float3, true, false},
    Builtin{K::Particles, F::Fragment, "TEXTURE", Type::Texture, true, false},
    Builtin{K::Particles, F::Fragment, "INSTANCE_ID", Type::Int, true, false, "int(INSTANCE_ID)"},
    Builtin{K::Particles, F::Fragment, "PARTICLE_SIZE", Type::Float, true, false},
    Builtin{K::Particles, F::Fragment, "TIME", Type::Float, true, false},
    Builtin{K::Particles, F::Fragment, "SCREEN_UV", Type::Float2, true, false},
    Builtin{K::Particles, F::Fragment, "FRAGCOORD", Type::Float4, true, false},
    // Skies.
    Builtin{K::Sky, F::Sky, "COLOR", Type::Float3, false, true, {}, {}, "Color"},
    Builtin{K::Sky, F::Sky, "EYEDIR", Type::Float3, true, false},
    Builtin{K::Sky, F::Sky, "POSITION", Type::Float3, true, false},
    Builtin{K::Sky, F::Sky, "SKY_COORDS", Type::Float2, true, false},
    Builtin{K::Sky, F::Sky, "TIME", Type::Float, true, false},
    Builtin{K::Sky, F::Sky, "SCREEN_UV", Type::Float2, true, false},
    Builtin{K::Sky, F::Sky, "FRAGCOORD", Type::Float4, true, false},
    Builtin{K::Sky, F::Sky, "LIGHT0_ENABLED", Type::Bool, true, false},
    Builtin{K::Sky, F::Sky, "LIGHT0_DIRECTION", Type::Float3, true, false},
    Builtin{K::Sky, F::Sky, "LIGHT0_COLOR", Type::Float3, true, false},
    Builtin{K::Sky, F::Sky, "LIGHT0_ENERGY", Type::Float, true, false},
    Builtin{K::Sky, F::Sky, "AT_CUBEMAP_PASS", Type::Bool, true, false},
};

// The name an Input node shows and keeps: that of the built-in, without the space that tells two
// built-ins of the same name apart.
[[nodiscard]] std::string_view shownName(std::string_view name) noexcept
{
    while (!name.empty() && name.back() == ' ')
    {
        name.remove_suffix(1);
    }
    return name;
}

[[nodiscard]] const Builtin* readableBuiltin(ShaderKind kind, ShaderFunction function, std::string_view name) noexcept
{
    for (const Builtin& builtin : builtins)
    {
        if (builtin.kind == kind && builtin.function == function && builtin.readable && shownName(builtin.name) == name)
        {
            return &builtin;
        }
    }
    return nullptr;
}

// ---- Types ----

[[nodiscard]] std::string_view typeName(Type type) noexcept
{
    return toString(type);
}

// The types a node edits, as its "type" setting names them.
[[nodiscard]] std::optional<Type> settingType(std::string_view text) noexcept
{
    if (text == "float")
    {
        return Type::Float;
    }
    if (text == "int")
    {
        return Type::Int;
    }
    if (text == "bool")
    {
        return Type::Bool;
    }
    if (text == "vec2")
    {
        return Type::Float2;
    }
    if (text == "vec3" || text == "color")
    {
        return Type::Float3;
    }
    if (text == "vec4")
    {
        return Type::Float4;
    }
    if (text == "texture")
    {
        return Type::Texture;
    }
    return std::nullopt;
}

[[nodiscard]] Type typeOf(const ShaderGraphNode& node, Type fallback) noexcept
{
    return settingType(node.text("type")).value_or(fallback);
}

[[nodiscard]] std::string floatLiteral(float value)
{
    if (!std::isfinite(value))
    {
        return "0.0";
    }
    std::string text = std::format("{}", value);
    if (text.find_first_of(".e") == std::string::npos)
    {
        text += ".0";
    }
    return text;
}

[[nodiscard]] std::string literal(Type type, math::Vec4 value)
{
    switch (type)
    {
    case Type::Float:
        return floatLiteral(value.x);
    case Type::Int:
        return std::format("{}", static_cast<int>(std::round(value.x)));
    case Type::Bool:
        return value.x != 0.0f ? "true" : "false";
    case Type::Float2:
        return std::format("float2({}, {})", floatLiteral(value.x), floatLiteral(value.y));
    case Type::Float3:
        return std::format("float3({}, {}, {})", floatLiteral(value.x), floatLiteral(value.y), floatLiteral(value.z));
    case Type::Float4:
        return std::format("float4({}, {}, {}, {})", floatLiteral(value.x), floatLiteral(value.y), floatLiteral(value.z),
                           floatLiteral(value.w));
    case Type::Texture:
        break;
    }
    return "0.0";
}

[[nodiscard]] bool isVector(Type type) noexcept
{
    return type == Type::Float2 || type == Type::Float3 || type == Type::Float4;
}

// An expression of one type as another, as Godot converts ports: a number fills a vector, a vector
// gives its first components, a shorter vector is padded. Nothing for textures and numbers.
[[nodiscard]] std::optional<std::string> convert(std::string expression, Type from, Type to)
{
    if (from == to)
    {
        return expression;
    }
    if (from == Type::Texture || to == Type::Texture)
    {
        return std::nullopt;
    }
    const auto asFloat = [&]() -> std::string {
        switch (from)
        {
        case Type::Int:
            return std::format("float({})", expression);
        case Type::Bool:
            return std::format("({} ? 1.0 : 0.0)", expression);
        case Type::Float2:
        case Type::Float3:
        case Type::Float4:
            return std::format("({}).x", expression);
        default:
            return expression;
        }
    };
    switch (to)
    {
    case Type::Float:
        return asFloat();
    case Type::Int:
        return from == Type::Bool ? std::format("({} ? 1 : 0)", expression) : std::format("int({})", asFloat());
    case Type::Bool:
        return std::format("({} != 0.0)", asFloat());
    case Type::Float2:
        if (from == Type::Float3 || from == Type::Float4)
        {
            return std::format("({}).xy", expression);
        }
        return std::format("float2({})", asFloat());
    case Type::Float3:
        if (from == Type::Float4)
        {
            return std::format("({}).xyz", expression);
        }
        if (from == Type::Float2)
        {
            return std::format("float3({}, 0.0)", expression);
        }
        return std::format("float3({})", asFloat());
    case Type::Float4:
        if (from == Type::Float3)
        {
            return std::format("float4({}, 1.0)", expression);
        }
        if (from == Type::Float2)
        {
            return std::format("float4({}, 0.0, 1.0)", expression);
        }
        return std::format("float4({})", asFloat());
    case Type::Texture:
        break;
    }
    return std::nullopt;
}

[[nodiscard]] bool isIdentifier(std::string_view name) noexcept
{
    if (name.empty() || !(std::isalpha(static_cast<unsigned char>(name.front())) != 0 || name.front() == '_'))
    {
        return false;
    }
    return std::ranges::all_of(name, [](char character) {
        return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_';
    });
}

// The ports an Expression node declares: "float a, float3 b".
[[nodiscard]] std::vector<ShaderGraphPort> declaredPorts(std::string_view text)
{
    std::vector<ShaderGraphPort> ports;
    std::size_t at = 0;
    while (at <= text.size())
    {
        const std::size_t end = std::min(text.find(',', at), text.size());
        std::string_view part = text.substr(at, end - at);
        while (!part.empty() && std::isspace(static_cast<unsigned char>(part.front())) != 0)
        {
            part.remove_prefix(1);
        }
        while (!part.empty() && std::isspace(static_cast<unsigned char>(part.back())) != 0)
        {
            part.remove_suffix(1);
        }
        if (const std::size_t space = part.find(' '); space != std::string_view::npos)
        {
            std::string_view name = part.substr(space + 1);
            while (!name.empty() && name.front() == ' ')
            {
                name.remove_prefix(1);
            }
            const std::optional<Type> type = parseShaderParameterType(part.substr(0, space));
            if (type && *type != Type::Texture && isIdentifier(name))
            {
                ports.push_back({.name = std::string(name), .type = *type});
            }
        }
        at = end + 1;
    }
    return ports;
}

const std::vector<std::string> numberTypes{"float", "int", "vec2", "vec3", "vec4"};
const std::vector<std::string> floatTypes{"float", "vec2", "vec3", "vec4"};
const std::vector<std::string> vectorTypes{"vec2", "vec3", "vec4"};

[[nodiscard]] std::vector<std::string> operatorsOf(Type type)
{
    std::vector<std::string> names{"add", "subtract", "multiply", "divide", "remainder", "power", "min", "max", "atan2"};
    if (type == Type::Int)
    {
        names = {"add", "subtract", "multiply", "divide", "remainder", "min", "max"};
    }
    if (type == Type::Float3)
    {
        names.emplace_back("cross");
        names.emplace_back("reflect");
    }
    return names;
}

[[nodiscard]] std::vector<std::string> functionsFor(Type type)
{
    std::vector<std::string> names{"sin",  "cos",  "tan",     "asin",         "acos",  "atan", "abs",      "sign",
                                   "floor", "ceil", "round",  "fract",        "sqrt",  "inverse_sqrt", "exp", "exp2",
                                   "log",  "log2", "negate",  "one_minus",    "saturate", "reciprocal", "degrees", "radians"};
    if (isVector(type))
    {
        names.emplace_back("normalize");
    }
    return names;
}

[[nodiscard]] std::string titleCase(std::string_view name)
{
    std::string text;
    bool start = true;
    for (const char character : name)
    {
        if (character == '_')
        {
            text += ' ';
            start = true;
            continue;
        }
        text += start ? static_cast<char>(std::toupper(static_cast<unsigned char>(character))) : character;
        start = false;
    }
    return text;
}

constexpr std::array<std::string_view, 4> axes{"x", "y", "z", "w"};

} // namespace

// ---- Functions ----

std::string_view toString(ShaderFunction function) noexcept
{
    switch (function)
    {
    case ShaderFunction::Vertex:
        return "vertex";
    case ShaderFunction::Fragment:
        return "fragment";
    case ShaderFunction::Light:
        return "light";
    case ShaderFunction::Sky:
        return "sky";
    }
    return "unknown";
}

std::optional<ShaderFunction> parseShaderFunction(std::string_view text) noexcept
{
    for (const ShaderFunction function : {ShaderFunction::Vertex, ShaderFunction::Fragment, ShaderFunction::Light, ShaderFunction::Sky})
    {
        if (toString(function) == text)
        {
            return function;
        }
    }
    return std::nullopt;
}

std::span<const ShaderFunction> functionsOf(ShaderKind kind) noexcept
{
    static constexpr std::array<ShaderFunction, 3> spatial{ShaderFunction::Vertex, ShaderFunction::Fragment, ShaderFunction::Light};
    static constexpr std::array<ShaderFunction, 2> flat{ShaderFunction::Vertex, ShaderFunction::Fragment};
    static constexpr std::array<ShaderFunction, 1> sky{ShaderFunction::Sky};
    switch (kind)
    {
    case ShaderKind::Spatial:
        return spatial;
    case ShaderKind::CanvasItem:
    case ShaderKind::Particles:
        return flat;
    case ShaderKind::Sky:
        return sky;
    }
    return {};
}

// ---- Nodes and graphs ----

const ShaderGraphSetting* ShaderGraphNode::find(std::string_view name) const noexcept
{
    for (const ShaderGraphSetting& setting : settings)
    {
        if (setting.name == name)
        {
            return &setting;
        }
    }
    return nullptr;
}

std::string_view ShaderGraphNode::text(std::string_view name, std::string_view fallback) const noexcept
{
    const ShaderGraphSetting* const setting = find(name);
    return setting != nullptr ? std::string_view(setting->text) : fallback;
}

math::Vec4 ShaderGraphNode::vector(std::string_view name, math::Vec4 fallback) const noexcept
{
    const ShaderGraphSetting* const setting = find(name);
    return setting != nullptr ? setting->value : fallback;
}

void ShaderGraphNode::setText(std::string_view name, std::string value)
{
    for (ShaderGraphSetting& setting : settings)
    {
        if (setting.name == name)
        {
            setting.text = std::move(value);
            return;
        }
    }
    settings.push_back({.name = std::string(name), .text = std::move(value)});
}

void ShaderGraphNode::setVector(std::string_view name, math::Vec4 value)
{
    for (ShaderGraphSetting& setting : settings)
    {
        if (setting.name == name)
        {
            setting.value = value;
            return;
        }
    }
    settings.push_back({.name = std::string(name), .value = value});
}

math::Vec4 ShaderGraphNode::input(std::size_t port, math::Vec4 fallback) const noexcept
{
    return port < inputs.size() ? inputs[port] : fallback;
}

ShaderGraphNode* ShaderGraphData::find(std::uint32_t id) noexcept
{
    for (ShaderGraphNode& node : nodes)
    {
        if (node.id == id)
        {
            return &node;
        }
    }
    return nullptr;
}

const ShaderGraphNode* ShaderGraphData::find(std::uint32_t id) const noexcept
{
    for (const ShaderGraphNode& node : nodes)
    {
        if (node.id == id)
        {
            return &node;
        }
    }
    return nullptr;
}

std::uint32_t ShaderGraphData::nextId() const noexcept
{
    std::uint32_t highest = 0;
    for (const ShaderGraphNode& node : nodes)
    {
        highest = std::max(highest, node.id);
    }
    return highest + 1;
}

const ShaderGraphLink* ShaderGraphData::linkInto(std::uint32_t node, std::uint32_t port) const noexcept
{
    for (const ShaderGraphLink& link : links)
    {
        if (link.toNode == node && link.toPort == port)
        {
            return &link;
        }
    }
    return nullptr;
}

bool ShaderGraphData::feeds(std::uint32_t to, std::uint32_t from) const
{
    // Whether `from` is reached by following the links out of `to`.
    std::vector<std::uint32_t> pending{to};
    std::set<std::uint32_t> seen;
    while (!pending.empty())
    {
        const std::uint32_t current = pending.back();
        pending.pop_back();
        if (current == from)
        {
            return true;
        }
        if (!seen.insert(current).second)
        {
            continue;
        }
        for (const ShaderGraphLink& link : links)
        {
            if (link.fromNode == current)
            {
                pending.push_back(link.toNode);
            }
        }
    }
    return false;
}

bool ShaderGraphData::connect(ShaderGraphLink link)
{
    const ShaderGraphNode* const from = find(link.fromNode);
    const ShaderGraphNode* const to = find(link.toNode);
    if (from == nullptr || to == nullptr || from == to || from->function != to->function ||
        link.fromPort >= outputsOf(*from, kind).size() || link.toPort >= inputsOf(*to, kind).size() || feeds(link.toNode, link.fromNode))
    {
        return false;
    }
    std::erase_if(links, [&](const ShaderGraphLink& other) { return other.toNode == link.toNode && other.toPort == link.toPort; });
    links.push_back(link);
    return true;
}

void ShaderGraphData::removeNode(std::uint32_t id)
{
    std::erase_if(nodes, [&](const ShaderGraphNode& node) { return node.id == id; });
    std::erase_if(links, [&](const ShaderGraphLink& link) { return link.fromNode == id || link.toNode == id; });
}

const ShaderGraphNode* ShaderGraphData::outputOf(ShaderFunction function) const noexcept
{
    for (const ShaderGraphNode& node : nodes)
    {
        if (node.type == "output" && node.function == function)
        {
            return &node;
        }
    }
    return nullptr;
}

// ---- The catalog ----

std::span<const ShaderNodeEntry> shaderNodeCatalog()
{
    static const std::vector<ShaderNodeEntry> entries{
        {"Input", "Input", "input", "A built-in of the function: UV, TIME, NORMAL, VIEW...", {}},
        {"Constant", "Float", "constant", "A number", {{"type", "float"}}},
        {"Constant", "Int", "constant", "A whole number", {{"type", "int"}}},
        {"Constant", "Bool", "constant", "True or false", {{"type", "bool"}}},
        {"Constant", "Vector2", "constant", "Two numbers", {{"type", "vec2"}}},
        {"Constant", "Vector3", "constant", "Three numbers", {{"type", "vec3"}}},
        {"Constant", "Vector4", "constant", "Four numbers", {{"type", "vec4"}}},
        {"Constant", "Color", "constant", "A colour and its alpha", {{"type", "color"}}},
        {"Parameter", "Float Parameter", "parameter", "A number each material gives", {{"type", "float"}}},
        {"Parameter", "Int Parameter", "parameter", "A whole number each material gives", {{"type", "int"}}},
        {"Parameter", "Bool Parameter", "parameter", "True or false, as each material says", {{"type", "bool"}}},
        {"Parameter", "Vector2 Parameter", "parameter", "Two numbers each material gives", {{"type", "vec2"}}},
        {"Parameter", "Vector3 Parameter", "parameter", "Three numbers each material gives", {{"type", "vec3"}}},
        {"Parameter", "Vector4 Parameter", "parameter", "Four numbers each material gives", {{"type", "vec4"}}},
        {"Parameter", "Color Parameter", "parameter", "A colour each material gives", {{"type", "color"}}},
        {"Parameter", "Texture Parameter", "parameter", "A texture each material gives, for Texture nodes", {{"type", "texture"}}},
        {"Math", "Operator", "operator", "Adds, subtracts, multiplies... two numbers", {{"type", "float"}, {"op", "multiply"}}},
        {"Math", "Vector Operator", "operator", "Adds, subtracts, multiplies... two vectors", {{"type", "vec3"}, {"op", "multiply"}}},
        {"Math", "Function", "function", "sin, abs, floor, sqrt... of a number", {{"type", "float"}, {"function", "sin"}}},
        {"Math", "Vector Function", "function", "normalize, abs, fract... of a vector", {{"type", "vec3"}, {"function", "normalize"}}},
        {"Math", "Remap", "remap", "Takes a number from one range to another", {}},
        {"Vector", "Dot Product", "dot", "How much two vectors point the same way", {{"type", "vec3"}}},
        {"Vector", "Cross Product", "cross", "The vector at right angles to two others", {}},
        {"Vector", "Length", "length", "The length of a vector", {{"type", "vec3"}}},
        {"Vector", "Distance", "distance", "How far two points are", {{"type", "vec3"}}},
        {"Vector", "Compose Vector", "compose", "A vector from numbers", {{"type", "vec3"}}},
        {"Vector", "Decompose Vector", "decompose", "The numbers of a vector", {{"type", "vec3"}}},
        {"Interpolation", "Mix", "mix", "Blends two values by a weight", {{"type", "vec3"}}},
        {"Interpolation", "Clamp", "clamp", "Keeps a value between two others", {{"type", "float"}}},
        {"Interpolation", "Step", "step", "0 below an edge, 1 from it", {{"type", "float"}}},
        {"Interpolation", "Smoothstep", "smoothstep", "A smooth step between two edges", {{"type", "float"}}},
        {"Texture", "Texture", "texture", "Samples a texture: a parameter, or the texture of the sprite", {}},
        {"Special", "Fresnel", "fresnel", "Brighter where the surface turns away from the viewer", {}},
        {"Special", "Noise", "noise", "Smooth random values across space, from 0 to 1", {}},
        {"Special", "Expression", "expression", "A few lines of Slang, with inputs and outputs of its own",
         {{"inputs", "float a, float b"}, {"outputs", "float result"}, {"code", "result = a + b;"}}},
    };
    return entries;
}

bool offers(const ShaderNodeEntry& entry, ShaderKind kind, ShaderFunction function)
{
    if (entry.type == "fresnel")
    {
        return kind == ShaderKind::Spatial && (function == ShaderFunction::Fragment || function == ShaderFunction::Light);
    }
    if (entry.type == "input")
    {
        return !inputBuiltins(kind, function).empty();
    }
    return entry.type != "output";
}

ShaderGraphNode makeShaderNode(const ShaderNodeEntry& entry, ShaderKind kind, ShaderFunction function, std::uint32_t id,
                               math::Vec2 position)
{
    ShaderGraphNode node{.id = id, .type = std::string(entry.type), .function = function, .position = position};
    for (const auto& [name, value] : entry.presets)
    {
        node.setText(name, std::string(value));
    }
    if (node.type == "input")
    {
        const std::vector<std::string> names = inputBuiltins(kind, function);
        const bool hasUv = std::ranges::find(names, "UV") != names.end();
        node.setText("name", hasUv ? "UV" : names.empty() ? "TIME" : names.front());
    }
    if (node.type == "constant" && node.text("type") == "color")
    {
        node.setVector("value", math::Vec4{1.0f});
    }
    if (node.type == "parameter")
    {
        node.setText("name", std::format("{}_{}", node.text("type") == "texture" ? "texture" : "value", id));
        if (node.text("type") == "color")
        {
            node.setVector("value", math::Vec4{1.0f});
        }
        node.setVector("range", math::Vec4{0.0f, 1.0f, 0.0f, 0.0f});
    }
    if (node.type == "noise")
    {
        node.setVector("octaves", math::Vec4{3.0f, 0.0f, 0.0f, 0.0f});
    }
    for (const ShaderGraphPort& port : inputsOf(node, kind))
    {
        node.inputs.push_back(port.fallback);
    }
    return node;
}

std::vector<std::string> inputBuiltins(ShaderKind kind, ShaderFunction function)
{
    std::vector<std::string> names;
    for (const Builtin& builtin : builtins)
    {
        if (builtin.kind == kind && builtin.function == function && builtin.readable)
        {
            names.emplace_back(shownName(builtin.name));
        }
    }
    return names;
}

std::vector<ShaderGraphPort> inputsOf(const ShaderGraphNode& node, ShaderKind kind)
{
    const std::string_view type = node.type;
    const Type value = typeOf(node, Type::Float);
    const auto same = [&](std::initializer_list<std::pair<std::string_view, float>> names) {
        std::vector<ShaderGraphPort> ports;
        for (const auto& [name, fallback] : names)
        {
            ports.push_back({.name = std::string(name), .type = value, .fallback = math::Vec4{fallback}});
        }
        return ports;
    };
    if (type == "output")
    {
        std::vector<ShaderGraphPort> ports;
        for (const Builtin& builtin : builtins)
        {
            if (builtin.kind == kind && builtin.function == node.function && builtin.writable)
            {
                ports.push_back({.name = std::string(builtin.label), .type = builtin.type});
            }
        }
        return ports;
    }
    if (type == "operator")
    {
        const std::string_view op = node.text("op", "add");
        const bool scaling = op == "multiply" || op == "divide" || op == "power";
        return same({{"a", 0.0f}, {"b", scaling ? 1.0f : 0.0f}});
    }
    if (type == "function")
    {
        return same({{"x", 0.0f}});
    }
    if (type == "dot" || type == "distance")
    {
        return same({{"a", 0.0f}, {"b", 0.0f}});
    }
    if (type == "length")
    {
        return same({{"vector", 0.0f}});
    }
    if (type == "cross")
    {
        return {{.name = "a", .type = Type::Float3}, {.name = "b", .type = Type::Float3}};
    }
    if (type == "mix")
    {
        std::vector<ShaderGraphPort> ports = same({{"a", 0.0f}, {"b", 1.0f}});
        ports.push_back({.name = "weight", .type = Type::Float, .fallback = math::Vec4{0.5f}});
        return ports;
    }
    if (type == "clamp")
    {
        return same({{"value", 0.0f}, {"min", 0.0f}, {"max", 1.0f}});
    }
    if (type == "step")
    {
        return same({{"edge", 0.5f}, {"x", 0.0f}});
    }
    if (type == "smoothstep")
    {
        return same({{"edge0", 0.0f}, {"edge1", 1.0f}, {"x", 0.5f}});
    }
    if (type == "remap")
    {
        return {{.name = "value", .type = Type::Float},
                {.name = "in min", .type = Type::Float},
                {.name = "in max", .type = Type::Float, .fallback = math::Vec4{1.0f}},
                {.name = "out min", .type = Type::Float},
                {.name = "out max", .type = Type::Float, .fallback = math::Vec4{1.0f}}};
    }
    if (type == "compose")
    {
        std::vector<ShaderGraphPort> ports;
        for (std::uint32_t axis = 0; axis < componentCount(typeOf(node, Type::Float3)); ++axis)
        {
            ports.push_back({.name = std::string(axes[axis]), .type = Type::Float});
        }
        return ports;
    }
    if (type == "decompose")
    {
        return {{.name = "vector", .type = typeOf(node, Type::Float3)}};
    }
    if (type == "texture")
    {
        const bool sprite = kind == ShaderKind::CanvasItem || kind == ShaderKind::Particles;
        return {{.name = "texture", .type = Type::Texture, .builtin = sprite && node.function == ShaderFunction::Fragment ? "TEXTURE" : ""},
                {.name = "uv", .type = Type::Float2, .builtin = "UV"},
                {.name = "lod", .type = Type::Float}};
    }
    if (type == "fresnel")
    {
        return {{.name = "normal", .type = Type::Float3, .builtin = "NORMAL"},
                {.name = "view", .type = Type::Float3, .builtin = "VIEW"},
                {.name = "power", .type = Type::Float, .fallback = math::Vec4{1.0f}}};
    }
    if (type == "noise")
    {
        return {{.name = "position", .type = Type::Float3, .builtin = kind == ShaderKind::Spatial ? "VERTEX" : kind == ShaderKind::Sky ? "EYEDIR" : "UV"},
                {.name = "scale", .type = Type::Float, .fallback = math::Vec4{4.0f}}};
    }
    if (type == "expression")
    {
        return declaredPorts(node.text("inputs"));
    }
    return {};
}

std::vector<ShaderGraphPort> outputsOf(const ShaderGraphNode& node, ShaderKind kind)
{
    const std::string_view type = node.type;
    const Type value = typeOf(node, Type::Float);
    if (type == "input")
    {
        const Builtin* const builtin = readableBuiltin(kind, node.function, node.text("name"));
        return {{.name = std::string(node.text("name")), .type = builtin != nullptr ? builtin->type : Type::Float}};
    }
    if (type == "constant" || type == "parameter")
    {
        if (node.text("type") == "color")
        {
            return {{.name = "color", .type = Type::Float3}, {.name = "alpha", .type = Type::Float}};
        }
        return {{.name = std::string(type == "parameter" && value == Type::Texture ? "texture" : "value"), .type = value}};
    }
    if (type == "operator" || type == "function" || type == "mix" || type == "clamp" || type == "step" || type == "smoothstep")
    {
        return {{.name = "result", .type = value}};
    }
    if (type == "dot" || type == "length" || type == "distance" || type == "remap" || type == "fresnel" || type == "noise")
    {
        return {{.name = "result", .type = Type::Float}};
    }
    if (type == "cross")
    {
        return {{.name = "result", .type = Type::Float3}};
    }
    if (type == "compose")
    {
        return {{.name = "vector", .type = typeOf(node, Type::Float3)}};
    }
    if (type == "decompose")
    {
        std::vector<ShaderGraphPort> ports;
        for (std::uint32_t axis = 0; axis < componentCount(typeOf(node, Type::Float3)); ++axis)
        {
            ports.push_back({.name = std::string(axes[axis]), .type = Type::Float});
        }
        return ports;
    }
    if (type == "texture")
    {
        return {{.name = "rgb", .type = Type::Float3}, {.name = "alpha", .type = Type::Float}, {.name = "rgba", .type = Type::Float4}};
    }
    if (type == "expression")
    {
        return declaredPorts(node.text("outputs"));
    }
    return {};
}

std::string nodeTitle(const ShaderGraphNode& node)
{
    const std::string_view type = node.type;
    if (type == "output")
    {
        return std::format("Output: {}", titleCase(toString(node.function)));
    }
    if (type == "input")
    {
        return std::format("Input: {}", node.text("name"));
    }
    if (type == "parameter")
    {
        return std::format("{}: {}", node.vector("instance").x != 0.0f ? "Instance Parameter" : "Parameter", node.text("name"));
    }
    if (type == "constant")
    {
        return std::format("{} Constant", titleCase(node.text("type", "float")));
    }
    if (type == "operator")
    {
        return titleCase(node.text("op", "add"));
    }
    if (type == "function")
    {
        return titleCase(node.text("function", "sin"));
    }
    if (type == "smoothstep")
    {
        return "Smoothstep";
    }
    if (type == "compose" || type == "decompose")
    {
        return std::format("{} {}", titleCase(type), titleCase(node.text("type", "vec3")));
    }
    return titleCase(type);
}

std::string_view nodeCategory(const ShaderGraphNode& node)
{
    if (node.type == "output")
    {
        return "Output";
    }
    for (const ShaderNodeEntry& entry : shaderNodeCatalog())
    {
        if (entry.type == node.type)
        {
            return entry.category;
        }
    }
    return "Special";
}

std::vector<ShaderSettingInfo> settingsOf(const ShaderGraphNode& node, ShaderKind kind)
{
    const std::string_view type = node.type;
    const Type value = typeOf(node, Type::Float);
    std::vector<ShaderSettingInfo> settings;
    const auto choice = [&](std::string name, std::string label, std::vector<std::string> options, std::string tooltip = {}) {
        settings.push_back({.name = std::move(name), .label = std::move(label), .kind = ShaderSettingKind::Choice, .options = std::move(options),
                            .tooltip = std::move(tooltip)});
    };
    if (type == "input")
    {
        choice("name", "Built-in", inputBuiltins(kind, node.function), "What the node reads in this function");
    }
    else if (type == "constant" || type == "parameter")
    {
        if (type == "parameter")
        {
            settings.push_back({.name = "name", .label = "Name", .kind = ShaderSettingKind::Name, .tooltip = "The name materials give it"});
        }
        std::vector<std::string> types{"float", "int", "bool", "vec2", "vec3", "vec4", "color"};
        if (type == "parameter")
        {
            types.emplace_back("texture");
        }
        choice("type", "Type", types);
        const std::string_view typeName = node.text("type", "float");
        if (typeName == "texture")
        {
            choice("hint", "Empty Texture", {"white", "black", "normal"}, "What it samples while a material gives no texture");
        }
        else if (typeName == "color")
        {
            settings.push_back({.name = "value", .label = type == "parameter" ? "Default" : "Value", .kind = ShaderSettingKind::Color});
        }
        else if (typeName == "bool")
        {
            settings.push_back({.name = "value", .label = type == "parameter" ? "Default" : "Value", .kind = ShaderSettingKind::Toggle});
        }
        else
        {
            settings.push_back({.name = "value",
                                .label = type == "parameter" ? "Default" : "Value",
                                .kind = ShaderSettingKind::Numbers,
                                .count = componentCount(value)});
        }
        // Godot's instance qualifier: each object gives its own value, in its renderer.
        if (type == "parameter" && typeName != "texture" && (kind == ShaderKind::Spatial || kind == ShaderKind::CanvasItem))
        {
            settings.push_back({.name = "instance", .label = "Per Instance", .kind = ShaderSettingKind::Toggle,
                                .tooltip = "Each object drawn with the material gives its own value, in its renderer; the "
                                           "default is the value of the others"});
        }
        if (type == "parameter" && (typeName == "float" || typeName == "int"))
        {
            choice("hint", "Hint", {"none", "range"}, "A range shows a slider in the inspector of materials");
            if (node.text("hint") == "range")
            {
                settings.push_back({.name = "range", .label = "Range", .kind = ShaderSettingKind::Numbers, .count = 3,
                                    .tooltip = "The lowest value, the highest, and the step (0 for any)"});
            }
        }
    }
    else if (type == "operator")
    {
        choice("type", "Type", numberTypes);
        choice("op", "Operator", operatorsOf(value));
    }
    else if (type == "function")
    {
        choice("type", "Type", floatTypes);
        choice("function", "Function", functionsFor(value));
    }
    else if (type == "dot" || type == "length" || type == "distance" || type == "compose" || type == "decompose")
    {
        choice("type", "Type", vectorTypes);
    }
    else if (type == "mix" || type == "clamp" || type == "step" || type == "smoothstep")
    {
        choice("type", "Type", floatTypes);
    }
    else if (type == "fresnel")
    {
        settings.push_back({.name = "invert", .label = "Invert", .kind = ShaderSettingKind::Toggle, .tooltip = "Brighter where the surface faces the viewer"});
    }
    else if (type == "noise")
    {
        settings.push_back({.name = "octaves", .label = "Octaves", .kind = ShaderSettingKind::Numbers, .tooltip = "Layers of finer detail, 1 to 6"});
    }
    else if (type == "expression")
    {
        settings.push_back({.name = "inputs", .label = "Inputs", .kind = ShaderSettingKind::Name, .tooltip = "Typed names: float a, float3 b"});
        settings.push_back({.name = "outputs", .label = "Outputs", .kind = ShaderSettingKind::Name, .tooltip = "Typed names the code sets: float result"});
        settings.push_back({.name = "code", .label = "Code", .kind = ShaderSettingKind::Code, .tooltip = "Slang that sets the outputs from the inputs"});
    }
    return settings;
}

ShaderGraphData makeShaderGraph(ShaderKind kind)
{
    ShaderGraphData graph{.kind = kind};
    for (const ShaderFunction function : functionsOf(kind))
    {
        graph.nodes.push_back({.id = graph.nextId(), .type = "output", .function = function, .position = {400.0f, 0.0f}});
        for (const ShaderGraphPort& port : inputsOf(graph.nodes.back(), kind))
        {
            graph.nodes.back().inputs.push_back(port.fallback);
        }
    }
    return graph;
}

// ---- Code ----

namespace {

class Writer
{
public:
    Writer(const ShaderGraphData& graph, GeneratedShader& result)
        : m_graph(graph)
        , m_result(result)
    {
    }

    void line(std::string text, std::uint32_t node = 0)
    {
        // A line of code is one line of the text, so that the compiler's lines lead back to nodes.
        std::size_t at = 0;
        while (true)
        {
            const std::size_t end = text.find('\n', at);
            m_result.code += text.substr(at, end == std::string::npos ? std::string::npos : end - at);
            m_result.code += '\n';
            m_result.lineNodes.push_back(node);
            if (end == std::string::npos)
            {
                break;
            }
            at = end + 1;
        }
    }

    void error(std::uint32_t node, std::string message, bool isError = true)
    {
        m_result.diagnostics.push_back({.message = std::move(message), .error = isError, .node = node});
    }

    void write(std::string_view name)
    {
        const ShaderKind kind = m_graph.kind;
        line(std::format("// Generated by Devex from the shader graph {}: what it draws, as code.", name));
        line(std::format("shader_type {};", toString(kind)));
        if (!m_graph.renderModes.empty())
        {
            std::string modes;
            for (const std::string& mode : m_graph.renderModes)
            {
                modes += (modes.empty() ? "" : ", ") + mode;
            }
            line(std::format("render_mode {};", modes));
        }
        line("");
        writeParameters();
        if (std::ranges::any_of(m_graph.nodes, [](const ShaderGraphNode& node) { return node.type == "noise"; }))
        {
            writeNoise();
        }
        for (const ShaderFunction function : functionsOf(kind))
        {
            if (const ShaderGraphNode* const output = m_graph.outputOf(function))
            {
                writeFunction(function, *output);
            }
        }
    }

private:
    void writeParameters()
    {
        std::set<std::string, std::less<>> names;
        for (const ShaderGraphNode& node : m_graph.nodes)
        {
            if (node.type != "parameter")
            {
                continue;
            }
            const std::string name(node.text("name"));
            if (!isIdentifier(name))
            {
                error(node.id, std::format("'{}' is not a name a parameter can take", name));
                continue;
            }
            if (!names.insert(name).second)
            {
                error(node.id, std::format("two parameters are named '{}'", name));
                continue;
            }
            const std::string_view type = node.text("type", "float");
            const math::Vec4 value = node.vector("value");
            // Per instance, where the kind and the type allow it.
            const bool instance = node.vector("instance").x != 0.0f && type != "texture" &&
                                  (m_graph.kind == ShaderKind::Spatial || m_graph.kind == ShaderKind::CanvasItem);
            std::string declaration;
            if (type == "texture")
            {
                const std::string_view hint = node.text("hint", "white");
                declaration = std::format("uniform sampler2D {} : {};", name,
                                          hint == "black" ? "hint_default_black" : hint == "normal" ? "hint_normal" : "hint_default_white");
            }
            else if (type == "color")
            {
                declaration = std::format("uniform float4 {} : source_color = {};", name, literal(Type::Float4, value));
            }
            else
            {
                const Type parameterType = settingType(type).value_or(Type::Float);
                std::string hints;
                if ((type == "float" || type == "int") && node.text("hint") == "range")
                {
                    const math::Vec4 range = node.vector("range", math::Vec4{0.0f, 1.0f, 0.0f, 0.0f});
                    hints = range.z > 0.0f ? std::format(" : hint_range({}, {}, {})", floatLiteral(range.x), floatLiteral(range.y), floatLiteral(range.z))
                                           : std::format(" : hint_range({}, {})", floatLiteral(range.x), floatLiteral(range.y));
                }
                declaration = std::format("uniform {} {}{} = {};", typeName(parameterType), name, hints, literal(parameterType, value));
            }
            line(instance ? "instance " + declaration : declaration, node.id);
        }
        line("");
    }

    void writeNoise()
    {
        line("// Value noise of the Noise nodes, from 0 to 1, in octaves.");
        line("float devexHash(float3 p)");
        line("{");
        line("    p = frac(p * 0.3183099 + 0.1) * 17.0;");
        line("    return frac(p.x * p.y * p.z * (p.x + p.y + p.z));");
        line("}");
        line("float devexValueNoise(float3 x)");
        line("{");
        line("    const float3 i = floor(x);");
        line("    const float3 f = frac(x);");
        line("    const float3 u = f * f * (3.0 - 2.0 * f);");
        line("    return lerp(lerp(lerp(devexHash(i), devexHash(i + float3(1.0, 0.0, 0.0)), u.x),");
        line("                     lerp(devexHash(i + float3(0.0, 1.0, 0.0)), devexHash(i + float3(1.0, 1.0, 0.0)), u.x), u.y),");
        line("                lerp(lerp(devexHash(i + float3(0.0, 0.0, 1.0)), devexHash(i + float3(1.0, 0.0, 1.0)), u.x),");
        line("                     lerp(devexHash(i + float3(0.0, 1.0, 1.0)), devexHash(i + float3(1.0, 1.0, 1.0)), u.x), u.y), u.z);");
        line("}");
        line("float devexNoise(float3 p, int octaves)");
        line("{");
        line("    float sum = 0.0;");
        line("    float amplitude = 0.5;");
        line("    float total = 0.0;");
        line("    for (int octave = 0; octave < octaves; ++octave)");
        line("    {");
        line("        sum += devexValueNoise(p) * amplitude;");
        line("        total += amplitude;");
        line("        p = p * 2.03 + 17.1;");
        line("        amplitude *= 0.5;");
        line("    }");
        line("    return sum / max(total, 1e-6);");
        line("}");
        line("");
    }

    void writeFunction(ShaderFunction function, const ShaderGraphNode& output)
    {
        m_written.clear();
        m_visiting.clear();
        line(std::format("void {}()", toString(function)));
        line("{");
        const std::vector<ShaderGraphPort> ports = inputsOf(output, m_graph.kind);
        std::vector<const Builtin*> targets;
        for (const Builtin& builtin : builtins)
        {
            if (builtin.kind == m_graph.kind && builtin.function == function && builtin.writable)
            {
                targets.push_back(&builtin);
            }
        }
        for (std::uint32_t port = 0; port < ports.size() && port < targets.size(); ++port)
        {
            const ShaderGraphLink* const link = m_graph.linkInto(output.id, port);
            if (link == nullptr)
            {
                continue;
            }
            if (const std::optional<std::string> value = inputExpression(output, port, ports[port]))
            {
                const Builtin& target = *targets[port];
                const std::string_view written = target.write.empty() ? target.name : target.write;
                const bool adds = function == ShaderFunction::Light;
                line(std::format("    {} {}= {};", written, adds ? "+" : "", *value), output.id);
            }
        }
        line("}");
        line("");
    }

    // What an input of a node reads: what links into it, a built-in, or its value.
    [[nodiscard]] std::optional<std::string> inputExpression(const ShaderGraphNode& node, std::uint32_t port, const ShaderGraphPort& info)
    {
        if (const ShaderGraphLink* const link = m_graph.linkInto(node.id, port))
        {
            const ShaderGraphNode* const from = m_graph.find(link->fromNode);
            if (from == nullptr || from->function != node.function)
            {
                error(node.id, "a link comes from a node of another function", false);
            }
            else if (!writeNode(*from))
            {
                return std::nullopt;
            }
            else
            {
                const std::vector<ShaderGraphPort> outputs = outputsOf(*from, m_graph.kind);
                if (link->fromPort < outputs.size())
                {
                    std::optional<std::string> converted = convert(outputExpression(*from, link->fromPort), outputs[link->fromPort].type, info.type);
                    if (!converted)
                    {
                        error(node.id, std::format("{} cannot take a {} from {}", info.name, typeName(outputs[link->fromPort].type), nodeTitle(*from)));
                    }
                    return converted;
                }
            }
        }
        if (!info.builtin.empty())
        {
            if (const Builtin* const builtin = readableBuiltin(m_graph.kind, node.function, info.builtin))
            {
                return convert(std::string(builtin->read.empty() ? shownName(builtin->name) : builtin->read), builtin->type, info.type);
            }
        }
        if (info.type == Type::Texture)
        {
            error(node.id, std::format("{} needs a texture: link a Texture Parameter", nodeTitle(node)));
            return std::nullopt;
        }
        return literal(info.type, node.input(port, info.fallback));
    }

    // The expression of an output of a node already written.
    [[nodiscard]] std::string outputExpression(const ShaderGraphNode& node, std::uint32_t port) const
    {
        if (node.type == "input")
        {
            const Builtin* const builtin = readableBuiltin(m_graph.kind, node.function, node.text("name"));
            return builtin == nullptr ? "0.0" : std::string(builtin->read.empty() ? shownName(builtin->name) : builtin->read);
        }
        if (node.type == "constant")
        {
            const std::string_view type = node.text("type", "float");
            const math::Vec4 value = node.vector("value");
            if (type == "color")
            {
                return port == 0 ? literal(Type::Float3, value) : floatLiteral(value.w);
            }
            return literal(typeOf(node, Type::Float), value);
        }
        if (node.type == "parameter")
        {
            const std::string name(node.text("name"));
            if (node.text("type") == "color")
            {
                return port == 0 ? name + ".rgb" : name + ".a";
            }
            return name;
        }
        return std::format("n{}_{}", node.id, port);
    }

    // Writes the lines of a node once, those of what it reads first. False for a loop.
    bool writeNode(const ShaderGraphNode& node)
    {
        if (m_written.contains(node.id))
        {
            return true;
        }
        if (!m_visiting.insert(node.id).second)
        {
            error(node.id, std::format("{} reads what it computes: the links make a loop", nodeTitle(node)));
            return false;
        }
        const std::vector<ShaderGraphPort> ports = inputsOf(node, m_graph.kind);
        std::vector<std::string> inputs;
        bool complete = true;
        for (std::uint32_t port = 0; port < ports.size(); ++port)
        {
            std::optional<std::string> expression = inputExpression(node, port, ports[port]);
            complete = complete && expression.has_value();
            inputs.push_back(expression.value_or("0.0"));
        }
        m_visiting.erase(node.id);
        m_written.insert(node.id);
        if (!complete)
        {
            return false;
        }
        writeLines(node, ports, inputs);
        return true;
    }

    void writeLines(const ShaderGraphNode& node, const std::vector<ShaderGraphPort>& ports, const std::vector<std::string>& in)
    {
        const std::string_view type = node.type;
        if (type == "input" || type == "constant" || type == "parameter" || type == "output")
        {
            return;
        }
        const Type value = typeOf(node, Type::Float);
        const auto assign = [&](std::uint32_t port, Type outputType, const std::string& expression) {
            line(std::format("    const {} n{}_{} = {};", typeName(outputType), node.id, port, expression), node.id);
        };
        if (type == "operator")
        {
            const std::string_view op = node.text("op", "add");
            const std::string& a = in[0];
            const std::string& b = in[1];
            std::string expression;
            if (op == "add")
            {
                expression = std::format("{} + {}", a, b);
            }
            else if (op == "subtract")
            {
                expression = std::format("{} - {}", a, b);
            }
            else if (op == "multiply")
            {
                expression = std::format("{} * {}", a, b);
            }
            else if (op == "divide")
            {
                expression = std::format("{} / {}", a, b);
            }
            else if (op == "remainder")
            {
                expression = value == Type::Int ? std::format("{} % {}", a, b) : std::format("fmod({}, {})", a, b);
            }
            else if (op == "power" || op == "min" || op == "max" || op == "atan2" || op == "cross" || op == "reflect")
            {
                expression = std::format("{}({}, {})", op == "power" ? std::string_view("pow") : op, a, b);
            }
            else
            {
                error(node.id, std::format("unknown operator '{}'", op));
                expression = a;
            }
            assign(0, value, expression);
        }
        else if (type == "function")
        {
            const std::string_view function = node.text("function", "sin");
            const std::string& x = in[0];
            std::string expression;
            if (function == "fract")
            {
                expression = std::format("frac({})", x);
            }
            else if (function == "inverse_sqrt")
            {
                expression = std::format("rsqrt({})", x);
            }
            else if (function == "negate")
            {
                expression = std::format("-({})", x);
            }
            else if (function == "one_minus")
            {
                expression = std::format("1.0 - ({})", x);
            }
            else if (function == "reciprocal")
            {
                expression = std::format("1.0 / ({})", x);
            }
            else
            {
                expression = std::format("{}({})", function, x);
            }
            assign(0, value, expression);
        }
        else if (type == "dot" || type == "distance" || type == "cross")
        {
            assign(0, type == "cross" ? Type::Float3 : Type::Float, std::format("{}({}, {})", type, in[0], in[1]));
        }
        else if (type == "length")
        {
            assign(0, Type::Float, std::format("length({})", in[0]));
        }
        else if (type == "mix")
        {
            assign(0, value, std::format("lerp({}, {}, {})", in[0], in[1], convert(in[2], Type::Float, value).value_or(in[2])));
        }
        else if (type == "clamp")
        {
            assign(0, value, std::format("clamp({}, {}, {})", in[0], in[1], in[2]));
        }
        else if (type == "step")
        {
            assign(0, value, std::format("step({}, {})", in[0], in[1]));
        }
        else if (type == "smoothstep")
        {
            assign(0, value, std::format("smoothstep({}, {}, {})", in[0], in[1], in[2]));
        }
        else if (type == "remap")
        {
            assign(0, Type::Float, std::format("{3} + ({0} - {1}) * ({4} - {3}) / max({2} - {1}, 1e-6)", in[0], in[1], in[2], in[3], in[4]));
        }
        else if (type == "compose")
        {
            const Type vector = typeOf(node, Type::Float3);
            std::string arguments;
            for (const std::string& component : in)
            {
                arguments += (arguments.empty() ? "" : ", ") + component;
            }
            assign(0, vector, std::format("{}({})", typeName(vector), arguments));
        }
        else if (type == "decompose")
        {
            for (std::uint32_t axis = 0; axis < componentCount(typeOf(node, Type::Float3)); ++axis)
            {
                assign(axis, Type::Float, std::format("({}).{}", in[0], axes[axis]));
            }
        }
        else if (type == "texture")
        {
            const bool lod = m_graph.linkInto(node.id, 2) != nullptr;
            line(std::format("    const float4 n{}_2 = {};", node.id,
                             lod ? std::format("textureLod({}, {}, {})", in[0], in[1], in[2]) : std::format("texture({}, {})", in[0], in[1])),
                 node.id);
            line(std::format("    const float3 n{0}_0 = n{0}_2.rgb;", node.id), node.id);
            line(std::format("    const float n{0}_1 = n{0}_2.a;", node.id), node.id);
        }
        else if (type == "fresnel")
        {
            const bool invert = node.vector("invert").x != 0.0f;
            const std::string facing = std::format("saturate(dot(normalize({}), normalize({})))", in[0], in[1]);
            assign(0, Type::Float, std::format("pow({}, {})", invert ? facing : std::format("1.0 - {}", facing), in[2]));
        }
        else if (type == "noise")
        {
            const int octaves = std::clamp(static_cast<int>(std::round(node.vector("octaves", math::Vec4{3.0f}).x)), 1, 6);
            assign(0, Type::Float, std::format("devexNoise({} * {}, {})", in[0], in[1], octaves));
        }
        else if (type == "expression")
        {
            const std::vector<ShaderGraphPort> outputs = outputsOf(node, m_graph.kind);
            if (outputs.empty())
            {
                error(node.id, "the expression declares no output: list them as \"float result\"");
            }
            for (std::uint32_t port = 0; port < outputs.size(); ++port)
            {
                line(std::format("    {} n{}_{};", typeName(outputs[port].type), node.id, port), node.id);
            }
            line("    {", node.id);
            for (std::size_t port = 0; port < ports.size(); ++port)
            {
                line(std::format("        const {} {} = {};", typeName(ports[port].type), ports[port].name, in[port]), node.id);
            }
            for (const ShaderGraphPort& output : outputs)
            {
                line(std::format("        {} {} = {};", typeName(output.type), output.name, literal(output.type, math::Vec4{0.0f})), node.id);
            }
            std::string code(node.text("code"));
            std::string indented;
            std::size_t at = 0;
            while (at <= code.size())
            {
                const std::size_t end = std::min(code.find('\n', at), code.size());
                indented += (indented.empty() ? "" : "\n") + std::string("        ") + code.substr(at, end - at);
                at = end + 1;
            }
            line(indented, node.id);
            for (std::uint32_t port = 0; port < outputs.size(); ++port)
            {
                line(std::format("        n{}_{} = {};", node.id, port, outputs[port].name), node.id);
            }
            line("    }", node.id);
        }
        else
        {
            error(node.id, std::format("unknown node type '{}'", type));
        }
    }

    const ShaderGraphData& m_graph;
    GeneratedShader& m_result;
    std::set<std::uint32_t> m_written;
    std::set<std::uint32_t> m_visiting;
};

} // namespace

GeneratedShader generateShaderGraph(const ShaderGraphData& graph, std::string_view name)
{
    GeneratedShader result;
    Writer(graph, result).write(name);
    // A node that no link takes to an output is drawn but does nothing; one in a loop says so above.
    return result;
}

} // namespace devex::asset
