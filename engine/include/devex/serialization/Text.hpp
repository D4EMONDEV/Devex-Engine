#pragma once

#include <devex/core/Export.hpp>

#include <devex/core/Error.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// The readable text format shared by .dvx* files. A document is a list of sections:
//
//     # Comment
//     [entity uuid="6f1c2a9e-3b7d-4e21-9a55-0c8d7e4f1b23" name="Player"]
//     position = vec3(0, 1.5, -2)
//     visible = true
//
// Values are booleans, integers, real numbers, strings, or calls such as vec3(...) whose
// arguments are values. A section header and a property each fit on one line.
namespace devex::serialization {

struct TextValue;

struct DEVEX_API TextCall
{
    std::string name;
    std::vector<TextValue> arguments;

    bool operator==(const TextCall&) const = default;
};

struct DEVEX_API TextValue : std::variant<bool, std::int64_t, double, std::string, TextCall>
{
    using variant::variant;
};

[[nodiscard]] DEVEX_API TextValue makeCall(std::string name, std::vector<TextValue> arguments);

// Integers and real numbers both convert to double.
[[nodiscard]] DEVEX_API std::optional<double> asNumber(const TextValue& value) noexcept;
[[nodiscard]] DEVEX_API std::optional<std::int64_t> asInteger(const TextValue& value) noexcept;
[[nodiscard]] DEVEX_API std::optional<bool> asBool(const TextValue& value) noexcept;
[[nodiscard]] DEVEX_API const std::string* asString(const TextValue& value) noexcept;
// Returns the call when the value is a call with that name.
[[nodiscard]] DEVEX_API const TextCall* asCall(const TextValue& value, std::string_view name) noexcept;

// Formats the value as it appears in a document. Real numbers use the shortest text that
// reads back to the same double.
[[nodiscard]] DEVEX_API std::string formatValue(const TextValue& value);

struct DEVEX_API TextProperty
{
    std::string key;
    TextValue value;
    // 1-based line in the parsed source, 0 for properties built in code.
    std::uint32_t line = 0;

    // The same key and value, wherever they were read.
    bool operator==(const TextProperty& other) const
    {
        return key == other.key && value == other.value;
    }
};

struct DEVEX_API TextSection
{
    std::string type;
    // The key=value pairs of the header line.
    std::vector<TextProperty> attributes;
    // The key = value lines that follow the header.
    std::vector<TextProperty> properties;
    std::uint32_t line = 0;

    [[nodiscard]] const TextValue* findAttribute(std::string_view key) const noexcept;
    [[nodiscard]] const TextValue* findProperty(std::string_view key) const noexcept;
};

struct DEVEX_API TextDocument
{
    std::vector<TextSection> sections;
};

// Errors name the line and column of the problem.
[[nodiscard]] DEVEX_API core::Result<TextDocument> parseText(std::string_view source);

// One value on its own, as formatValue wrote it. Nothing when the text is not a value.
[[nodiscard]] DEVEX_API std::optional<TextValue> parseValue(std::string_view text);

[[nodiscard]] DEVEX_API std::string writeText(const TextDocument& document);

} // namespace devex::serialization
