#pragma once

#include <devex/core/Export.hpp>

#include <devex/asset/ShaderData.hpp>
#include <devex/math/Math.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Shader graphs, .dvxshadergraph files: the VisualShader of Godot. Nodes linked from their outputs
// to the inputs of others compute what the output node of each function writes, and the import
// turns the graph into the code of a .dvxshader, compiled as one. A material uses a graph as it uses
// a shader.
namespace devex::asset {

inline constexpr std::string_view shaderGraphExtension = ".dvxshadergraph";

// The function of a shader a node belongs to, which its graph is drawn in.
enum class ShaderFunction : std::uint8_t
{
    Vertex = 0,
    Fragment = 1,
    Light = 2,
    Sky = 3,
};

// "vertex", "fragment", "light" or "sky".
[[nodiscard]] DEVEX_API std::string_view toString(ShaderFunction function) noexcept;
[[nodiscard]] DEVEX_API std::optional<ShaderFunction> parseShaderFunction(std::string_view text) noexcept;
// The functions a kind has: vertex, fragment and light for spatial; vertex and fragment for canvas
// items and particles; sky for skies.
[[nodiscard]] DEVEX_API std::span<const ShaderFunction> functionsOf(ShaderKind kind) noexcept;

// A setting of a node, by name: a text (an operator, a type, a name, code) or numbers.
struct DEVEX_API ShaderGraphSetting
{
    std::string name;
    std::string text;
    math::Vec4 value{0.0f};

    bool operator==(const ShaderGraphSetting&) const = default;
};

struct DEVEX_API ShaderGraphNode
{
    // From 1, unique in its graph.
    std::uint32_t id = 0;
    // What it computes, in the catalog: "operator", "texture", "parameter"...
    std::string type;
    ShaderFunction function = ShaderFunction::Fragment;
    // Where its top-left corner is in the graph.
    math::Vec2 position{0.0f};
    std::vector<ShaderGraphSetting> settings;
    // The values of its inputs that no link feeds, by port.
    std::vector<math::Vec4> inputs;

    [[nodiscard]] const ShaderGraphSetting* find(std::string_view name) const noexcept;
    [[nodiscard]] std::string_view text(std::string_view name, std::string_view fallback = {}) const noexcept;
    [[nodiscard]] math::Vec4 vector(std::string_view name, math::Vec4 fallback = math::Vec4{0.0f}) const noexcept;
    void setText(std::string_view name, std::string text);
    void setVector(std::string_view name, math::Vec4 value);
    // The value of an input no link feeds, or the fallback of its port.
    [[nodiscard]] math::Vec4 input(std::size_t port, math::Vec4 fallback) const noexcept;

    bool operator==(const ShaderGraphNode&) const = default;
};

// A link from an output of a node to an input of another; an input takes one link at most.
struct DEVEX_API ShaderGraphLink
{
    std::uint32_t fromNode = 0;
    std::uint32_t fromPort = 0;
    std::uint32_t toNode = 0;
    std::uint32_t toPort = 0;

    bool operator==(const ShaderGraphLink&) const = default;
};

struct DEVEX_API ShaderGraphData
{
    ShaderKind kind = ShaderKind::Spatial;
    // The render_mode of the shader, by name.
    std::vector<std::string> renderModes;
    std::vector<ShaderGraphNode> nodes;
    std::vector<ShaderGraphLink> links;

    [[nodiscard]] ShaderGraphNode* find(std::uint32_t id) noexcept;
    [[nodiscard]] const ShaderGraphNode* find(std::uint32_t id) const noexcept;
    [[nodiscard]] std::uint32_t nextId() const noexcept;
    // The link that feeds an input, or null.
    [[nodiscard]] const ShaderGraphLink* linkInto(std::uint32_t node, std::uint32_t port) const noexcept;
    // Links an output to an input, replacing what fed it. Returns false, linking nothing, for ports
    // that do not exist, for nodes of different functions, and for a link that would close a loop.
    bool connect(ShaderGraphLink link);
    // Removes a node and its links.
    void removeNode(std::uint32_t id);
    // Whether a link from `from` to `to` would close a loop: `to` already feeds `from`.
    [[nodiscard]] bool feeds(std::uint32_t to, std::uint32_t from) const;
    // The output node of a function, or null.
    [[nodiscard]] const ShaderGraphNode* outputOf(ShaderFunction function) const noexcept;

    bool operator==(const ShaderGraphData&) const = default;
};

// A port of a node: what it carries, and what an input reads when no link feeds it.
struct DEVEX_API ShaderGraphPort
{
    std::string name;
    ShaderParameterType type = ShaderParameterType::Float;
    math::Vec4 fallback{0.0f};
    // A built-in an input reads when no link feeds it, such as UV; empty for its value.
    std::string builtin;
};

// How the inspector edits a setting of a node.
enum class ShaderSettingKind : std::uint8_t
{
    // One of `options`, as text.
    Choice,
    // A name, typed.
    Name,
    // Numbers: as many as `count`.
    Numbers,
    Color,
    Toggle,
    // Lines of Slang.
    Code,
};

struct DEVEX_API ShaderSettingInfo
{
    std::string name;
    std::string label;
    ShaderSettingKind kind = ShaderSettingKind::Choice;
    std::vector<std::string> options;
    std::uint32_t count = 1;
    std::string tooltip;
};

// An entry of the menu that adds nodes: a type with the settings it starts with.
struct DEVEX_API ShaderNodeEntry
{
    std::string_view category;
    std::string_view label;
    std::string_view type;
    std::string_view description;
    // Settings the node starts with: name then text, in pairs.
    std::vector<std::pair<std::string_view, std::string_view>> presets;
};

// The nodes the menu offers, by category.
[[nodiscard]] DEVEX_API std::span<const ShaderNodeEntry> shaderNodeCatalog();
// A node of an entry, at a place, for a function of a kind of shader.
[[nodiscard]] DEVEX_API ShaderGraphNode makeShaderNode(const ShaderNodeEntry& entry, ShaderKind kind, ShaderFunction function,
                                                       std::uint32_t id, math::Vec2 position);
// Whether the menu offers an entry in a function of a kind: the outputs of a light only for spatial
// shaders, a texture of the sprite only where one is drawn...
[[nodiscard]] DEVEX_API bool offers(const ShaderNodeEntry& entry, ShaderKind kind, ShaderFunction function);

// The ports of a node as its settings make them, in the graph of a kind.
[[nodiscard]] DEVEX_API std::vector<ShaderGraphPort> inputsOf(const ShaderGraphNode& node, ShaderKind kind);
[[nodiscard]] DEVEX_API std::vector<ShaderGraphPort> outputsOf(const ShaderGraphNode& node, ShaderKind kind);
// What a node shows on its title: "Add", "Input: UV", "Parameter: speed"...
[[nodiscard]] DEVEX_API std::string nodeTitle(const ShaderGraphNode& node);
// The category of a node, for its colour.
[[nodiscard]] DEVEX_API std::string_view nodeCategory(const ShaderGraphNode& node);
// The settings of a node the inspector edits.
[[nodiscard]] DEVEX_API std::vector<ShaderSettingInfo> settingsOf(const ShaderGraphNode& node, ShaderKind kind);
// The built-ins an Input node reads in a function of a kind.
[[nodiscard]] DEVEX_API std::vector<std::string> inputBuiltins(ShaderKind kind, ShaderFunction function);

// A new graph of a kind: the output node of each of its functions.
[[nodiscard]] DEVEX_API ShaderGraphData makeShaderGraph(ShaderKind kind);

// The code of a graph, as a .dvxshader, and the node each of its lines comes from (0 for none),
// with the mistakes of the graph: links between ports that cannot convert, names of parameters
// taken twice, loops.
struct DEVEX_API GeneratedShader
{
    std::string code;
    std::vector<std::uint32_t> lineNodes;
    std::vector<ShaderDiagnostic> diagnostics;
};

[[nodiscard]] DEVEX_API GeneratedShader generateShaderGraph(const ShaderGraphData& graph, std::string_view name);

} // namespace devex::asset
