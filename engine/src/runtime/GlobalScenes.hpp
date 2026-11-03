#pragma once

#include <devex/asset/Project.hpp>
#include <devex/core/Error.hpp>
#include <devex/scene/Scene.hpp>

#include <functional>
#include <span>
#include <string>

namespace devex::runtime::detail
{

// Serialized with runtime snapshots, so restoring a save never creates a second autoload.
struct GlobalRoot
{
    std::string name;
};
DEVEX_DECLARE_ENGINE_REFLECTION(GlobalRoot);

DEVEX_API void registerGlobalRoot();
using InstantiateGlobal =
    std::function<core::Result<scene::Entity>(scene::Scene&, const std::string&)>;
[[nodiscard]] DEVEX_API core::Result<void> startGlobals(scene::Scene& scene,
                                                        std::span<const asset::GlobalScene> globals,
                                                        const InstantiateGlobal& instantiate);
[[nodiscard]] DEVEX_API bool hasGlobals(const scene::Scene& scene);
// Keeps live autoload objects and handles. Only incoming level entities are deserialized.
[[nodiscard]] DEVEX_API core::Result<void> replaceLevelKeepingGlobals(scene::Scene& scene,
                                                                      const scene::Scene& level);

} // namespace devex::runtime::detail
