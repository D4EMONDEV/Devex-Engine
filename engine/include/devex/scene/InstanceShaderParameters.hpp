#pragma once

#include <devex/core/Export.hpp>

#include <devex/math/Math.hpp>
#include <devex/scene/Entity.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The values an object gives the instance uniforms of the shader it draws with, as Godot's
// set_instance_shader_parameter: each renderer (MeshRenderer, SkinnedMeshRenderer, SpriteRenderer,
// Tilemap) keeps them by name in two hidden fields, and the uniforms it gives none keep the
// defaults of the shader.
namespace devex::scene {

class Scene;
struct ComponentType;

// The fields that keep them: the names, and the values in the same order.
inline constexpr std::string_view instanceShaderParametersField = "instance_shader_parameters";
inline constexpr std::string_view instanceShaderValuesField = "instance_shader_values";

// The values a component keeps, changed in place.
struct DEVEX_API InstanceShaderValues
{
    std::vector<std::string>* names = nullptr;
    std::vector<math::Vec4>* values = nullptr;

    [[nodiscard]] bool isValid() const noexcept
    {
        return names != nullptr && values != nullptr;
    }
    // The value given to the uniform, nullopt when it keeps its default.
    [[nodiscard]] std::optional<math::Vec4> find(std::string_view name) const;
    // Gives the uniform a value, in place of the one it had.
    void set(std::string_view name, math::Vec4 value) const;
    // Lets the uniform take its default again; false when it had no value.
    bool erase(std::string_view name) const;
};

// The values of a component, by its reflection; invalid for a type that keeps none.
[[nodiscard]] DEVEX_API InstanceShaderValues instanceShaderValues(const ComponentType& type, void* component);

// Gives the value to every renderer of the entity; false when it has none.
DEVEX_API bool setInstanceShaderParameter(Scene& scene, Entity entity, std::string_view name, math::Vec4 value);
// The value the first renderer of the entity gives the uniform, nullopt when none gives one: the
// default of the shader applies then.
[[nodiscard]] DEVEX_API std::optional<math::Vec4> instanceShaderParameter(Scene& scene, Entity entity, std::string_view name);
// Lets the renderers of the entity take the default of the uniform again; false when none gave a value.
DEVEX_API bool resetInstanceShaderParameter(Scene& scene, Entity entity, std::string_view name);

} // namespace devex::scene
