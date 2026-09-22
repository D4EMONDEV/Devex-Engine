#pragma once

#include <devex/core/Error.hpp>

#include <string>
#include <string_view>
#include <vector>

// The look of an interface, kept away from the entities that wear it. A theme holds named styles,
// a canvas names the theme it follows, and every element names the style it takes from it.
namespace devex::asset {

inline constexpr std::string_view themeExtension = ".dvxtheme";

// One value a style sets: the component it belongs to, the field inside it, and the value written
// the way a text file writes it, such as "vec4(0.1, 0.1, 0.12, 0.9)".
struct ThemeOverride
{
    std::string component;
    std::string field;
    std::string value;
};

// A look an element can follow: "panel", "title", "primary".
struct ThemeStyle
{
    std::string name;
    std::vector<ThemeOverride> values;
};

struct ThemeData
{
    std::vector<ThemeStyle> styles;

    // The style of that name, or nothing when the theme does not carry it.
    [[nodiscard]] const ThemeStyle* find(std::string_view name) const noexcept;
};

[[nodiscard]] core::Result<void> validate(const ThemeData& theme);

} // namespace devex::asset
