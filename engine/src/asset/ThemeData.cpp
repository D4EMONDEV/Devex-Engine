#include <devex/asset/ThemeData.hpp>

#include <algorithm>

namespace devex::asset {

const ThemeStyle* ThemeData::find(std::string_view name) const noexcept
{
    const auto found = std::ranges::find(styles, name, &ThemeStyle::name);
    return found != styles.end() ? &*found : nullptr;
}

core::Result<void> validate(const ThemeData& theme)
{
    for (const ThemeStyle& style : theme.styles)
    {
        if (style.name.empty())
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "a style has no name");
        }
        for (const ThemeOverride& value : style.values)
        {
            if (value.component.empty() || value.field.empty())
            {
                return core::makeError(core::ErrorCode::InvalidArgument,
                                       "the style '{}' sets a value without naming a field",
                                       style.name);
            }
        }
    }
    // Two styles of the same name would make the one an element follows a matter of order.
    for (std::size_t index = 1; index < theme.styles.size(); ++index)
    {
        const auto& name = theme.styles[index].name;
        if (std::ranges::any_of(theme.styles.begin(), theme.styles.begin() + index,
                                [&name](const ThemeStyle& other) { return other.name == name; }))
        {
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "the style '{}' is written twice", name);
        }
    }
    return {};
}

} // namespace devex::asset
