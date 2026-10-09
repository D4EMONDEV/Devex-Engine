#pragma once

#include <devex/core/Export.hpp>

#include <devex/asset/ShaderData.hpp>

#include <atomic>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// Shaders of a project, .dvxshader files: the structure of Godot's shading language, with the syntax
// of Slang. The first statement names what the shader draws:
//
//     shader_type spatial;
//     render_mode cull_disabled;
//
//     uniform float3 tint : source_color = float3(0.2, 0.5, 0.9);
//     uniform float speed : hint_range(0.0, 4.0) = 1.0;
//     uniform sampler2D noise : hint_default_white;
//
//     void vertex()
//     {
//         VERTEX.y += sin(TIME * speed + VERTEX.x) * 0.1;
//     }
//
//     void fragment()
//     {
//         ALBEDO = tint * texture(noise, UV).rgb;
//     }
//
// Devex declares the built-ins, reads the uniforms from the material, wraps the functions in the
// entry points of every pass the kind draws in, and compiles the whole with slangc into SPIR-V.
namespace devex::asset {

// A value vertex() hands to fragment(), interpolated across the triangle.
struct DEVEX_API ShaderVarying
{
    std::string name;
    ShaderParameterType type = ShaderParameterType::Float;

    bool operator==(const ShaderVarying&) const = default;
};

// The text of a .dvxshader file once read, before it is compiled.
struct DEVEX_API ShaderSource
{
    // The kind, the render modes and the uniforms, with the errors of the text; no code yet.
    ShaderData shader;
    std::vector<ShaderVarying> varyings;
    // The text with its shader_type, render_mode, uniform and varying statements blanked out, so
    // that the lines of the rest stay where they were.
    std::string code;
    // The functions the code defines at its top level, and every name it uses.
    std::vector<std::string> functions;
    std::vector<std::string> identifiers;
    // render_mode unshaded, ambient_light_disabled and world_vertex_coords.
    bool unshaded = false;
    bool ambientLight = true;
    bool worldVertexCoords = false;

    [[nodiscard]] bool defines(std::string_view function) const noexcept;
    [[nodiscard]] bool names(std::string_view identifier) const noexcept;
    [[nodiscard]] bool hasErrors() const noexcept;
};

// Reads the statements Devex handles; the rest of the text is left to the compiler.
[[nodiscard]] DEVEX_API ShaderSource parseShaderSource(std::string_view text);

// The Slang module that wraps the code: built-ins, uniforms, varyings and the entry points of the
// kind. Its lines name `sourceName`, so that errors land on the lines of the shader.
//
// Entry points, by kind:
// - spatial: sceneVertex and sceneFragment; prepassVertex and prepassFragment; shadowVertex and
//   localShadowVertex, with shadowFragment when the shader discards; pickVertex and pickFragment;
//   maskVertex and maskFragment;
// - canvas_item: spriteVertex, spriteFragment, spritePickVertex, spritePickFragment,
//   spriteMaskVertex and spriteMaskFragment;
// - particles: particleVertex and particleFragment;
// - sky: skyVertex, skyFragment, and bakeFragment, which lays the whole sky out as an
//   equirectangular image for the light of the environment.
[[nodiscard]] DEVEX_API std::string generateShaderCode(const ShaderSource& source, std::string_view sourceName);

// Where slangc is, and the Slang modules the generated code imports. The editor ships them in the
// folder "slang" beside its executable, and the modules in "shaders/modules".
struct DEVEX_API ShaderCompiler
{
    std::filesystem::path slangc;
    std::filesystem::path modules;
};

// The compiler beside the executable.
[[nodiscard]] DEVEX_API ShaderCompiler defaultShaderCompiler();
// The compiler imports use; the default one until it is set.
[[nodiscard]] DEVEX_API ShaderCompiler shaderCompiler();
DEVEX_API void setShaderCompiler(ShaderCompiler compiler);

// Compiles the text of a .dvxshader file named `source`. The errors of the text and of the compiler
// land in the diagnostics, on its lines, and leave the shader without code. Waits for slangc, which
// takes a second or so; `cancelled` stops it.
[[nodiscard]] DEVEX_API ShaderData compileShader(std::string_view text, const std::filesystem::path& source,
                                                 const ShaderCompiler& compiler = shaderCompiler(),
                                                 const std::atomic<bool>* cancelled = nullptr);

// The text of a new shader of the kind, as the editor creates it.
[[nodiscard]] DEVEX_API std::string shaderTemplate(ShaderKind kind);

// The built-ins the code of a kind may name, for the text editor.
[[nodiscard]] DEVEX_API std::vector<std::string_view> shaderBuiltins(ShaderKind kind);

} // namespace devex::asset
