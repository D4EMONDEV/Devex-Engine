#include <devex/asset/import/ShaderFile.hpp>

#include <devex/core/Path.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/platform/Process.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <format>
#include <fstream>
#include <mutex>
#include <optional>
#include <ranges>
#include <set>
#include <system_error>
#include <thread>
#include <utility>

namespace devex::asset {
namespace {

// ---- Reading ----

enum class TokenKind : std::uint8_t
{
    Identifier,
    Number,
    String,
    Symbol,
};

struct Token
{
    TokenKind kind = TokenKind::Symbol;
    std::string_view text;
    std::size_t offset = 0;
    std::uint32_t line = 1;
    std::uint32_t column = 1;
};

[[nodiscard]] bool isIdentifierStart(char character) noexcept
{
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || character == '_';
}

[[nodiscard]] bool isDigit(char character) noexcept
{
    return character >= '0' && character <= '9';
}

[[nodiscard]] bool isIdentifierPart(char character) noexcept
{
    return isIdentifierStart(character) || isDigit(character);
}

// The tokens of the text, without its comments and white space.
[[nodiscard]] std::vector<Token> tokenize(std::string_view text)
{
    std::vector<Token> tokens;
    std::size_t at = 0;
    std::uint32_t line = 1;
    std::size_t lineStart = 0;
    const auto advance = [&](std::size_t count) {
        for (std::size_t index = 0; index < count && at < text.size(); ++index, ++at)
        {
            if (text[at] == '\n')
            {
                ++line;
                lineStart = at + 1;
            }
        }
    };
    while (at < text.size())
    {
        const char character = text[at];
        if (character == ' ' || character == '\t' || character == '\r' || character == '\n')
        {
            advance(1);
            continue;
        }
        if (text.substr(at, 2) == "//")
        {
            const std::size_t end = text.find('\n', at);
            advance((end == std::string_view::npos ? text.size() : end) - at);
            continue;
        }
        if (text.substr(at, 2) == "/*")
        {
            const std::size_t end = text.find("*/", at + 2);
            advance((end == std::string_view::npos ? text.size() : end + 2) - at);
            continue;
        }
        Token token{.offset = at, .line = line, .column = static_cast<std::uint32_t>(at - lineStart + 1)};
        std::size_t length = 1;
        if (isIdentifierStart(character))
        {
            token.kind = TokenKind::Identifier;
            while (at + length < text.size() && isIdentifierPart(text[at + length]))
            {
                ++length;
            }
        }
        else if (isDigit(character) || (character == '.' && at + 1 < text.size() && isDigit(text[at + 1])))
        {
            token.kind = TokenKind::Number;
            while (at + length < text.size())
            {
                const char next = text[at + length];
                const bool exponent = (next == '+' || next == '-') &&
                                      (text[at + length - 1] == 'e' || text[at + length - 1] == 'E');
                if (!isIdentifierPart(next) && next != '.' && !exponent)
                {
                    break;
                }
                ++length;
            }
        }
        else if (character == '"')
        {
            token.kind = TokenKind::String;
            while (at + length < text.size() && text[at + length] != '"' && text[at + length] != '\n')
            {
                length += text[at + length] == '\\' ? 2 : 1;
            }
            length = std::min(length + 1, text.size() - at);
        }
        token.text = text.substr(at, length);
        tokens.push_back(token);
        advance(length);
    }
    return tokens;
}

[[nodiscard]] std::optional<double> parseNumber(std::string_view text) noexcept
{
    while (!text.empty() && (text.back() == 'f' || text.back() == 'F' || text.back() == 'h' || text.back() == 'H'))
    {
        text.remove_suffix(1);
    }
    double value = 0.0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size())
    {
        return std::nullopt;
    }
    return value;
}

// The render modes each kind accepts.
[[nodiscard]] std::span<const std::string_view> renderModesOf(ShaderKind kind) noexcept
{
    static constexpr std::array<std::string_view, 10> spatial{
        "unshaded",      "cull_back",        "cull_front",         "cull_disabled",          "blend_mix",
        "blend_add",     "shadows_disabled", "ambient_light_disabled", "world_vertex_coords", "depth_draw_opaque",
    };
    static constexpr std::array<std::string_view, 3> flat{"unshaded", "blend_mix", "blend_add"};
    switch (kind)
    {
    case ShaderKind::Spatial:
        return spatial;
    case ShaderKind::CanvasItem:
    case ShaderKind::Particles:
        return flat;
    case ShaderKind::Sky:
        return {};
    }
    return {};
}

constexpr std::array<std::string_view, 32> spatialBuiltins{
    "TIME",     "VIEWPORT_SIZE", "MODEL_MATRIX",  "VIEW_MATRIX",       "CAMERA_POSITION_WORLD", "VERTEX_ID",
    "VERTEX",   "NORMAL",        "TANGENT",       "BINORMAL",          "UV",                    "COLOR",
    "FRAGCOORD", "FRONT_FACING", "SCREEN_UV",     "VIEW",              "ALBEDO",                "ALPHA",
    "METALLIC", "ROUGHNESS",     "SPECULAR",      "EMISSION",          "AO",                    "NORMAL_MAP",
    "NORMAL_MAP_DEPTH", "ALPHA_SCISSOR_THRESHOLD", "LIGHT", "LIGHT_COLOR", "ATTENUATION", "LIGHT_IS_DIRECTIONAL",
    "DIFFUSE_LIGHT", "SPECULAR_LIGHT",
};
constexpr std::array<std::string_view, 14> canvasBuiltins{
    "TIME",      "VIEWPORT_SIZE", "MODEL_MATRIX", "VERTEX",     "UV",               "COLOR",          "TEXTURE",
    "TEXTURE_PIXEL_SIZE", "FRAGCOORD", "SCREEN_UV", "NORMAL_MAP", "NORMAL_MAP_DEPTH", "NORMAL_TEXTURE", "INSTANCE_ID",
};
constexpr std::array<std::string_view, 11> particleBuiltins{
    "TIME",    "VIEWPORT_SIZE", "CAMERA_POSITION_WORLD", "INSTANCE_ID", "VERTEX", "COLOR",
    "UV",      "TEXTURE",       "FRAGCOORD",             "SCREEN_UV",   "PARTICLE_SIZE",
};
constexpr std::array<std::string_view, 13> skyBuiltins{
    "TIME",        "EYEDIR",        "POSITION",       "SKY_COORDS",       "COLOR",          "FRAGCOORD",
    "SCREEN_UV",   "VIEWPORT_SIZE", "LIGHT0_ENABLED", "LIGHT0_DIRECTION", "LIGHT0_COLOR",   "LIGHT0_ENERGY",
    "AT_CUBEMAP_PASS",
};
// What every kind sees, from the module custom.
constexpr std::array<std::string_view, 3> constants{"PI", "TAU", "E"};

struct Statement
{
    // The tokens from the keyword to the semicolon, both included.
    std::span<const Token> tokens;
};

class SourceReader
{
public:
    explicit SourceReader(ShaderSource& source)
        : m_source(source)
    {
    }

    void error(const Token& at, std::string message)
    {
        m_source.shader.diagnostics.push_back({.line = at.line, .column = at.column, .message = std::move(message)});
    }

    void warning(const Token& at, std::string message)
    {
        m_source.shader.diagnostics.push_back(
            {.line = at.line, .column = at.column, .message = std::move(message), .error = false});
    }

    void readKind(Statement statement)
    {
        const std::span<const Token> tokens = statement.tokens;
        if (m_kindRead)
        {
            error(tokens.front(), "the shader names its shader_type once");
            return;
        }
        m_kindRead = true;
        const std::optional<ShaderKind> kind =
            tokens.size() == 3 ? parseShaderKind(tokens[1].text) : std::optional<ShaderKind>{};
        if (!kind)
        {
            error(tokens.front(), "shader_type is spatial, canvas_item, particles or sky");
            return;
        }
        m_source.shader.kind = *kind;
    }

    // Reports, once, a statement before the shader_type.
    void requireKind(const Token& at)
    {
        if (!m_kindRead)
        {
            error(at, "the shader starts with its shader_type: spatial, canvas_item, particles or sky");
            m_kindRead = true;
        }
    }

    [[nodiscard]] bool kindRead() const noexcept
    {
        return m_kindRead;
    }

    void readRenderModes(Statement statement)
    {
        const std::span<const Token> tokens = statement.tokens;
        requireKind(tokens.front());
        const std::span<const std::string_view> accepted = renderModesOf(m_source.shader.kind);
        bool expectName = true;
        for (const Token& token : tokens.subspan(1, tokens.size() - 2))
        {
            if (expectName != (token.kind == TokenKind::Identifier) || (!expectName && token.text != ","))
            {
                error(token, "render_mode lists names separated by commas");
                return;
            }
            expectName = !expectName;
            if (token.kind != TokenKind::Identifier)
            {
                continue;
            }
            if (std::ranges::find(accepted, token.text) == accepted.end())
            {
                error(token, std::format("{} shaders have no render mode '{}'", toString(m_source.shader.kind), token.text));
                continue;
            }
            applyRenderMode(token.text);
        }
    }

    void readUniform(Statement statement)
    {
        const std::span<const Token> tokens = statement.tokens;
        requireKind(tokens.front());
        if (tokens.size() < 4 || tokens[1].kind != TokenKind::Identifier || tokens[2].kind != TokenKind::Identifier)
        {
            error(tokens.front(), "a uniform reads: uniform <type> <name> [: hints] [= value];");
            return;
        }
        const std::optional<ShaderParameterType> type = parseShaderParameterType(tokens[1].text);
        if (!type)
        {
            error(tokens[1], std::format("uniforms are float, float2, float3, float4, int, bool or sampler2D, not '{}'",
                                         tokens[1].text));
            return;
        }
        ShaderParameter parameter{.name = std::string(tokens[2].text), .type = *type};
        if (!checkName(tokens[2]))
        {
            return;
        }

        std::size_t at = 3;
        if (tokens[at].text == ":")
        {
            ++at;
            while (at < tokens.size() - 1 && tokens[at].text != "=")
            {
                if (!readHint(parameter, tokens, at))
                {
                    return;
                }
                if (tokens[at].text == ",")
                {
                    ++at;
                }
            }
        }
        if (tokens[at].text == "=")
        {
            if (parameter.type == ShaderParameterType::Texture)
            {
                error(tokens[at], "textures have no default value: their hint chooses what an empty one samples");
                return;
            }
            const std::optional<math::Vec4> value = readValue(parameter.type, tokens.subspan(at + 1, tokens.size() - at - 2));
            if (!value)
            {
                error(tokens[at], std::format("the default value of '{}' is a {}", parameter.name, toString(parameter.type)));
                return;
            }
            parameter.defaultValue = *value;
            at = tokens.size() - 1;
        }
        if (at != tokens.size() - 1)
        {
            error(tokens[at], "a uniform reads: uniform <type> <name> [: hints] [= value];");
            return;
        }
        m_source.shader.parameters.push_back(std::move(parameter));
    }

    void readVarying(Statement statement)
    {
        const std::span<const Token> tokens = statement.tokens;
        requireKind(tokens.front());
        if (m_source.shader.kind == ShaderKind::Sky)
        {
            error(tokens.front(), "sky shaders have no varyings");
            return;
        }
        if (tokens.size() != 4 || tokens[1].kind != TokenKind::Identifier || tokens[2].kind != TokenKind::Identifier)
        {
            error(tokens.front(), "a varying reads: varying <type> <name>;");
            return;
        }
        const std::optional<ShaderParameterType> type = parseShaderParameterType(tokens[1].text);
        if (!type || componentCount(*type) == 0 || *type == ShaderParameterType::Int ||
            *type == ShaderParameterType::Bool || *type == ShaderParameterType::Texture)
        {
            error(tokens[1], "varyings are float, float2, float3 or float4");
            return;
        }
        if (checkName(tokens[2]))
        {
            m_source.varyings.push_back({.name = std::string(tokens[2].text), .type = *type});
        }
    }

private:
    void applyRenderMode(std::string_view mode)
    {
        ShaderData& shader = m_source.shader;
        if (mode == "unshaded")
        {
            m_source.unshaded = true;
        }
        else if (mode == "cull_back")
        {
            shader.cull = ShaderCull::Back;
        }
        else if (mode == "cull_front")
        {
            shader.cull = ShaderCull::Front;
        }
        else if (mode == "cull_disabled")
        {
            shader.cull = ShaderCull::Disabled;
        }
        else if (mode == "blend_mix")
        {
            shader.blend = ShaderBlend::Mix;
        }
        else if (mode == "blend_add")
        {
            shader.blend = ShaderBlend::Add;
        }
        else if (mode == "shadows_disabled")
        {
            shader.castsShadows = false;
        }
        else if (mode == "ambient_light_disabled")
        {
            m_source.ambientLight = false;
        }
        else if (mode == "world_vertex_coords")
        {
            m_source.worldVertexCoords = true;
        }
    }

    // A name of its own: not a built-in, not another uniform or varying.
    [[nodiscard]] bool checkName(const Token& name)
    {
        const std::vector<std::string_view> builtins = shaderBuiltins(m_source.shader.kind);
        if (std::ranges::find(builtins, name.text) != builtins.end())
        {
            error(name, std::format("'{}' is a built-in", name.text));
            return false;
        }
        const bool taken = std::ranges::any_of(m_source.shader.parameters,
                                               [&](const ShaderParameter& other) { return other.name == name.text; }) ||
                           std::ranges::any_of(m_source.varyings,
                                               [&](const ShaderVarying& other) { return other.name == name.text; });
        if (taken)
        {
            error(name, std::format("'{}' is declared twice", name.text));
            return false;
        }
        return true;
    }

    [[nodiscard]] bool readHint(ShaderParameter& parameter, std::span<const Token> tokens, std::size_t& at)
    {
        const Token& name = tokens[at];
        if (name.kind != TokenKind::Identifier)
        {
            error(name, "a hint is a name, such as source_color or hint_range(0.0, 1.0)");
            return false;
        }
        ++at;
        const bool texture = parameter.type == ShaderParameterType::Texture;
        if (name.text == "hint_range")
        {
            // hint_range(lowest, highest[, step])
            std::vector<double> numbers;
            if (tokens[at].text != "(")
            {
                error(name, "hint_range(lowest, highest[, step])");
                return false;
            }
            ++at;
            while (at < tokens.size() - 1 && tokens[at].text != ")")
            {
                double sign = 1.0;
                if (tokens[at].text == "-")
                {
                    sign = -1.0;
                    ++at;
                }
                const std::optional<double> number = parseNumber(tokens[at].text);
                if (!number)
                {
                    error(tokens[at], "hint_range(lowest, highest[, step]) takes numbers");
                    return false;
                }
                numbers.push_back(*number * sign);
                ++at;
                if (tokens[at].text == ",")
                {
                    ++at;
                }
            }
            ++at;
            if ((numbers.size() != 2 && numbers.size() != 3) ||
                (parameter.type != ShaderParameterType::Float && parameter.type != ShaderParameterType::Int))
            {
                error(name, "hint_range(lowest, highest[, step]) is for float and int uniforms");
                return false;
            }
            parameter.hint = ShaderHint::Range;
            parameter.range = math::Vec3{static_cast<float>(numbers[0]), static_cast<float>(numbers[1]),
                                         numbers.size() == 3 ? static_cast<float>(numbers[2]) : 0.0f};
            return true;
        }
        if (name.text == "source_color")
        {
            if (parameter.type != ShaderParameterType::Float3 && parameter.type != ShaderParameterType::Float4 && !texture)
            {
                error(name, "source_color is for float3, float4 and sampler2D uniforms");
                return false;
            }
            if (!texture)
            {
                parameter.hint = ShaderHint::Color;
            }
            return true;
        }
        constexpr std::array<std::pair<std::string_view, ShaderHint>, 4> textureHints{{
            {"hint_default_white", ShaderHint::White},
            {"hint_default_black", ShaderHint::Black},
            {"hint_normal", ShaderHint::Normal},
            {"hint_default_transparent", ShaderHint::Black},
        }};
        for (const auto& [hintName, hint] : textureHints)
        {
            if (name.text == hintName)
            {
                if (!texture)
                {
                    error(name, std::format("{} is for sampler2D uniforms", hintName));
                    return false;
                }
                parameter.hint = hint;
                return true;
            }
        }
        constexpr std::array<std::string_view, 8> samplerHints{
            "filter_nearest", "filter_linear",  "filter_nearest_mipmap", "filter_linear_mipmap",
            "repeat_enable",  "repeat_disable", "filter_linear_mipmap_anisotropic", "hint_anisotropy",
        };
        if (texture && std::ranges::find(samplerHints, name.text) != samplerHints.end())
        {
            warning(name, std::format("{} is ignored: a texture keeps the sampler chosen where it is imported", name.text));
            return true;
        }
        error(name, std::format("unknown hint '{}'", name.text));
        return false;
    }

    // A literal: a number, true or false, or floatN(...) of numbers, one of them for all.
    [[nodiscard]] static std::optional<math::Vec4> readValue(ShaderParameterType type, std::span<const Token> tokens)
    {
        std::vector<float> numbers;
        std::size_t at = 0;
        const std::uint32_t count = componentCount(type);
        const auto readNumber = [&]() -> bool {
            float sign = 1.0f;
            if (at < tokens.size() && (tokens[at].text == "-" || tokens[at].text == "+"))
            {
                sign = tokens[at].text == "-" ? -1.0f : 1.0f;
                ++at;
            }
            if (at >= tokens.size())
            {
                return false;
            }
            const std::optional<double> number = parseNumber(tokens[at].text);
            if (!number)
            {
                return false;
            }
            numbers.push_back(static_cast<float>(*number) * sign);
            ++at;
            return true;
        };
        if (type == ShaderParameterType::Bool)
        {
            if (tokens.size() == 1 && (tokens[0].text == "true" || tokens[0].text == "false"))
            {
                return math::Vec4{tokens[0].text == "true" ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};
            }
            return std::nullopt;
        }
        if (count == 1)
        {
            if (!readNumber() || at != tokens.size())
            {
                return std::nullopt;
            }
            return math::Vec4{type == ShaderParameterType::Int ? std::round(numbers[0]) : numbers[0], 0.0f, 0.0f, 0.0f};
        }
        if (tokens.size() < 3 || tokens[0].text != toString(type) || tokens[1].text != "(" || tokens.back().text != ")")
        {
            return std::nullopt;
        }
        at = 2;
        while (at < tokens.size() - 1)
        {
            if (!readNumber())
            {
                return std::nullopt;
            }
            if (at < tokens.size() - 1)
            {
                if (tokens[at].text != ",")
                {
                    return std::nullopt;
                }
                ++at;
            }
        }
        if (numbers.size() == 1)
        {
            numbers.resize(count, numbers.front());
        }
        if (numbers.size() != count)
        {
            return std::nullopt;
        }
        math::Vec4 value{0.0f};
        for (std::uint32_t index = 0; index < count; ++index)
        {
            value[static_cast<int>(index)] = numbers[index];
        }
        return value;
    }

    ShaderSource& m_source;
    bool m_kindRead = false;
};

// ---- Writing ----

void replaceAll(std::string& text, std::string_view marker, std::string_view value)
{
    for (std::size_t at = text.find(marker); at != std::string::npos; at = text.find(marker, at + value.size()))
    {
        text.replace(at, marker.size(), value);
    }
}

[[nodiscard]] std::string uniformDeclarations(const ShaderSource& source)
{
    std::string text;
    for (const ShaderParameter& parameter : source.shader.parameters)
    {
        text += std::format("static {} {};\n", toString(parameter.type), parameter.name);
    }
    for (const ShaderVarying& varying : source.varyings)
    {
        text += std::format("static {} {};\n", toString(varying.type), varying.name);
    }
    return text;
}

[[nodiscard]] std::string uniformReads(const ShaderSource& source)
{
    std::string text;
    std::uint32_t slot = 0;
    for (const ShaderParameter& parameter : source.shader.parameters)
    {
        const std::string read = std::format("materialSlot(scene, material, {})", slot++);
        switch (parameter.type)
        {
        case ShaderParameterType::Float:
            text += std::format("    {} = {}.x;\n", parameter.name, read);
            break;
        case ShaderParameterType::Float2:
            text += std::format("    {} = {}.xy;\n", parameter.name, read);
            break;
        case ShaderParameterType::Float3:
            text += std::format("    {} = {}.xyz;\n", parameter.name, read);
            break;
        case ShaderParameterType::Float4:
            text += std::format("    {} = {};\n", parameter.name, read);
            break;
        case ShaderParameterType::Int:
            text += std::format("    {} = int(round({}.x));\n", parameter.name, read);
            break;
        case ShaderParameterType::Bool:
            text += std::format("    {} = {}.x != 0.0;\n", parameter.name, read);
            break;
        case ShaderParameterType::Texture:
            text += std::format("    {} = slotTexture({});\n", parameter.name, read);
            break;
        }
    }
    return text;
}

// The fields of the varyings in the output of the vertex shader, from TEXCOORD`first` on, and the
// lines that write and read them.
struct VaryingCode
{
    std::string fields;
    std::string writes;
    std::string reads;
};

[[nodiscard]] VaryingCode varyingCode(const ShaderSource& source, std::uint32_t first)
{
    VaryingCode code;
    for (const ShaderVarying& varying : source.varyings)
    {
        code.fields += std::format("    {} varying_{} : TEXCOORD{};\n", toString(varying.type), varying.name, first++);
        code.writes += std::format("    output.varying_{0} = {0};\n", varying.name);
        code.reads += std::format("    {0} = input.varying_{0};\n", varying.name);
    }
    return code;
}

[[nodiscard]] std::string directiveName(std::string_view name)
{
    std::string text;
    for (const char character : name)
    {
        text += character == '"' || character == '\\' ? '_' : character;
    }
    return text;
}

constexpr std::string_view spatialTemplate = R"(// Generated by Devex from @NAME@: a spatial shader, in every pass that draws meshes.

import common;
import pbr;
import custom;

// What the passes push: the instance drawn, and for the prepass where it stood on the previous frame.
struct DevexPass
{
    DrawData draw;
    float4x4 previousWorld;
    float4x4* previousBones;
};

[[vk::push_constant]]
ConstantBuffer<DevexPass> devexPass;

static float TIME;
static float2 VIEWPORT_SIZE;
static float4x4 MODEL_MATRIX;
static float4x4 VIEW_MATRIX;
static float3 CAMERA_POSITION_WORLD;
static uint VERTEX_ID;
static float3 VERTEX;
static float3 NORMAL;
static float3 TANGENT;
static float3 BINORMAL;
static float2 UV;
static float4 COLOR;
static float4 FRAGCOORD;
static bool FRONT_FACING;
static float2 SCREEN_UV;
static float3 VIEW;
static float3 ALBEDO;
static float ALPHA;
static float METALLIC;
static float ROUGHNESS;
static float SPECULAR;
static float3 EMISSION;
static float AO;
static float3 NORMAL_MAP;
static float NORMAL_MAP_DEPTH;
static float ALPHA_SCISSOR_THRESHOLD;
static float3 LIGHT;
static float3 LIGHT_COLOR;
static float ATTENUATION;
static bool LIGHT_IS_DIRECTIONAL;
static float3 DIFFUSE_LIGHT;
static float3 SPECULAR_LIGHT;

@UNIFORMS@
#line 1 "@NAME@"
@CODE@
#line default

void devexReadUniforms(SceneData* scene, uint material)
{
@READ_UNIFORMS@}

void devexBegin(DrawData draw)
{
    let scene = draw.scene;
    TIME = scene->time;
    VIEWPORT_SIZE = float2(scene->viewportWidth, scene->viewportHeight);
    MODEL_MATRIX = draw.world;
    VIEW_MATRIX = scene->view;
    CAMERA_POSITION_WORLD = scene->cameraPosition;
    devexReadUniforms(scene, draw.material);
}

struct DevexVertexOutput
{
    float4 position : SV_Position;
    float3 worldPosition : POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 uv : TEXCOORD0;
    float4 color : COLOR;
    // Clip positions without the jitter, whose difference is the motion of the pixel in the prepass.
    float4 current : TEXCOORD1;
    float4 previous : TEXCOORD2;
@VARYING_FIELDS@};

struct DevexWorldVertex
{
    float3 position;
    float3 normal;
    float4 tangent;
};

// Runs vertex() on a vertex posed by `world` at `time`, and takes it to the world.
DevexWorldVertex devexShadeVertex(uint vertexIndex, float4x4 world, float4x4 model, float time)
{
    const Vertex devexInput = devexPass.draw.vertices[vertexIndex];
    TIME = time;
    MODEL_MATRIX = model;
    VERTEX_ID = vertexIndex;
    VERTEX = devexInput.position;
    NORMAL = devexInput.normal;
    TANGENT = devexInput.tangent.xyz;
    BINORMAL = cross(devexInput.normal, devexInput.tangent.xyz) * (devexInput.tangent.w < 0.0 ? -1.0 : 1.0);
    UV = float2(devexInput.u, devexInput.v);
    COLOR = float4(1.0);
@BEFORE_VERTEX@@CALL_VERTEX@
    DevexWorldVertex result;
@TO_WORLD@    return result;
}

DevexVertexOutput devexFinishVertex(DevexWorldVertex world, float4x4 viewProjection)
{
    DevexVertexOutput output;
    output.position = mul(viewProjection, float4(world.position, 1.0));
    output.worldPosition = world.position;
    output.normal = world.normal;
    output.tangent = world.tangent;
    output.uv = UV;
    output.color = COLOR;
    output.current = float4(0.0);
    output.previous = float4(0.0);
@WRITE_VARYINGS@    return output;
}

DevexWorldVertex devexCurrentVertex(uint vertexIndex)
{
    let draw = devexPass.draw;
    devexBegin(draw);
    return devexShadeVertex(vertexIndex, vertexTransform(draw, vertexIndex), draw.world, draw.scene->time);
}

// Where the vertex stood on the previous frame: the same blend of bones, posed as they were.
float4x4 devexPreviousTransform(uint vertexIndex)
{
    let draw = devexPass.draw;
    if (draw.skinned == 0)
    {
        return devexPass.previousWorld;
    }
    const VertexSkin skin = draw.skin[vertexIndex];
    float4x4 blended = devexPass.previousBones[skin.joints.x] * skin.weights.x;
    blended += devexPass.previousBones[skin.joints.y] * skin.weights.y;
    blended += devexPass.previousBones[skin.joints.z] * skin.weights.z;
    blended += devexPass.previousBones[skin.joints.w] * skin.weights.w;
    const float total = skin.weights.x + skin.weights.y + skin.weights.z + skin.weights.w;
    return total > 0.0001 ? blended : devexPass.previousWorld;
}

[shader("vertex")]
DevexVertexOutput sceneVertex(uint vertexIndex : SV_VertexID)
{
    return devexFinishVertex(devexCurrentVertex(vertexIndex), devexPass.draw.scene->viewProjection);
}

[shader("vertex")]
DevexVertexOutput prepassVertex(uint vertexIndex : SV_VertexID)
{
    let draw = devexPass.draw;
    let scene = draw.scene;
    devexBegin(draw);
    // vertex() at the previous time, on the vertex posed as it was, then as it is.
    const DevexWorldVertex previous =
        devexShadeVertex(vertexIndex, devexPreviousTransform(vertexIndex), devexPass.previousWorld, scene->previousTime);
    const DevexWorldVertex world = devexShadeVertex(vertexIndex, vertexTransform(draw, vertexIndex), draw.world, scene->time);
    DevexVertexOutput output = devexFinishVertex(world, scene->viewProjection);
    output.current = mul(scene->unjitteredViewProjection, float4(world.position, 1.0));
    output.previous = mul(scene->previousViewProjection, float4(previous.position, 1.0));
    return output;
}

[shader("vertex")]
DevexVertexOutput shadowVertex(uint vertexIndex : SV_VertexID)
{
    let draw = devexPass.draw;
    return devexFinishVertex(devexCurrentVertex(vertexIndex), draw.scene->cascadeViewProjections[draw.cascade]);
}

[shader("vertex")]
DevexVertexOutput localShadowVertex(uint vertexIndex : SV_VertexID)
{
    let draw = devexPass.draw;
    return devexFinishVertex(devexCurrentVertex(vertexIndex), draw.scene->shadowViews[draw.cascade].viewProjection);
}

[shader("vertex")]
DevexVertexOutput pickVertex(uint vertexIndex : SV_VertexID)
{
    return devexFinishVertex(devexCurrentVertex(vertexIndex), devexPass.draw.scene->pickViewProjection);
}

[shader("vertex")]
DevexVertexOutput maskVertex(uint vertexIndex : SV_VertexID)
{
    // The outline is composited after TAA, so its silhouette must not inherit the scene jitter.
    return devexFinishVertex(devexCurrentVertex(vertexIndex), devexPass.draw.scene->unjitteredViewProjection);
}

// The built-ins of a fragment, then fragment(), which may discard it.
void devexShadeFragment(DevexVertexOutput input, bool frontFacing)
{
    devexBegin(devexPass.draw);
    VERTEX = input.worldPosition;
    FRAGCOORD = input.position;
    FRONT_FACING = frontFacing;
    float3 normal = normalize(input.normal);
    // Back faces are lit as seen from their side.
    if (!frontFacing)
    {
        normal = -normal;
    }
    float3 tangent = input.tangent.xyz - normal * dot(normal, input.tangent.xyz);
    const float lengthSquared = dot(tangent, tangent);
    tangent = lengthSquared > 1e-10 ? tangent * rsqrt(lengthSquared) : float3(0.0);
    NORMAL = normal;
    TANGENT = tangent;
    BINORMAL = cross(normal, tangent) * (input.tangent.w < 0.0 ? -1.0 : 1.0);
    UV = input.uv;
    COLOR = input.color;
    SCREEN_UV = input.position.xy / VIEWPORT_SIZE;
    VIEW = normalize(CAMERA_POSITION_WORLD - VERTEX);
    ALBEDO = float3(1.0);
    ALPHA = 1.0;
    METALLIC = 0.0;
    ROUGHNESS = 1.0;
    SPECULAR = 0.5;
    EMISSION = float3(0.0);
    AO = 1.0;
    NORMAL_MAP = float3(0.5, 0.5, 1.0);
    NORMAL_MAP_DEPTH = 1.0;
    ALPHA_SCISSOR_THRESHOLD = 0.0;
@READ_VARYINGS@@CALL_FRAGMENT@@SCISSOR@}

// The light() of the shader, as the light model of the surface: what it adds for each light.
struct DevexShaderLight : ILightModel
{
    float3 shade(Surface surface, float3 toLight, float3 color, float attenuation, bool directional)
    {
        LIGHT = toLight;
        LIGHT_COLOR = color;
        ATTENUATION = attenuation;
        LIGHT_IS_DIRECTIONAL = directional;
        DIFFUSE_LIGHT = float3(0.0);
        SPECULAR_LIGHT = float3(0.0);
@CALL_LIGHT@
        return DIFFUSE_LIGHT * surface.diffuseColor + SPECULAR_LIGHT;
    }
};

[shader("fragment")]
float4 sceneFragment(DevexVertexOutput input, bool frontFacing : SV_IsFrontFace) : SV_Target
{
    devexShadeFragment(input, frontFacing);
    let scene = devexPass.draw.scene;
    // A blended surface leaves its colour premultiplied, and an additive one an alpha of zero.
    const float alpha = @TRANSPARENT@ ? saturate(ALPHA) : 1.0;
    const float coverage = @ADDITIVE@ ? 0.0 : alpha;
    if (@UNSHADED@)
    {
        return float4((ALBEDO + EMISSION) * alpha, coverage);
    }

    float3 normal = normalize(NORMAL);
    if (@NORMAL_MAPPED@ && dot(TANGENT, TANGENT) > 1e-10)
    {
        // Tangent-space normal maps store X and Y; Z is reconstructed.
        const float2 encoded = NORMAL_MAP.xy * 2.0 - 1.0;
        const float2 tilt = encoded * NORMAL_MAP_DEPTH;
        const float z = sqrt(saturate(1.0 - dot(encoded, encoded)));
        normal = normalize(TANGENT * tilt.x + BINORMAL * tilt.y + normal * z);
    }
    NORMAL = normal;

    const float metallic = saturate(METALLIC);
    // Very low roughness makes highlights too small to sample: clamp it.
    const float roughness = clamp(ROUGHNESS, 0.045, 1.0);
    const float2 screenUv = input.position.xy / VIEWPORT_SIZE;
    Surface surface;
    surface.position = VERTEX;
    surface.normal = normal;
    surface.view = normalize(VIEW);
    surface.diffuseColor = ALBEDO * (1.0 - metallic);
    surface.f0 = lerp(float3(0.16 * SPECULAR * SPECULAR), ALBEDO, metallic);
    surface.roughness = roughness;
    surface.alpha = roughness * roughness;
    surface.occlusion = saturate(AO) * ambientOcclusionTexture.Sample(clampSampler, screenUv).r;
    surface.viewDistance = -mul(scene->view, float4(VERTEX, 1.0)).z;
    surface.fragmentCoordinate = input.position.xy;

    float3 luminance = shadeLights(scene, surface, @LIGHT_MODEL@());
    if (@AMBIENT@)
    {
        luminance += environmentLight(scene, surface);
    }
    // Emission is relative to the exposure, as that of materials.
    return float4((luminance * scene->exposure + EMISSION) * alpha, coverage);
}

struct DevexPrepassTargets
{
    float2 velocity : SV_Target0;
    float2 normal : SV_Target1;
};

[shader("fragment")]
DevexPrepassTargets prepassFragment(DevexVertexOutput input, bool frontFacing : SV_IsFrontFace)
{
@DEPTH_FRAGMENT@    const float2 current = input.current.xy / input.current.w;
    const float2 previous = input.previous.xy / input.previous.w;
    const float3 normal = frontFacing ? input.normal : -input.normal;
    DevexPrepassTargets output;
    // Clip space spans two units where texture coordinates span one.
    output.velocity = (current - previous) * 0.5;
    output.normal = encodeNormal(normalize(normal));
    return output;
}
@SHADOW_FRAGMENT@
[shader("fragment")]
uint pickFragment(DevexVertexOutput input, bool frontFacing : SV_IsFrontFace) : SV_Target
{
@DEPTH_FRAGMENT@    return devexPass.draw.objectId;
}

[shader("fragment")]
float maskFragment(DevexVertexOutput input, bool frontFacing : SV_IsFrontFace) : SV_Target
{
@DEPTH_FRAGMENT@    return 1.0;
}
)";

constexpr std::string_view canvasTemplate = R"(// Generated by Devex from @NAME@: a canvas_item shader, for sprites and tilemaps.

import common;
import pbr;
import custom;
import canvas;

static float TIME;
static float2 VIEWPORT_SIZE;
static float4x4 MODEL_MATRIX;
static uint INSTANCE_ID;
static float2 VERTEX;
static float2 UV;
static float4 COLOR;
static sampler2D TEXTURE;
static float2 TEXTURE_PIXEL_SIZE;
static float4 FRAGCOORD;
static float2 SCREEN_UV;
static float3 NORMAL_MAP;
static float NORMAL_MAP_DEPTH;
static sampler2D NORMAL_TEXTURE;

@UNIFORMS@
#line 1 "@NAME@"
@CODE@
#line default

void devexReadUniforms(SceneData* scene, uint material)
{
@READ_UNIFORMS@}

void devexBegin(Sprite sprite, uint index)
{
    let scene = data.scene;
    TIME = scene->time;
    VIEWPORT_SIZE = float2(scene->viewportWidth, scene->viewportHeight);
    MODEL_MATRIX = sprite.world;
    INSTANCE_ID = index - data.first;
    // A sprite without a texture samples white, and a flat normal without a normal map.
    TEXTURE = sampler2D(sprite.texture != noTexture ? sprite.texture : 0u);
    TEXTURE_PIXEL_SIZE = 1.0 / float2(max(textureSize(TEXTURE, 0), int2(1)));
    NORMAL_TEXTURE = sampler2D(sprite.normalTexture != noTexture ? sprite.normalTexture : 1u);
    devexReadUniforms(scene, sprite.material);
}

struct DevexVertexOutput
{
    float4 position : SV_Position;
    float3 worldPosition : POSITION;
    // Meters from the bottom-left corner of the rectangle, before any flip.
    float2 local : TEXCOORD0;
    float viewDepth : TEXCOORD1;
    nointerpolation uint sprite : TEXCOORD2;
    float4 color : COLOR;
    // What vertex() moved the texture coordinates by.
    float2 uvOffset : TEXCOORD3;
@VARYING_FIELDS@};

DevexVertexOutput devexTransform(float4x4 viewProjection, uint vertexIndex, uint instanceIndex)
{
    let scene = data.scene;
    const uint index = data.first + instanceIndex;
    const Sprite sprite = data.sprites[index];
    devexBegin(sprite, index);
    const float2 local = corners[vertexIndex] * sprite.size;
    VERTEX = spritePlanePosition(sprite, local);
    // The rectangle of the sprite over its quad; the fragments of sliced and tiled ones map their own.
    const float2 fraction = corners[vertexIndex];
    const float2 uv = float2(lerp(sprite.uvRect.x, sprite.uvRect.z, fraction.x), lerp(sprite.uvRect.w, sprite.uvRect.y, fraction.y));
    UV = uv;
    COLOR = sprite.color;
@CALL_VERTEX@
    const float3 world = mul(sprite.world, float4(VERTEX, 0.0, 1.0)).xyz;
    DevexVertexOutput output;
    output.position = mul(viewProjection, float4(world, 1.0));
    output.worldPosition = world;
    output.local = local;
    output.viewDepth = -mul(scene->view, float4(world, 1.0)).z;
    output.sprite = index;
    output.color = COLOR;
    output.uvOffset = UV - uv;
@WRITE_VARYINGS@    return output;
}

// The built-ins of a fragment, then fragment(): COLOR starts as the texture times the colour of the
// sprite.
Sprite devexShadeFragment(DevexVertexOutput input)
{
    const Sprite sprite = data.sprites[input.sprite];
    devexBegin(sprite, input.sprite);
    FRAGCOORD = input.position;
    SCREEN_UV = input.position.xy / VIEWPORT_SIZE;
    if (sprite.texture != noTexture)
    {
        const SpriteSample at = spriteSample(sprite, input.local);
        UV = at.uv + input.uvOffset;
        COLOR = input.color * sampleTextureGrad(sprite.texture, UV, at.slopeX, at.slopeY);
    }
    else
    {
        UV = input.local / max(sprite.size, float2(1e-6)) + input.uvOffset;
        COLOR = input.color;
    }
    NORMAL_MAP = float3(spriteNormalMap(sprite, input.local) * 0.5 + 0.5, 1.0);
    NORMAL_MAP_DEPTH = 1.0;
@READ_VARYINGS@@CALL_FRAGMENT@
    return sprite;
}

[shader("vertex")]
DevexVertexOutput spriteVertex(uint vertexIndex : SV_VertexID, uint instanceIndex : SV_InstanceID)
{
    return devexTransform(data.scene->viewProjection, vertexIndex, instanceIndex);
}

[shader("fragment")]
float4 spriteFragment(DevexVertexOutput input) : SV_Target
{
    const Sprite sprite = devexShadeFragment(input);
    const bool mapped = spriteMapped(sprite) || @NORMAL_MAPPED@;
    const float3 normal = mapped ? spriteNormalOf(sprite, (NORMAL_MAP.xy * 2.0 - 1.0) * NORMAL_MAP_DEPTH) : float3(0.0, 0.0, 1.0);
    return shadeSprite(sprite, COLOR, input.worldPosition, input.viewDepth, input.position.xy, normal, mapped, @UNSHADED@,
                       @ADDITIVE@);
}

// Picking and the selection mask keep the pixels the shader covers.
[shader("vertex")]
DevexVertexOutput spritePickVertex(uint vertexIndex : SV_VertexID, uint instanceIndex : SV_InstanceID)
{
    return devexTransform(data.scene->pickViewProjection, vertexIndex, instanceIndex);
}

[shader("fragment")]
uint spritePickFragment(DevexVertexOutput input) : SV_Target
{
    const Sprite sprite = devexShadeFragment(input);
    if (COLOR.a < 0.25)
    {
        discard;
    }
    return sprite.objectId;
}

[shader("vertex")]
DevexVertexOutput spriteMaskVertex(uint vertexIndex : SV_VertexID, uint instanceIndex : SV_InstanceID)
{
    return devexTransform(data.scene->unjitteredViewProjection, vertexIndex, instanceIndex);
}

[shader("fragment")]
float spriteMaskFragment(DevexVertexOutput input) : SV_Target
{
    devexShadeFragment(input);
    if (COLOR.a < 0.25)
    {
        discard;
    }
    return 1.0;
}
)";

constexpr std::string_view particlesTemplate = R"(// Generated by Devex from @NAME@: a particles shader, for the particles an emitter simulates.

import common;
import pbr;
import custom;
import particles;

static float TIME;
static float2 VIEWPORT_SIZE;
static float3 CAMERA_POSITION_WORLD;
static uint INSTANCE_ID;
static float3 VERTEX;
static float4 COLOR;
static float2 UV;
static float PARTICLE_SIZE;
static sampler2D TEXTURE;
static float4 FRAGCOORD;
static float2 SCREEN_UV;

@UNIFORMS@
#line 1 "@NAME@"
@CODE@
#line default

void devexReadUniforms(SceneData* scene, uint material)
{
@READ_UNIFORMS@}

void devexBegin()
{
    let scene = data.scene;
    TIME = scene->time;
    VIEWPORT_SIZE = float2(scene->viewportWidth, scene->viewportHeight);
    CAMERA_POSITION_WORLD = scene->cameraPosition;
    // Particles without a texture sample white.
    TEXTURE = sampler2D(data.texture != noTexture ? data.texture : 0u);
    devexReadUniforms(scene, data.material);
}

struct DevexVertexOutput
{
    float4 position : SV_Position;
    float3 worldPosition : POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
    // Where the fragment is in its quad, from -1 to 1.
    float2 local : TEXCOORD1;
    float viewDepth : TEXCOORD2;
@PARTICLE_FIELDS@@VARYING_FIELDS@};

[shader("vertex")]
DevexVertexOutput particleVertex(uint vertexIndex : SV_VertexID, uint instanceIndex : SV_InstanceID)
{
    let scene = data.scene;
    devexBegin();
    const Particle particle = data.particles[data.first + instanceIndex];
    const ParticleCorner corner = particleCorner(particle, vertexIndex);
    INSTANCE_ID = instanceIndex;
    PARTICLE_SIZE = particle.size;
    VERTEX = corner.world;
    COLOR = particle.color;
    UV = corner.uv;
@CALL_VERTEX@
    DevexVertexOutput output;
    output.position = mul(scene->viewProjection, float4(VERTEX, 1.0));
    output.worldPosition = VERTEX;
    output.color = COLOR;
    output.uv = UV;
    output.local = corner.local;
    output.viewDepth = -mul(scene->view, float4(VERTEX, 1.0)).z;
@PARTICLE_WRITES@@WRITE_VARYINGS@    return output;
}

// COLOR starts as the colour of the particle times its texture, or a soft disc without one.
[shader("fragment")]
float4 particleFragment(DevexVertexOutput input) : SV_Target
{
    devexBegin();
@PARTICLE_READS@    VERTEX = input.worldPosition;
    FRAGCOORD = input.position;
    SCREEN_UV = input.position.xy / VIEWPORT_SIZE;
    UV = input.uv;
    COLOR = particleColor(input.color, input.uv, input.local, false);
@READ_VARYINGS@@CALL_FRAGMENT@
    return shadeParticle(COLOR, input.worldPosition, input.viewDepth, input.position, @UNSHADED@, @ADDITIVE@);
}
)";

constexpr std::string_view skyTemplate = R"(// Generated by Devex from @NAME@: a sky shader, behind the scene and in the light of the environment.

import common;
import custom;

struct DevexSky
{
    SceneData* scene;
    uint material;
    uint padding;
};

[[vk::push_constant]]
ConstantBuffer<DevexSky> devexSky;

static float TIME;
static float3 EYEDIR;
static float3 POSITION;
static float2 SKY_COORDS;
static float3 COLOR;
static float4 FRAGCOORD;
static float2 SCREEN_UV;
static float2 VIEWPORT_SIZE;
static bool LIGHT0_ENABLED;
static float3 LIGHT0_DIRECTION;
static float3 LIGHT0_COLOR;
static float LIGHT0_ENERGY;
// True while the sky is drawn for the light of the environment, where a sun disc would light twice.
static bool AT_CUBEMAP_PASS;

@UNIFORMS@
#line 1 "@NAME@"
@CODE@
#line default

void devexReadUniforms(SceneData* scene, uint material)
{
@READ_UNIFORMS@}

// The built-ins of a direction of the sky, turned as the environment is, then sky().
void devexShadeSky(float3 direction)
{
    let scene = devexSky.scene;
    TIME = scene->time;
    VIEWPORT_SIZE = float2(scene->viewportWidth, scene->viewportHeight);
    EYEDIR = direction;
    POSITION = scene->cameraPosition;
    SKY_COORDS = equirectangularUv(direction);
    COLOR = float3(0.0);
    LIGHT0_ENABLED = (scene->sunFlags & sunEnabled) != 0;
    // Towards the sun, in the frame of the sky, so that a sun drawn there stays where the light is.
    LIGHT0_DIRECTION = rotateAroundY(-scene->sunDirection, -scene->environmentRotation);
    const float energy = max(max(scene->sunIlluminance.r, scene->sunIlluminance.g), scene->sunIlluminance.b);
    LIGHT0_COLOR = energy > 0.0 ? scene->sunIlluminance / energy : float3(0.0);
    // 1 in full sunlight.
    LIGHT0_ENERGY = LIGHT0_ENABLED ? energy / 100000.0 : 0.0;
    devexReadUniforms(scene, devexSky.material);
@CALL_SKY@
}

struct DevexVertexOutput
{
    float4 position : SV_Position;
    float2 ndc : TEXCOORD0;
};

[shader("vertex")]
DevexVertexOutput skyVertex(uint vertexIndex : SV_VertexID)
{
    const float2 uv = fullscreenUv(vertexIndex);
    DevexVertexOutput output;
    output.ndc = uv * 2.0 - 1.0;
    // Depth 0 is infinitely far away with reversed depth.
    output.position = float4(output.ndc, 0.0, 1.0);
    return output;
}

[shader("fragment")]
float4 skyFragment(DevexVertexOutput input) : SV_Target
{
    let scene = devexSky.scene;
    const float4 world = mul(scene->skyInverseViewProjection, float4(input.ndc, 1.0, 1.0));
    const float3 direction = normalize(world.xyz / world.w);
    FRAGCOORD = input.position;
    SCREEN_UV = input.ndc * 0.5 + 0.5;
    AT_CUBEMAP_PASS = false;
    devexShadeSky(rotateAroundY(direction, -scene->environmentRotation));
    return float4(max(COLOR, float3(0.0)) * scene->environmentColor * scene->environmentIntensity * scene->exposure, 1.0);
}

// The whole sky laid out as an equirectangular image, as a sky texture is, for the light of the
// environment.
[shader("fragment")]
float4 bakeFragment(DevexVertexOutput input) : SV_Target
{
    const float2 uv = input.ndc * 0.5 + 0.5;
    const float angle = (uv.x - 0.5) * 2.0 * pi;
    const float polar = uv.y * pi;
    const float3 direction = float3(sin(polar) * sin(angle), cos(polar), -sin(polar) * cos(angle));
    FRAGCOORD = input.position;
    SCREEN_UV = uv;
    AT_CUBEMAP_PASS = true;
    devexShadeSky(direction);
    return float4(max(COLOR, float3(0.0)), 1.0);
}
)";

} // namespace

bool ShaderSource::defines(std::string_view function) const noexcept
{
    return std::ranges::find(functions, function) != functions.end();
}

bool ShaderSource::names(std::string_view identifier) const noexcept
{
    return std::ranges::binary_search(identifiers, identifier);
}

bool ShaderSource::hasErrors() const noexcept
{
    return std::ranges::any_of(shader.diagnostics, &ShaderDiagnostic::error);
}

std::vector<std::string_view> shaderBuiltins(ShaderKind kind)
{
    std::vector<std::string_view> names(constants.begin(), constants.end());
    switch (kind)
    {
    case ShaderKind::Spatial:
        names.insert(names.end(), spatialBuiltins.begin(), spatialBuiltins.end());
        break;
    case ShaderKind::CanvasItem:
        names.insert(names.end(), canvasBuiltins.begin(), canvasBuiltins.end());
        break;
    case ShaderKind::Particles:
        names.insert(names.end(), particleBuiltins.begin(), particleBuiltins.end());
        break;
    case ShaderKind::Sky:
        names.insert(names.end(), skyBuiltins.begin(), skyBuiltins.end());
        break;
    }
    return names;
}

ShaderSource parseShaderSource(std::string_view text)
{
    ShaderSource source;
    source.code = std::string(text);
    SourceReader reader(source);
    const std::vector<Token> tokens = tokenize(text);

    std::set<std::string, std::less<>> identifiers;
    int depth = 0;
    std::size_t at = 0;
    while (at < tokens.size())
    {
        const Token& token = tokens[at];
        const bool statement = depth == 0 && token.kind == TokenKind::Identifier &&
                               (token.text == "shader_type" || token.text == "render_mode" || token.text == "uniform" ||
                                token.text == "varying");
        if (statement)
        {
            std::size_t end = at;
            while (end < tokens.size() && tokens[end].text != ";" && tokens[end].text != "{" && tokens[end].text != "}")
            {
                ++end;
            }
            if (end >= tokens.size() || tokens[end].text != ";")
            {
                reader.error(token, std::format("{} ends with a semicolon", token.text));
                at = std::max(end, at + 1);
                continue;
            }
            const Statement read{std::span(tokens).subspan(at, end - at + 1)};
            if (token.text == "shader_type")
            {
                reader.readKind(read);
            }
            else if (token.text == "render_mode")
            {
                reader.readRenderModes(read);
            }
            else if (token.text == "uniform")
            {
                reader.readUniform(read);
            }
            else
            {
                reader.readVarying(read);
            }
            // Blanked out, keeping the lines.
            const std::size_t first = token.offset;
            const std::size_t last = tokens[end].offset + 1;
            for (std::size_t index = first; index < last; ++index)
            {
                if (source.code[index] != '\n' && source.code[index] != '\r')
                {
                    source.code[index] = ' ';
                }
            }
            at = end + 1;
            continue;
        }
        if (token.text == "{")
        {
            ++depth;
        }
        else if (token.text == "}")
        {
            depth = std::max(depth - 1, 0);
        }
        else if (token.kind == TokenKind::Identifier)
        {
            identifiers.emplace(token.text);
            // A function of the top level: a type, its name, then its parameters.
            if (depth == 0 && at > 0 && at + 1 < tokens.size() && tokens[at + 1].text == "(" &&
                tokens[at - 1].kind == TokenKind::Identifier)
            {
                source.functions.emplace_back(token.text);
            }
        }
        ++at;
    }
    if (!reader.kindRead())
    {
        source.shader.diagnostics.push_back(
            {.line = 1, .column = 1, .message = "the shader starts with its shader_type: spatial, canvas_item, particles or sky"});
    }
    source.identifiers.assign(identifiers.begin(), identifiers.end());

    ShaderData& shader = source.shader;
    const auto warnAbout = [&](std::string_view function, std::string_view message) {
        if (source.defines(function))
        {
            shader.diagnostics.push_back({.line = 0, .message = std::string(message), .error = false});
        }
    };
    switch (shader.kind)
    {
    case ShaderKind::Spatial:
        shader.transparent = shader.blend == ShaderBlend::Add || (source.names("ALPHA") && !source.names("ALPHA_SCISSOR_THRESHOLD"));
        shader.discards = source.names("discard") || source.names("ALPHA_SCISSOR_THRESHOLD");
        // Blended surfaces cast no shadow.
        shader.castsShadows = shader.castsShadows && !shader.transparent;
        break;
    case ShaderKind::CanvasItem:
        shader.transparent = true;
        shader.castsShadows = false;
        warnAbout("light", "light() is only called for spatial shaders: the 2D lights light canvas items as they do sprites");
        break;
    case ShaderKind::Particles:
        shader.transparent = true;
        shader.castsShadows = false;
        warnAbout("start", "start() is not called: the emitter simulates the particles, the shader draws them");
        warnAbout("process", "process() is not called: the emitter simulates the particles, the shader draws them");
        break;
    case ShaderKind::Sky:
        shader.castsShadows = false;
        if (!source.defines("sky") && !source.hasErrors())
        {
            shader.diagnostics.push_back({.line = 0, .message = "a sky shader writes COLOR in sky()", .error = false});
        }
        break;
    }
    std::ranges::stable_sort(shader.diagnostics, {}, &ShaderDiagnostic::line);
    return source;
}

std::string generateShaderCode(const ShaderSource& source, std::string_view sourceName)
{
    const ShaderData& shader = source.shader;
    std::string text;
    VaryingCode varyings;
    switch (shader.kind)
    {
    case ShaderKind::Spatial:
        text = spatialTemplate;
        varyings = varyingCode(source, 3);
        break;
    case ShaderKind::CanvasItem:
        text = canvasTemplate;
        varyings = varyingCode(source, 4);
        break;
    case ShaderKind::Particles:
        text = particlesTemplate;
        varyings = varyingCode(source, 5);
        break;
    case ShaderKind::Sky:
        text = skyTemplate;
        break;
    }
    const auto call = [&](std::string_view function, std::string_view indent) {
        return source.defines(function) ? std::format("{}{}();\n", indent, function) : std::string{};
    };
    const auto flag = [](bool value) { return std::string(value ? "true" : "false"); };

    std::string spatialToWorld;
    std::string spatialBefore;
    if (source.worldVertexCoords)
    {
        spatialBefore = "    VERTEX = mul(world, float4(VERTEX, 1.0)).xyz;\n"
                        "    NORMAL = transformNormal(world, NORMAL);\n"
                        "    TANGENT = mul(float3x3(world), TANGENT);\n"
                        "    BINORMAL = cross(NORMAL, TANGENT) * (devexInput.tangent.w < 0.0 ? -1.0 : 1.0);\n";
        spatialToWorld = "    result.position = VERTEX;\n"
                         "    result.normal = NORMAL;\n"
                         "    result.tangent = float4(TANGENT, devexInput.tangent.w * handedness(world));\n";
    }
    else
    {
        spatialToWorld = "    result.position = mul(world, float4(VERTEX, 1.0)).xyz;\n"
                         "    result.normal = transformNormal(world, NORMAL);\n"
                         "    result.tangent = float4(mul(float3x3(world), TANGENT), devexInput.tangent.w * handedness(world));\n";
    }
    const bool scissor = source.names("ALPHA_SCISSOR_THRESHOLD");
    const std::string depthFragment = shader.discards ? "    devexShadeFragment(input, frontFacing);\n" : std::string{};
    const std::string shadowFragment =
        shader.discards ? "\n[shader(\"fragment\")]\nvoid shadowFragment(DevexVertexOutput input, bool frontFacing : SV_IsFrontFace)\n"
                          "{\n    devexShadeFragment(input, frontFacing);\n}\n"
                        : std::string{};

    // The code first, then the rest, which it could otherwise appear in.
    std::string code = source.code;
    if (!code.empty() && code.back() != '\n')
    {
        code += '\n';
    }
    replaceAll(text, "@CODE@", "@@CODE@@");
    replaceAll(text, "@NAME@", directiveName(sourceName));
    replaceAll(text, "@UNIFORMS@", uniformDeclarations(source));
    replaceAll(text, "@READ_UNIFORMS@", uniformReads(source));
    replaceAll(text, "@VARYING_FIELDS@", varyings.fields);
    replaceAll(text, "@WRITE_VARYINGS@", varyings.writes);
    replaceAll(text, "@READ_VARYINGS@", varyings.reads);
    replaceAll(text, "@CALL_VERTEX@", call("vertex", "    "));
    replaceAll(text, "@CALL_FRAGMENT@", call("fragment", "    "));
    replaceAll(text, "@CALL_LIGHT@", call("light", "        "));
    replaceAll(text, "@CALL_SKY@", call("sky", "    "));
    replaceAll(text, "@BEFORE_VERTEX@", spatialBefore);
    replaceAll(text, "@TO_WORLD@", spatialToWorld);
    replaceAll(text, "@SCISSOR@", scissor ? "    if (ALPHA < ALPHA_SCISSOR_THRESHOLD)\n    {\n        discard;\n    }\n" : "");
    replaceAll(text, "@DEPTH_FRAGMENT@", depthFragment);
    replaceAll(text, "@SHADOW_FRAGMENT@", shadowFragment);
    replaceAll(text, "@TRANSPARENT@", flag(shader.transparent));
    replaceAll(text, "@ADDITIVE@", flag(shader.blend == ShaderBlend::Add));
    replaceAll(text, "@UNSHADED@", flag(source.unshaded));
    replaceAll(text, "@NORMAL_MAPPED@", flag(source.names("NORMAL_MAP")));
    replaceAll(text, "@AMBIENT@", flag(source.ambientLight));
    replaceAll(text, "@LIGHT_MODEL@", source.defines("light") ? "DevexShaderLight" : "PhysicalLight");
    // The particle a fragment belongs to reaches fragment() only when the code names it.
    const bool particleInputs = source.names("INSTANCE_ID") || source.names("PARTICLE_SIZE");
    replaceAll(text, "@PARTICLE_FIELDS@",
               particleInputs ? "    nointerpolation uint instance : TEXCOORD3;\n    nointerpolation float size : TEXCOORD4;\n" : "");
    replaceAll(text, "@PARTICLE_WRITES@",
               particleInputs ? "    output.instance = instanceIndex;\n    output.size = particle.size;\n" : "");
    replaceAll(text, "@PARTICLE_READS@",
               particleInputs ? "    INSTANCE_ID = input.instance;\n    PARTICLE_SIZE = input.size;\n" : "");
    replaceAll(text, "@@CODE@@", code);
    return text;
}

namespace {

std::mutex compilerMutex;
std::optional<ShaderCompiler> chosenCompiler;

// Reads the machine-readable diagnostics of slangc: code, severity, file, line, column, end line,
// end column and message, separated by tabs. A "span" line after an error gives its details.
void readDiagnostics(std::span<const std::string> lines, std::string_view sourceName, ShaderData& shader)
{
    std::optional<std::size_t> last;
    std::string lastFile;
    for (const std::string& line : lines)
    {
        std::vector<std::string_view> fields;
        for (const auto field : std::views::split(std::string_view(line), '\t'))
        {
            fields.emplace_back(field.begin(), field.end());
        }
        if (fields.size() < 8)
        {
            continue;
        }
        std::string message(fields[7]);
        for (std::size_t index = 8; index < fields.size(); ++index)
        {
            message += ' ';
            message += fields[index];
        }
        while (!message.empty() && (message.back() == '\r' || message.back() == '.'))
        {
            message.pop_back();
        }
        std::uint32_t number = 0;
        std::from_chars(fields[3].data(), fields[3].data() + fields[3].size(), number);
        std::uint32_t column = 0;
        std::from_chars(fields[4].data(), fields[4].data() + fields[4].size(), column);
        const bool inSource = fields[2] == sourceName;
        const std::string_view severity = fields[1];
        if (severity == "span")
        {
            if (last && fields[2] == lastFile && !message.empty())
            {
                shader.diagnostics[*last].message = message;
            }
            continue;
        }
        last.reset();
        const bool error = severity == "error" || severity == "fatal error";
        if ((!error && severity != "warning") || fields[0] == "E40003" || fields[0] == "E39999")
        {
            continue;
        }
        // Warnings about the generated code are not the shader's.
        if (!error && !inSource)
        {
            continue;
        }
        shader.diagnostics.push_back({.line = inSource ? number : 0, .column = inSource ? column : 0, .message = message, .error = error});
        last = shader.diagnostics.size() - 1;
        lastFile = fields[2];
    }
    // The same error reached from several entry points is reported once.
    std::vector<ShaderDiagnostic> unique;
    for (ShaderDiagnostic& diagnostic : shader.diagnostics)
    {
        if (std::ranges::find(unique, diagnostic) == unique.end())
        {
            unique.push_back(std::move(diagnostic));
        }
    }
    shader.diagnostics = std::move(unique);
}

} // namespace

ShaderCompiler defaultShaderCompiler()
{
    const std::filesystem::path base = platform::executableDirectory();
#if defined(_WIN32)
    const std::filesystem::path slangc = base / "slang" / "slangc.exe";
#else
    const std::filesystem::path slangc = base / "slang" / "bin" / "slangc";
#endif
    return {.slangc = slangc, .modules = base / "shaders" / "modules"};
}

ShaderCompiler shaderCompiler()
{
    const std::scoped_lock lock(compilerMutex);
    return chosenCompiler ? *chosenCompiler : defaultShaderCompiler();
}

void setShaderCompiler(ShaderCompiler compiler)
{
    const std::scoped_lock lock(compilerMutex);
    chosenCompiler = std::move(compiler);
}

ShaderData compileShader(std::string_view text, const std::filesystem::path& source, const ShaderCompiler& compiler,
                         const std::atomic<bool>* cancelled)
{
    const ShaderSource parsed = parseShaderSource(text);
    ShaderData shader = parsed.shader;
    if (std::ranges::any_of(shader.diagnostics, &ShaderDiagnostic::error))
    {
        return shader;
    }
    std::error_code error;
    if (compiler.slangc.empty() || !std::filesystem::exists(compiler.slangc, error))
    {
        shader.diagnostics.push_back(
            {.message = std::format("the shader compiler was not found at '{}'", core::toUtf8(compiler.slangc))});
        return shader;
    }

    // A file of its own in the temporary folder, for each compilation.
    static std::atomic<std::uint64_t> counter{0};
    const std::filesystem::path folder = std::filesystem::temp_directory_path(error) / "devex-shaders";
    std::filesystem::create_directories(folder, error);
    const std::string stem = std::format("shader-{}-{}", platform::currentProcessId(), counter.fetch_add(1));
    const std::filesystem::path input = folder / (stem + ".slang");
    const std::filesystem::path output = folder / (stem + ".spv");
    const std::string sourceName = directiveName(core::toUtf8(source.filename()));
    {
        std::ofstream file(input, std::ios::binary | std::ios::trunc);
        const std::string code = generateShaderCode(parsed, sourceName);
        file.write(code.data(), static_cast<std::streamsize>(code.size()));
        if (!file)
        {
            shader.diagnostics.push_back({.message = std::format("cannot write '{}'", core::toUtf8(input))});
            return shader;
        }
    }

    const std::vector<std::string> arguments{
        core::toUtf8(compiler.slangc),
        core::toUtf8(input),
        "-I",
        core::toUtf8(compiler.modules),
        "-target",
        "spirv",
        "-fvk-use-entrypoint-name",
        "-matrix-layout-column-major",
        "-enable-machine-readable-diagnostics",
        "-diagnostic-color",
        "never",
        "-o",
        core::toUtf8(output),
    };
    core::Result<platform::Process> process = platform::Process::start(arguments);
    if (!process)
    {
        shader.diagnostics.push_back({.message = std::format("cannot start the shader compiler: {}", process.error().message)});
        std::filesystem::remove(input, error);
        return shader;
    }
    std::vector<std::string> lines;
    std::optional<int> exitCode;
    const auto start = std::chrono::steady_clock::now();
    while (!(exitCode = process->exitCode()))
    {
        for (std::string& line : process->readLines())
        {
            lines.push_back(std::move(line));
        }
        if ((cancelled != nullptr && cancelled->load()) || std::chrono::steady_clock::now() - start > std::chrono::minutes(2))
        {
            process->kill();
            shader.diagnostics.push_back({.message = "the compilation of the shader was stopped"});
            std::filesystem::remove(input, error);
            return shader;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    for (std::string& line : process->readLines())
    {
        lines.push_back(std::move(line));
    }
    readDiagnostics(lines, sourceName, shader);

    if (*exitCode == 0)
    {
        std::ifstream file(output, std::ios::binary | std::ios::ate);
        const std::streamsize size = file ? static_cast<std::streamsize>(file.tellg()) : 0;
        if (size > 0 && size % 4 == 0)
        {
            shader.code.resize(static_cast<std::size_t>(size) / 4);
            file.seekg(0);
            file.read(reinterpret_cast<char*>(shader.code.data()), size);
            if (!file)
            {
                shader.code.clear();
            }
        }
    }
    if (shader.code.empty() && std::ranges::none_of(shader.diagnostics, &ShaderDiagnostic::error))
    {
        shader.diagnostics.push_back({.message = std::format("the shader compiler failed (exit code {})", *exitCode)});
    }
    if (!shader.code.empty())
    {
        // Errors without code are impossible; what remains are warnings.
        std::erase_if(shader.diagnostics, [](const ShaderDiagnostic& diagnostic) { return diagnostic.error; });
    }
    std::filesystem::remove(input, error);
    std::filesystem::remove(output, error);
    return shader;
}

std::string shaderTemplate(ShaderKind kind)
{
    switch (kind)
    {
    case ShaderKind::Spatial:
        return R"(shader_type spatial;

// The values a material gives this shader.
uniform float3 albedo : source_color = float3(1.0, 1.0, 1.0);
uniform float roughness : hint_range(0.0, 1.0) = 0.6;

void vertex()
{
    // Called for every vertex: VERTEX, NORMAL and UV, in the space of the mesh.
}

void fragment()
{
    // Called for every pixel: ALBEDO, ROUGHNESS, METALLIC, EMISSION, ALPHA...
    ALBEDO = albedo;
    ROUGHNESS = roughness;
}
)";
    case ShaderKind::CanvasItem:
        return R"(shader_type canvas_item;

// The values a material gives this shader.
uniform float4 tint : source_color = float4(1.0, 1.0, 1.0, 1.0);

void vertex()
{
    // Called for every corner of the sprite: VERTEX, in meters in its plane.
}

void fragment()
{
    // Called for every pixel: COLOR starts as the texture times the colour of the sprite.
    COLOR *= tint;
}
)";
    case ShaderKind::Particles:
        return R"(shader_type particles;

// The values a material gives this shader.
uniform float4 tint : source_color = float4(1.0, 1.0, 1.0, 1.0);

void vertex()
{
    // Called for every corner of a particle: VERTEX in the world, COLOR, UV.
}

void fragment()
{
    // Called for every pixel: COLOR starts as the colour of the particle times its texture.
    COLOR *= tint;
}
)";
    case ShaderKind::Sky:
        return R"(shader_type sky;

// The values a material gives this shader.
uniform float3 zenith : source_color = float3(0.2, 0.4, 0.9);
uniform float3 horizon : source_color = float3(0.8, 0.85, 0.9);

void sky()
{
    // Called for every direction of the sky: EYEDIR in, COLOR out.
    COLOR = lerp(horizon, zenith, saturate(EYEDIR.y));
}
)";
    }
    return {};
}

} // namespace devex::asset
