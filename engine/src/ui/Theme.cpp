#include <devex/asset/ThemeData.hpp>
#include <devex/reflection/Reflection.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/FieldValue.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/scene/UiComponents.hpp>
#include <devex/serialization/Text.hpp>
#include <devex/ui/Theme.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

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

// A value a style writes, found once: the component and the field it goes to, and the value, as
// bytes when the field holds plain bytes.
struct CompiledValue
{
    const scene::ComponentType* type = nullptr;
    const reflection::FieldInfo* field = nullptr;
    std::shared_ptr<const serialization::TextValue> value;
    std::size_t size = 0;
    // Aligned as the largest plain value, four floats.
    std::array<float, 4> bytes{};
};

struct CompiledStyle
{
    std::vector<CompiledValue> values;
};

// Finds a name among strings without making a string of it.
struct NameHash
{
    using is_transparent = void;
    [[nodiscard]] std::size_t operator()(std::string_view name) const noexcept
    {
        return std::hash<std::string_view>{}(name);
    }
};

struct CompiledTheme
{
    std::shared_ptr<const asset::ThemeData> data;
    // A style the theme lacks is kept as missing, so that it is looked for once.
    std::unordered_map<std::string, std::optional<CompiledStyle>, NameHash, std::equal_to<>> styles;
    bool used = false;
};

struct ThemeApplier::Compiled
{
    // The registry the styles were made against: once game code reloads, their types are gone.
    std::uint64_t generation = 0;
    std::unordered_map<const asset::ThemeData*, CompiledTheme> themes;
    // Every value a theme has written, read from its text once. Keyed by the text, which stays right
    // when a theme is reloaded.
    std::unordered_map<std::string, std::shared_ptr<const serialization::TextValue>> values;
};

namespace {

// The bytes a field of this kind is made of, when a copy of them is the whole value.
[[nodiscard]] std::size_t plainSize(const reflection::FieldInfo& field) noexcept
{
    switch (field.kind)
    {
    case reflection::ValueKind::Bool:
        return sizeof(bool);
    case reflection::ValueKind::Int32:
    case reflection::ValueKind::UInt32:
    case reflection::ValueKind::Float:
        return 4;
    case reflection::ValueKind::Vec2:
        return sizeof(math::Vec2);
    case reflection::ValueKind::Vec3:
        return sizeof(math::Vec3);
    case reflection::ValueKind::Vec4:
        return sizeof(math::Vec4);
    case reflection::ValueKind::Quat:
        return sizeof(math::Quat);
    case reflection::ValueKind::Enum:
        return field.enumSize;
    default:
        return 0;
    }
}

} // namespace

ThemeApplier::ThemeApplier()
    : m_compiled(std::make_unique<Compiled>())
{
}

ThemeApplier::~ThemeApplier() = default;
ThemeApplier::ThemeApplier(ThemeApplier&&) noexcept = default;
ThemeApplier& ThemeApplier::operator=(ThemeApplier&&) noexcept = default;

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
    if (m_compiled == nullptr)
    {
        m_compiled = std::make_unique<Compiled>();
    }
    Compiled& compiled = *m_compiled;
    const scene::ComponentRegistry& registry = scene::componentRegistry();
    if (compiled.generation != registry.generation())
    {
        compiled.themes.clear();
        compiled.generation = registry.generation();
    }
    for (auto& [data, theme] : compiled.themes)
    {
        theme.used = false;
    }

    // The theme of a canvas, asked once a call; and the theme of the canvas above a parent, which
    // the elements under it share.
    std::unordered_map<std::uint64_t, CompiledTheme*> canvases;
    std::unordered_map<std::uint64_t, CompiledTheme*> parents;
    const auto key = [](scene::Entity entity) {
        return (static_cast<std::uint64_t>(entity.index) << 32) | entity.generation;
    };
    const auto themeOfCanvas = [&](scene::Entity canvas, asset::AssetId id) -> CompiledTheme* {
        if (const auto found = canvases.find(key(canvas)); found != canvases.end())
        {
            return found->second;
        }
        CompiledTheme* made = nullptr;
        if (id.isValid())
        {
            if (std::shared_ptr<const asset::ThemeData> data = m_themes(id))
            {
                CompiledTheme& theme = compiled.themes[data.get()];
                theme.data = std::move(data);
                made = &theme;
            }
        }
        canvases.emplace(key(canvas), made);
        return made;
    };
    // The canvas an element belongs to is the nearest one above it, itself included.
    const auto themeOf = [&](scene::Entity entity) -> CompiledTheme* {
        if (const scene::Canvas* const canvas = scene.tryGet<scene::Canvas>(entity))
        {
            return themeOfCanvas(entity, canvas->theme);
        }
        const scene::Entity parent = scene.parent(entity);
        if (!parent.isValid())
        {
            return nullptr;
        }
        if (const auto found = parents.find(key(parent)); found != parents.end())
        {
            return found->second;
        }
        CompiledTheme* theme = nullptr;
        for (scene::Entity above = parent; above.isValid(); above = scene.parent(above))
        {
            if (const scene::Canvas* const canvas = scene.tryGet<scene::Canvas>(above))
            {
                theme = themeOfCanvas(above, canvas->theme);
                break;
            }
        }
        parents.emplace(key(parent), theme);
        return theme;
    };
    const auto styleOf = [&](CompiledTheme& theme, const std::string& name) -> const CompiledStyle* {
        auto found = theme.styles.find(name);
        if (found == theme.styles.end())
        {
            std::optional<CompiledStyle> made;
            if (const asset::ThemeStyle* const style = theme.data->find(name))
            {
                made.emplace();
                for (const asset::ThemeOverride& written : style->values)
                {
                    const scene::ComponentType* const type = registry.find(written.component);
                    const reflection::FieldInfo* const field =
                        type != nullptr && type->findMutable ? type->type->findField(written.field) : nullptr;
                    if (field == nullptr || field->list != nullptr)
                    {
                        continue;
                    }
                    auto parsed = compiled.values.find(written.value);
                    if (parsed == compiled.values.end())
                    {
                        std::optional<serialization::TextValue> read = serialization::parseValue(written.value);
                        parsed = compiled.values
                                     .emplace(written.value,
                                              read ? std::make_shared<const serialization::TextValue>(std::move(*read)) : nullptr)
                                     .first;
                    }
                    if (parsed->second == nullptr)
                    {
                        continue;
                    }
                    CompiledValue value{.type = type, .field = field, .value = parsed->second};
                    // A plain value is read into its bytes once; a value the style writes badly is
                    // left alone rather than shouting every frame.
                    const std::size_t size = plainSize(*field);
                    if (size > 0 && size <= sizeof(value.bytes))
                    {
                        if (!scene::readFieldValue(*field, *value.value, value.bytes.data()))
                        {
                            continue;
                        }
                        value.size = size;
                    }
                    made->values.push_back(std::move(value));
                }
            }
            found = theme.styles.emplace(name, std::move(made)).first;
        }
        return found->second ? &*found->second : nullptr;
    };

    for (auto [entity, element] : scene.view<scene::UiRect>())
    {
        if (element.style.empty())
        {
            continue;
        }
        CompiledTheme* const theme = themeOf(entity);
        if (theme == nullptr)
        {
            continue;
        }
        theme->used = true;
        const CompiledStyle* const style = styleOf(*theme, element.style);
        if (style == nullptr)
        {
            continue;
        }
        for (const CompiledValue& value : style->values)
        {
            void* const component = value.type->findMutable(scene, entity);
            if (component == nullptr)
            {
                continue;
            }
            void* const address = value.field->address(component);
            if (value.size > 0)
            {
                std::memcpy(address, value.bytes.data(), value.size);
            }
            else
            {
                static_cast<void>(scene::readFieldValue(*value.field, *value.value, address));
            }
        }
    }
    // A theme no element followed this call, such as one that was reloaded, is let go.
    std::erase_if(compiled.themes, [](const auto& entry) { return !entry.second.used; });
}

} // namespace devex::ui
