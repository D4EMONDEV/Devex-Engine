#pragma once

// The WGSL of the WebGPU renderer, compiled by slangc from shaders/webgpu_*.slang at the build and kept in the program:
// a browser has no folder to load it from.
#include <string_view>

namespace devex::render::webgpu {

// The source of a shader by the name of its file without extension, such as "webgpu_sprite"; empty when there is none.
[[nodiscard]] std::string_view shaderSource(std::string_view name) noexcept;

} // namespace devex::render::webgpu
