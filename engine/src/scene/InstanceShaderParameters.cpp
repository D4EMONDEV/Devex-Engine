#include <devex/scene/InstanceShaderParameters.hpp>

#include <devex/scene/AnimationComponents.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/scene/SpriteComponents.hpp>
#include <devex/scene/TilemapComponents.hpp>

#include <algorithm>
#include <cstddef>
#include <iterator>

namespace devex::scene {

namespace {

template <typename Renderer>
[[nodiscard]] InstanceShaderValues valuesOf(Renderer* renderer) noexcept
{
    return renderer != nullptr ? InstanceShaderValues{&renderer->instanceShaderParameters, &renderer->instanceShaderValues}
                               : InstanceShaderValues{};
}

// The renderers of the entity, in the order the first one is looked for.
[[nodiscard]] std::vector<InstanceShaderValues> renderersOf(Scene& scene, Entity entity)
{
    std::vector<InstanceShaderValues> found;
    if (!scene.isAlive(entity))
    {
        return found;
    }
    for (const InstanceShaderValues values : {valuesOf(scene.tryGet<MeshRenderer>(entity)), valuesOf(scene.tryGet<SkinnedMeshRenderer>(entity)),
                                              valuesOf(scene.tryGet<SpriteRenderer>(entity)), valuesOf(scene.tryGet<Tilemap>(entity))})
    {
        if (values.isValid())
        {
            found.push_back(values);
        }
    }
    return found;
}

} // namespace

std::optional<math::Vec4> InstanceShaderValues::find(std::string_view name) const
{
    if (!isValid())
    {
        return std::nullopt;
    }
    const auto found = std::ranges::find(*names, name);
    const auto index = static_cast<std::size_t>(std::distance(names->begin(), found));
    return found != names->end() && index < values->size() ? std::optional((*values)[index]) : std::nullopt;
}

void InstanceShaderValues::set(std::string_view name, math::Vec4 value) const
{
    if (!isValid())
    {
        return;
    }
    // The two lists stay as long as each other, whatever a file or code left in them.
    values->resize(names->size(), math::Vec4{0.0f});
    const auto found = std::ranges::find(*names, name);
    if (found != names->end())
    {
        (*values)[static_cast<std::size_t>(std::distance(names->begin(), found))] = value;
        return;
    }
    names->emplace_back(name);
    values->push_back(value);
}

bool InstanceShaderValues::erase(std::string_view name) const
{
    if (!isValid())
    {
        return false;
    }
    const auto found = std::ranges::find(*names, name);
    if (found == names->end())
    {
        return false;
    }
    const auto index = std::distance(names->begin(), found);
    names->erase(found);
    if (static_cast<std::size_t>(index) < values->size())
    {
        values->erase(values->begin() + index);
    }
    return true;
}

InstanceShaderValues instanceShaderValues(const ComponentType& type, void* component)
{
    if (component == nullptr || type.type == nullptr)
    {
        return {};
    }
    const reflection::FieldInfo* const names = type.type->findField(instanceShaderParametersField);
    const reflection::FieldInfo* const values = type.type->findField(instanceShaderValuesField);
    if (names == nullptr || values == nullptr || names->list == nullptr || values->list == nullptr ||
        names->kind != reflection::ValueKind::String || values->kind != reflection::ValueKind::Vec4)
    {
        return {};
    }
    return {static_cast<std::vector<std::string>*>(names->address(component)),
            static_cast<std::vector<math::Vec4>*>(values->address(component))};
}

bool setInstanceShaderParameter(Scene& scene, Entity entity, std::string_view name, math::Vec4 value)
{
    const std::vector<InstanceShaderValues> renderers = renderersOf(scene, entity);
    for (const InstanceShaderValues& values : renderers)
    {
        values.set(name, value);
    }
    return !renderers.empty();
}

std::optional<math::Vec4> instanceShaderParameter(Scene& scene, Entity entity, std::string_view name)
{
    for (const InstanceShaderValues& values : renderersOf(scene, entity))
    {
        if (const std::optional<math::Vec4> value = values.find(name))
        {
            return value;
        }
    }
    return std::nullopt;
}

bool resetInstanceShaderParameter(Scene& scene, Entity entity, std::string_view name)
{
    bool erased = false;
    for (const InstanceShaderValues& values : renderersOf(scene, entity))
    {
        erased = values.erase(name) || erased;
    }
    return erased;
}

} // namespace devex::scene
