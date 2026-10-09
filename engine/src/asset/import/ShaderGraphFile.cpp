#include <devex/asset/import/ShaderGraphFile.hpp>

#include <devex/core/Path.hpp>
#include <devex/serialization/Text.hpp>

#include <algorithm>
#include <format>
#include <optional>
#include <set>

namespace devex::asset {
namespace {

using serialization::TextSection;
using serialization::TextValue;

constexpr std::int64_t shaderGraphFormat = 1;

// The shortest text that reads back as the same float.
[[nodiscard]] TextValue number(float value)
{
    return TextValue(std::stod(std::format("{}", value)));
}

[[nodiscard]] TextValue vector2(math::Vec2 value)
{
    return serialization::makeCall("vec2", {number(value.x), number(value.y)});
}

[[nodiscard]] TextValue vector4(math::Vec4 value)
{
    return serialization::makeCall("vec4", {number(value.x), number(value.y), number(value.z), number(value.w)});
}

// Numbers of vec2(...) to vec4(...), a number or a boolean; the components not given are zero.
[[nodiscard]] std::optional<math::Vec4> readVector(const TextValue& value)
{
    if (const std::optional<bool> flag = serialization::asBool(value))
    {
        return math::Vec4{*flag ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};
    }
    if (const std::optional<double> single = serialization::asNumber(value))
    {
        return math::Vec4{static_cast<float>(*single), 0.0f, 0.0f, 0.0f};
    }
    for (const std::string_view name : {"vec2", "vec3", "vec4"})
    {
        if (const serialization::TextCall* const call = serialization::asCall(value, name))
        {
            math::Vec4 result{0.0f};
            for (std::size_t index = 0; index < call->arguments.size() && index < 4; ++index)
            {
                const std::optional<double> component = serialization::asNumber(call->arguments[index]);
                if (!component)
                {
                    return std::nullopt;
                }
                result[static_cast<int>(index)] = static_cast<float>(*component);
            }
            return result;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::uint32_t> readId(const TextSection& section, std::string_view key)
{
    const TextValue* const value = section.findAttribute(key);
    const std::optional<std::int64_t> number = value != nullptr ? serialization::asInteger(*value) : std::nullopt;
    if (!number || *number < 0 || *number > 0xFFFFFFFF)
    {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(*number);
}

[[nodiscard]] core::Error parseError(const TextSection& section, std::string message)
{
    return core::Error{core::ErrorCode::Parse, std::format("line {}: {}", section.line, std::move(message))};
}

} // namespace

core::Result<ShaderGraphData> parseShaderGraphFile(std::string_view text)
{
    const core::Result<serialization::TextDocument> document = serialization::parseText(text);
    if (!document)
    {
        return std::unexpected(document.error());
    }
    if (document->sections.empty() || document->sections.front().type != "shader_graph")
    {
        return core::makeError(core::ErrorCode::Parse, "a shader graph starts with a [shader_graph] section");
    }
    const TextSection& header = document->sections.front();
    const TextValue* const format = header.findAttribute("format");
    const std::optional<std::int64_t> version = format != nullptr ? serialization::asInteger(*format) : std::nullopt;
    if (!version || *version > shaderGraphFormat)
    {
        return core::makeError(core::ErrorCode::Unsupported, "unknown shader graph format, a newer Devex may be needed");
    }
    ShaderGraphData graph;
    const TextValue* const kindValue = header.findAttribute("type");
    const std::string* const kindName = kindValue != nullptr ? serialization::asString(*kindValue) : nullptr;
    const std::optional<ShaderKind> kind = kindName != nullptr ? parseShaderKind(*kindName) : std::nullopt;
    if (!kind)
    {
        return std::unexpected(parseError(header, "type is \"spatial\", \"canvas_item\", \"particles\" or \"sky\""));
    }
    graph.kind = *kind;
    if (const TextValue* const modes = header.findProperty("render_modes"))
    {
        const serialization::TextCall* const list = serialization::asCall(*modes, "list");
        if (list == nullptr)
        {
            return std::unexpected(parseError(header, "render_modes is a list of names"));
        }
        for (const TextValue& mode : list->arguments)
        {
            const std::string* const name = serialization::asString(mode);
            if (name == nullptr)
            {
                return std::unexpected(parseError(header, "render_modes is a list of names"));
            }
            graph.renderModes.push_back(*name);
        }
    }

    std::set<std::uint32_t> ids;
    for (std::size_t index = 1; index < document->sections.size(); ++index)
    {
        const TextSection& section = document->sections[index];
        if (section.type == "node")
        {
            const std::optional<std::uint32_t> id = readId(section, "id");
            const TextValue* const typeValue = section.findAttribute("type");
            const std::string* const type = typeValue != nullptr ? serialization::asString(*typeValue) : nullptr;
            const TextValue* const functionValue = section.findAttribute("function");
            const std::string* const functionName = functionValue != nullptr ? serialization::asString(*functionValue) : nullptr;
            const std::optional<ShaderFunction> function = functionName != nullptr ? parseShaderFunction(*functionName) : std::nullopt;
            if (!id || *id == 0 || type == nullptr || !function)
            {
                return std::unexpected(parseError(section, "a node has an id from 1, a type and a function"));
            }
            if (!ids.insert(*id).second)
            {
                return std::unexpected(parseError(section, std::format("two nodes have the id {}", *id)));
            }
            ShaderGraphNode& node = graph.nodes.emplace_back();
            node.id = *id;
            node.type = *type;
            node.function = *function;
            for (const serialization::TextProperty& property : section.properties)
            {
                if (property.key == "position")
                {
                    const std::optional<math::Vec4> position = readVector(property.value);
                    if (!position)
                    {
                        return std::unexpected(parseError(section, "position is a vec2(...)"));
                    }
                    node.position = math::Vec2(*position);
                    continue;
                }
                if (property.key == "values")
                {
                    const serialization::TextCall* const list = serialization::asCall(property.value, "list");
                    if (list == nullptr)
                    {
                        return std::unexpected(parseError(section, "values is a list of vectors"));
                    }
                    for (const TextValue& value : list->arguments)
                    {
                        const std::optional<math::Vec4> input = readVector(value);
                        if (!input)
                        {
                            return std::unexpected(parseError(section, "values is a list of vectors"));
                        }
                        node.inputs.push_back(*input);
                    }
                    continue;
                }
                if (const std::string* const settingText = serialization::asString(property.value))
                {
                    node.setText(property.key, *settingText);
                }
                else if (const std::optional<math::Vec4> settingValue = readVector(property.value))
                {
                    node.setVector(property.key, *settingValue);
                }
                else
                {
                    return std::unexpected(parseError(section, std::format("{} is a text or numbers", property.key)));
                }
            }
        }
        else if (section.type == "link")
        {
            const std::optional<std::uint32_t> from = readId(section, "from");
            const std::optional<std::uint32_t> fromPort = readId(section, "from_port");
            const std::optional<std::uint32_t> to = readId(section, "to");
            const std::optional<std::uint32_t> toPort = readId(section, "to_port");
            if (!from || !fromPort || !to || !toPort)
            {
                return std::unexpected(parseError(section, "a link has from, from_port, to and to_port"));
            }
            graph.links.push_back({*from, *fromPort, *to, *toPort});
        }
        else
        {
            return std::unexpected(parseError(section, std::format("unknown section [{}]", section.type)));
        }
    }
    // Links to nodes that are gone are dropped: what is left still draws.
    std::erase_if(graph.links, [&](const ShaderGraphLink& link) { return !ids.contains(link.fromNode) || !ids.contains(link.toNode); });
    return graph;
}

std::string writeShaderGraphFile(const ShaderGraphData& graph)
{
    serialization::TextDocument document;
    TextSection& header = document.sections.emplace_back();
    header.type = "shader_graph";
    header.attributes.push_back({"format", TextValue(shaderGraphFormat)});
    header.attributes.push_back({"type", TextValue(std::string(toString(graph.kind)))});
    if (!graph.renderModes.empty())
    {
        std::vector<TextValue> modes;
        for (const std::string& mode : graph.renderModes)
        {
            modes.emplace_back(mode);
        }
        header.properties.push_back({"render_modes", serialization::makeCall("list", std::move(modes))});
    }
    for (const ShaderGraphNode& node : graph.nodes)
    {
        TextSection& section = document.sections.emplace_back();
        section.type = "node";
        section.attributes.push_back({"id", TextValue(static_cast<std::int64_t>(node.id))});
        section.attributes.push_back({"type", TextValue(node.type)});
        section.attributes.push_back({"function", TextValue(std::string(toString(node.function)))});
        section.properties.push_back({"position", vector2(node.position)});
        for (const ShaderGraphSetting& setting : node.settings)
        {
            section.properties.push_back({setting.name, setting.text.empty() ? vector4(setting.value) : TextValue(setting.text)});
        }
        if (!node.inputs.empty())
        {
            std::vector<TextValue> inputs;
            for (const math::Vec4 input : node.inputs)
            {
                inputs.push_back(vector4(input));
            }
            section.properties.push_back({"values", serialization::makeCall("list", std::move(inputs))});
        }
    }
    for (const ShaderGraphLink& link : graph.links)
    {
        TextSection& section = document.sections.emplace_back();
        section.type = "link";
        section.attributes.push_back({"from", TextValue(static_cast<std::int64_t>(link.fromNode))});
        section.attributes.push_back({"from_port", TextValue(static_cast<std::int64_t>(link.fromPort))});
        section.attributes.push_back({"to", TextValue(static_cast<std::int64_t>(link.toNode))});
        section.attributes.push_back({"to_port", TextValue(static_cast<std::int64_t>(link.toPort))});
    }
    return serialization::writeText(document);
}

ShaderData compileShaderGraph(const ShaderGraphData& graph, const std::filesystem::path& source, const ShaderCompiler& compiler,
                              const std::atomic<bool>* cancelled)
{
    const GeneratedShader generated = generateShaderGraph(graph, core::toUtf8(source.filename()));
    const bool broken = std::ranges::any_of(generated.diagnostics, &ShaderDiagnostic::error);
    ShaderData shader;
    if (broken)
    {
        // What the code would be, for its kind and its uniforms, without compiling it.
        const ShaderSource parsed = parseShaderSource(generated.code);
        shader = parsed.shader;
        shader.diagnostics.clear();
    }
    else
    {
        shader = compileShader(generated.code, source, compiler, cancelled);
    }
    // The lines of the code lead back to the nodes they come from.
    for (ShaderDiagnostic& diagnostic : shader.diagnostics)
    {
        if (diagnostic.line >= 1 && diagnostic.line <= generated.lineNodes.size())
        {
            diagnostic.node = generated.lineNodes[diagnostic.line - 1];
        }
        diagnostic.line = 0;
        diagnostic.column = 0;
    }
    shader.diagnostics.insert(shader.diagnostics.begin(), generated.diagnostics.begin(), generated.diagnostics.end());
    return shader;
}

} // namespace devex::asset
