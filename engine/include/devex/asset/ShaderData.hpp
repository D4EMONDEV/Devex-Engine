#pragma once

#include <devex/core/Export.hpp>

#include <devex/math/Math.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace devex::asset {

// The extension of the shaders a project writes.
inline constexpr std::string_view shaderExtension = ".dvxshader";

// What a shader draws, as the shader_type of its first line says. The values are stored in cooked
// files: never reorder them.
enum class ShaderKind : std::uint8_t
{
    // The surfaces of meshes: vertex(), fragment() and light().
    Spatial = 0,
    // Sprites and tilemaps: vertex() and fragment().
    CanvasItem = 1,
    // The particles of an emitter, simulated by the emitter and drawn by vertex() and fragment().
    Particles = 2,
    // The sky behind the scene: sky().
    Sky = 3,
};

// "spatial", "canvas_item", "particles" or "sky".
[[nodiscard]] DEVEX_API std::string_view toString(ShaderKind kind) noexcept;
[[nodiscard]] DEVEX_API std::optional<ShaderKind> parseShaderKind(std::string_view text) noexcept;

// The type of a uniform, as Slang names it. The values are stored in cooked files.
enum class ShaderParameterType : std::uint8_t
{
    Float = 0,
    Float2 = 1,
    Float3 = 2,
    Float4 = 3,
    Int = 4,
    Bool = 5,
    // A texture of the project, sampled with texture(); the sampler chosen at its import goes with it.
    Texture = 6,
};

// "float", "float2", "float3", "float4", "int", "bool" or "sampler2D".
[[nodiscard]] DEVEX_API std::string_view toString(ShaderParameterType type) noexcept;
[[nodiscard]] DEVEX_API std::optional<ShaderParameterType> parseShaderParameterType(std::string_view text) noexcept;
// The numbers a value of the type holds: 1 to 4, and 1 for a texture.
[[nodiscard]] DEVEX_API std::uint32_t componentCount(ShaderParameterType type) noexcept;

// How the inspector edits a uniform, and what a texture left empty samples as.
enum class ShaderHint : std::uint8_t
{
    None = 0,
    // hint_range(lowest, highest[, step]): a slider.
    Range = 1,
    // source_color: a colour picker, linear like every colour of the engine.
    Color = 2,
    // hint_default_white, hint_default_black and hint_normal: what an empty texture samples as.
    // White is the default of textures.
    White = 3,
    Black = 4,
    Normal = 5,
};

// A uniform of a shader: what materials give it, and where the shader reads it. Each takes one
// slot of four numbers in the parameters of the material, in the order they are declared.
struct DEVEX_API ShaderParameter
{
    std::string name;
    ShaderParameterType type = ShaderParameterType::Float;
    ShaderHint hint = ShaderHint::None;
    // The value of a material that gives none; unused for textures.
    math::Vec4 defaultValue{0.0f};
    // hint_range: the lowest value, the highest, and the step of the slider, 0 for any.
    math::Vec3 range{0.0f, 1.0f, 0.0f};

    bool operator==(const ShaderParameter&) const = default;
};

// How the blended pixels of a shader cover what is behind them.
enum class ShaderBlend : std::uint8_t
{
    // blend_mix: by their alpha.
    Mix = 0,
    // blend_add: their light adds to it.
    Add = 1,
};

// Which faces of a spatial shader are drawn.
enum class ShaderCull : std::uint8_t
{
    // cull_back: the faces turned towards the camera.
    Back = 0,
    // cull_front: those turned away.
    Front = 1,
    // cull_disabled: both, the back ones lit as seen from their side.
    Disabled = 2,
};

// An error or a warning of the compiler, on a line of the shader as it was written.
struct DEVEX_API ShaderDiagnostic
{
    // From 1; 0 for the shader as a whole.
    std::uint32_t line = 0;
    std::uint32_t column = 0;
    std::string message;
    bool error = true;
    // The node of a shader graph it is about, 0 for none.
    std::uint32_t node = 0;

    bool operator==(const ShaderDiagnostic&) const = default;
};

// A shader compiled from a .dvxshader file: what the renderer builds its pipelines from, and what
// materials give values to. A shader that did not compile keeps its diagnostics and no code;
// materials using it draw as the default material.
struct DEVEX_API ShaderData
{
    ShaderKind kind = ShaderKind::Spatial;
    // The render_mode of the shader, and what its code shows.
    ShaderCull cull = ShaderCull::Back;
    ShaderBlend blend = ShaderBlend::Mix;
    // Spatial: drawn among the blended surfaces, because it writes ALPHA or adds its light. Canvas
    // items and particles always are.
    bool transparent = false;
    // Spatial: whether it casts shadows (no shadows_disabled); transparent surfaces never do.
    bool castsShadows = true;
    // Spatial: whether fragment() may discard pixels, with discard or ALPHA_SCISSOR_THRESHOLD. The
    // depth passes then run it too.
    bool discards = false;
    std::vector<ShaderParameter> parameters;
    // SPIR-V, with the entry points of every pass the kind draws in; empty when it did not compile.
    std::vector<std::uint32_t> code;
    std::vector<ShaderDiagnostic> diagnostics;

    [[nodiscard]] bool compiled() const noexcept
    {
        return !code.empty();
    }
    // The uniform of that name, or null.
    [[nodiscard]] const ShaderParameter* findParameter(std::string_view name) const noexcept;

    bool operator==(const ShaderData&) const = default;
};

} // namespace devex::asset
