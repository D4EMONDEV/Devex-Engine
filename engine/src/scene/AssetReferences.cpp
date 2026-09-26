#include <devex/scene/AssetReferences.hpp>

#include <devex/scene/ComponentRegistry.hpp>

#include <unordered_set>

namespace devex::scene {
namespace {

void collect(const Scene& scene, Entity entity, std::unordered_set<asset::AssetId>& seen,
             std::vector<asset::AssetId>& assets)
{
    const auto add = [&](const void* address) {
        const asset::AssetId id = *static_cast<const asset::AssetId*>(address);
        if (id.isValid() && seen.insert(id).second)
        {
            assets.push_back(id);
        }
    };
    for (; entity.isValid(); entity = scene.nextSibling(entity))
    {
        for (const ComponentType& type : componentRegistry().types())
        {
            const void* const component = type.find(scene, entity);
            if (component == nullptr)
            {
                continue;
            }
            for (const reflection::FieldInfo& field : type.type->fields)
            {
                if (field.kind != reflection::ValueKind::AssetId)
                {
                    continue;
                }
                // Reading a list does not change it: element() only lacks a const overload.
                void* const address = const_cast<void*>(field.address(component));
                if (field.list == nullptr)
                {
                    add(address);
                    continue;
                }
                for (std::size_t index = 0; index < field.list->size(address); ++index)
                {
                    add(field.list->element(address, index));
                }
            }
        }
        collect(scene, scene.firstChild(entity), seen, assets);
    }
}

} // namespace

std::vector<asset::AssetId> referencedAssets(const Scene& scene)
{
    std::unordered_set<asset::AssetId> seen;
    std::vector<asset::AssetId> assets;
    collect(scene, scene.firstRoot(), seen, assets);
    return assets;
}

} // namespace devex::scene
