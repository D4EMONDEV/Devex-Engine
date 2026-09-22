#include <devex/asset/ThemeData.hpp>
#include <devex/reflection/Reflection.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/FieldValue.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/scene/UiComponents.hpp>
#include <devex/serialization/Text.hpp>
#include <devex/ui/Theme.hpp>

#include <algorithm>
#include <optional>
#include <utility>

namespace devex::ui {

bool ElementStyle::sets(std::string_view component, std::string_view field) const noexcept
{
    return style != nullptr && std::ranges::any_of(style->values, [&](const asset::ThemeOverride& written) {
               return written.component == component && written.field == field;
           });
}

ElementStyle styleOf(const scene::Scene& scene, scene::Entity entity, const ThemeSource& themes)
{
    ElementStyle found;
    const scene::UiRect* const element = scene.tryGet<scene::UiRect>(entity);
    if (element == nullptr || element->style.empty())
    {
        return found;
    }
    found.name = element->style;
    // The canvas the element belongs to is the nearest one above it, itself included.
    for (scene::Entity above = entity; above.isValid(); above = scene.parent(above))
    {
        if (const scene::Canvas* const canvas = scene.tryGet<scene::Canvas>(above))
        {
            found.theme = canvas->theme;
            break;
        }
    }
    if (found.theme.isValid() && themes)
    {
        found.data = themes(found.theme);
        found.style = found.data != nullptr ? found.data->find(found.name) : nullptr;
    }
    return found;
}

void ThemeApplier::setThemes(ThemeSource themes)
{
    m_themes = std::move(themes);
}

const ThemeSource& ThemeApplier::themes() const noexcept
{
    return m_themes;
}

void ThemeApplier::apply(scene::Scene& scene)
{
    if (!m_themes)
    {
        return;
    }
    const scene::ComponentRegistry& registry = scene::componentRegistry();
    for (auto [entity, element] : scene.view<scene::UiRect>())
    {
        if (element.style.empty())
        {
            continue;
        }
        const ElementStyle style = styleOf(scene, entity, m_themes);
        if (style.style == nullptr)
        {
            continue;
        }
        for (const asset::ThemeOverride& written : style.style->values)
        {
            const scene::ComponentType* const type = registry.find(written.component);
            void* const component =
                type != nullptr && type->findMutable ? type->findMutable(scene, entity) : nullptr;
            const reflection::FieldInfo* const field =
                component != nullptr ? type->type->findField(written.field) : nullptr;
            if (field == nullptr || field->list != nullptr)
            {
                continue;
            }
            auto parsed = m_values.find(written.value);
            if (parsed == m_values.end())
            {
                std::optional<serialization::TextValue> read = serialization::parseValue(written.value);
                parsed = m_values
                             .emplace(written.value,
                                      read ? std::make_shared<const serialization::TextValue>(std::move(*read))
                                           : nullptr)
                             .first;
            }
            if (parsed->second == nullptr)
            {
                continue;
            }
            // A value the style writes badly is left alone rather than shouting every frame.
            static_cast<void>(scene::readFieldValue(*field, *parsed->second, field->address(component)));
        }
    }
}

} // namespace devex::ui
