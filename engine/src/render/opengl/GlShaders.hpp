#pragma once

// The shaders of the OpenGL renderer, translated at the build into GLSL ES 3.00 and GLSL 3.30 (cmake/EmbedGlsl.cmake),
// by the name of their entry point.
#include <devex/core/Export.hpp>

#include <string_view>

namespace devex::render::opengl {

// The GLSL of an entry point, for OpenGL ES when `es`, for OpenGL 3.3 otherwise; empty for an unknown name.
[[nodiscard]] DEVEX_API std::string_view shaderSource(std::string_view name, bool es) noexcept;

} // namespace devex::render::opengl
