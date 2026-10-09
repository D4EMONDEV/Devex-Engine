#pragma once

#include <devex/core/Export.hpp>

#include <devex/asset/ShaderGraphData.hpp>
#include <devex/asset/import/ShaderFile.hpp>
#include <devex/core/Error.hpp>

#include <atomic>
#include <filesystem>
#include <string>
#include <string_view>

namespace devex::asset {

// A shader graph written by the editor. The settings of a node are texts or vectors; the values of
// its inputs no link feeds come in order, as `values`:
//
//     [shader_graph format=1 type="spatial"]
//     render_modes = list("cull_disabled")
//
//     [node id=1 type="output" function="fragment"]
//     position = vec2(400, 0)
//
//     [node id=2 type="parameter" function="fragment"]
//     position = vec2(0, 0)
//     name = "tint"
//     type = "color"
//     value = vec4(1, 0.5, 0.2, 1)
//
//     [link from=2 from_port=0 to=1 to_port=0]
[[nodiscard]] DEVEX_API core::Result<ShaderGraphData> parseShaderGraphFile(std::string_view text);
[[nodiscard]] DEVEX_API std::string writeShaderGraphFile(const ShaderGraphData& graph);

// Turns a graph into code and compiles it, as compileShader does a .dvxshader. Mistakes of the graph
// and errors of the compiler name the node they come from.
[[nodiscard]] DEVEX_API ShaderData compileShaderGraph(const ShaderGraphData& graph, const std::filesystem::path& source,
                                                      const ShaderCompiler& compiler = shaderCompiler(),
                                                      const std::atomic<bool>* cancelled = nullptr);

} // namespace devex::asset
