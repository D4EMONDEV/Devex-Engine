#include "GlobalScenes.hpp"
#include <devex/runtime/Globals.hpp>

#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/SceneSerializer.hpp>

#include <algorithm>
#include <unordered_set>
#include <vector>

namespace devex::runtime::detail
{

DEVEX_REFLECT(GlobalRoot) { type.field("name", &GlobalRoot::name, {.hidden = true}); }

void registerGlobalRoot() { scene::registerComponent<GlobalRoot>(); }

bool hasGlobals(const scene::Scene& scene)
{
    for (const auto entry : scene.view<const GlobalRoot>())
    {
        static_cast<void>(entry);
        return true;
    }
    return false;
}

core::Result<void> startGlobals(scene::Scene& scene, std::span<const asset::GlobalScene> globals,
                                const InstantiateGlobal& instantiate)
{
    registerGlobalRoot();
    if (auto valid = asset::validateGlobals(globals); !valid)
        return valid;
    std::vector<scene::Entity> created;
    std::vector<scene::Entity> ordered;
    auto before = scene.firstRoot();
    while (before.isValid() && scene.has<GlobalRoot>(before))
        before = scene.nextSibling(before);
    for (const auto& global : globals)
    {
        if (!global.enabled)
            continue;
        scene::Entity existing;
        for (const auto [entity, root] : scene.view<GlobalRoot>())
            if (root.name == global.name)
                existing = entity;
        if (existing.isValid())
        {
            ordered.push_back(existing);
            continue;
        }
        auto root = instantiate(scene, global.path);
        if (!root)
        {
            for (const auto entity : created)
                scene.destroyEntity(entity);
            return core::makeError(root.error().code, "Global {}: {}", global.name,
                                   root.error().message);
        }
        scene.setName(*root, global.name);
        scene.add<GlobalRoot>(*root, GlobalRoot{global.name});
        created.push_back(*root);
        ordered.push_back(*root);
    }
    // Autoloads precede the level in scene traversal, in project order.
    for (const auto entity : ordered)
        if (entity != before)
            static_cast<void>(scene.setParent(entity, {}, before));
    scene.updateTransforms();
    return {};
}

core::Result<void> replaceLevelKeepingGlobals(scene::Scene& scene, const scene::Scene& level)
{
    std::unordered_set<core::Uuid> kept;
    std::vector<scene::Entity> globals;
    const auto collect = [&](auto&& self, scene::Entity entity) -> void
    {
        kept.insert(scene.uuid(entity));
        for (auto child = scene.firstChild(entity); child.isValid();
             child = scene.nextSibling(child))
            self(self, child);
    };
    for (const auto [entity, root] : scene.view<GlobalRoot>())
    {
        static_cast<void>(root);
        globals.push_back(entity);
        collect(collect, entity);
    }
    // Check conflicts before removing any current level entity.
    const auto check = [&](auto&& self, scene::Entity entity) -> bool
    {
        if (kept.contains(level.uuid(entity)))
            return false;
        for (auto child = level.firstChild(entity); child.isValid();
             child = level.nextSibling(child))
            if (!self(self, child))
                return false;
        return true;
    };
    std::vector<std::string> trees;
    for (auto root = level.firstRoot(); root.isValid(); root = level.nextSibling(root))
    {
        if (!check(check, root))
            return core::makeError(core::ErrorCode::InvalidState,
                                   "The level reuses the UUID of a global entity");
        trees.push_back(scene::saveEntityTree(level, root));
    }
    for (const auto entity : globals)
        static_cast<void>(scene.setParent(entity, {}));
    for (auto root = scene.firstRoot(); root.isValid();)
    {
        const auto next = scene.nextSibling(root);
        if (!scene.has<GlobalRoot>(root))
            scene.destroyEntity(root);
        root = next;
    }
    for (const auto& text : trees)
        if (auto loaded = scene::loadEntityTree(scene, text, {}); !loaded)
            return std::unexpected(loaded.error());
    scene.setKind(level.kind());
    scene.updateTransforms();
    return {};
}

} // namespace devex::runtime::detail

namespace devex::runtime
{

scene::Entity findGlobal(const scene::Scene& scene, std::string_view name) noexcept
{
    for (const auto [entity, root] : scene.view<const detail::GlobalRoot>())
        if (root.name == name)
            return entity;
    return {};
}

} // namespace devex::runtime
