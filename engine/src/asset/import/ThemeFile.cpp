#include <devex/asset/import/ThemeFile.hpp>
#include <devex/serialization/Text.hpp>

#include <algorithm>
#include <optional>

namespace devex::asset {
namespace {

using serialization::TextSection;
using serialization::TextValue;

constexpr std::int64_t themeFormatVersion = 1;

// The style of that name, added to the theme when it is met for the first time. A style is
// written once per component, so the same name comes back as often as it has components.
[[nodiscard]] ThemeStyle& styleNamed(ThemeData& theme, const std::string& name)
{
    const auto found = std::ranges::find(theme.styles, name, &ThemeStyle::name);
    if (found != theme.styles.end())
    {
        return *found;
    }
    theme.styles.push_back(ThemeStyle{.name = name});
    return theme.styles.back();
}

} // namespace

core::Result<ThemeData> parseThemeFile(std::string_view text)
{
    const core::Result<serialization::TextDocument> document = serialization::parseText(text);
    if (!document)
    {
        return std::unexpected(document.error());
    }

    ThemeData theme;
    bool headerSeen = false;
    for (const TextSection& section : document->sections)
    {
        if (section.type == "theme")
        {
            const TextValue* const format = section.findAttribute("format");
            const std::optional<std::int64_t> version =
                format != nullptr ? serialization::asInteger(*format) : std::nullopt;
            if (!version || *version > themeFormatVersion)
            {
                return core::makeError(core::ErrorCode::Unsupported,
                                       "unknown theme format, a newer Devex may be needed");
            }
            headerSeen = true;
            continue;
        }
        if (section.type != "style")
        {
            return core::makeError(core::ErrorCode::Parse,
                                   "line {}: a theme holds [theme] and [style] sections, not [{}]",
                                   section.line, section.type);
        }

        const TextValue* const name = section.findAttribute("name");
        const std::string* const named =
            name != nullptr ? serialization::asString(*name) : nullptr;
        const TextValue* const component = section.findAttribute("component");
        const std::string* const componentName =
            component != nullptr ? serialization::asString(*component) : nullptr;
        if (named == nullptr || named->empty())
        {
            return core::makeError(core::ErrorCode::Parse, "line {}: a style needs a name",
                                   section.line);
        }
        if (componentName == nullptr || componentName->empty())
        {
            return core::makeError(core::ErrorCode::Parse,
                                   "line {}: the style '{}' names no component; write "
                                   "[style name=\"{}\" component=\"UiImage\"]",
                                   section.line, *named, *named);
        }
        ThemeStyle& style = styleNamed(theme, *named);
        for (const serialization::TextProperty& property : section.properties)
        {
            // The same field twice would make the look a matter of order.
            if (std::ranges::any_of(style.values, [&](const ThemeOverride& written) {
                    return written.component == *componentName && written.field == property.key;
                }))
            {
                return core::makeError(core::ErrorCode::Parse,
                                       "line {}: the style '{}' sets {}.{} twice", property.line,
                                       *named, *componentName, property.key);
            }
            style.values.push_back({.component = *componentName,
                                    .field = property.key,
                                    .value = serialization::formatValue(property.value)});
        }
    }
    if (!headerSeen)
    {
        return core::makeError(core::ErrorCode::Parse, "a theme file starts with [theme format=1]");
    }
    if (core::Result<void> valid = validate(theme); !valid)
    {
        return std::unexpected(valid.error());
    }
    return theme;
}

std::string writeThemeFile(const ThemeData& theme)
{
    serialization::TextDocument document;
    document.sections.push_back({.type = "theme",
                                 .attributes = {{.key = "format",
                                                 .value = TextValue(themeFormatVersion)}}});
    for (const ThemeStyle& style : theme.styles)
    {
        // One section per component the style touches, in the order it touches them.
        std::vector<std::string> components;
        for (const ThemeOverride& written : style.values)
        {
            if (std::ranges::find(components, written.component) == components.end())
            {
                components.push_back(written.component);
            }
        }
        for (const std::string& component : components)
        {
            TextSection section{.type = "style",
                                .attributes = {{.key = "name", .value = TextValue(style.name)},
                                               {.key = "component", .value = TextValue(component)}}};
            for (const ThemeOverride& written : style.values)
            {
                if (written.component != component)
                {
                    continue;
                }
                // The value was kept as it was written, and goes back as the value it names.
                section.properties.push_back(
                    {.key = written.field,
                     .value = serialization::parseValue(written.value)
                                  .value_or(TextValue(written.value))});
            }
            document.sections.push_back(std::move(section));
        }
    }
    return serialization::writeText(document);
}

} // namespace devex::asset
