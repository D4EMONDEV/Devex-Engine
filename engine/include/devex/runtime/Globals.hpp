#pragma once

#include <devex/core/Export.hpp>
#include <devex/scene/Scene.hpp>

#include <string_view>

namespace devex::runtime
{

// Finds the live root of a configured autoload by its project name, regardless of entity renames.
// Invalid when the global is disabled, missing or the game is not playing.
[[nodiscard]] DEVEX_API scene::Entity findGlobal(const scene::Scene& scene,
                                                 std::string_view name) noexcept;

} // namespace devex::runtime
