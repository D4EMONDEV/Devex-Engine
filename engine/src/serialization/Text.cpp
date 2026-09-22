#include <devex/core/Assert.hpp>
#include <devex/serialization/Text.hpp>

#include <charconv>
#include <cmath>
#include <format>
#include <iterator>
#include <system_error>
#include <type_traits>

namespace devex::serialization {
namespace {

[[nodiscard]] bool isIdentifierStart(char character) noexcept
{
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
           character == '_';
}

[[nodiscard]] bool isIdentifierPart(char character) noexcept
{
    return isIdentifierStart(character) || (character >= '0' && character <= '9');
}

[[nodiscard]] bool isNumberPart(char character) noexcept
{
    return (character >= '0' && character <= '9') || character == '-' || character == '+' ||
           character == '.' || character == 'e' || character == 'E';
}

class Parser
{
public:
    explicit Parser(std::string_view source) noexcept
        : m_source(source)
    {
    }

    [[nodiscard]] core::Result<TextDocument> parse()
    {
        TextDocument document;
        std::size_t lineStart = 0;
        while (lineStart <= m_source.size())
        {
            std::size_t lineEnd = m_source.find('\n', lineStart);
            if (lineEnd == std::string_view::npos)
            {
                lineEnd = m_source.size();
            }
            m_line = m_source.substr(lineStart, lineEnd - lineStart);
            if (!m_line.empty() && m_line.back() == '\r')
            {
                m_line.remove_suffix(1);
            }
            m_column = 0;
            ++m_lineNumber;

            if (core::Result<void> parsed = parseLine(document); !parsed)
            {
                return std::unexpected(parsed.error());
            }
            lineStart = lineEnd + 1;
        }
        return document;
    }

private:
    [[nodiscard]] std::unexpected<core::Error> error(std::string_view message) const
    {
        return core::makeError(core::ErrorCode::Parse, "line {}, column {}: {}", m_lineNumber,
                               m_column + 1, message);
    }

    [[nodiscard]] char peek() const noexcept
    {
        return m_column < m_line.size() ? m_line[m_column] : '\0';
    }

    void skipSpaces() noexcept
    {
        while (m_column < m_line.size() && (m_line[m_column] == ' ' || m_line[m_column] == '\t'))
        {
            ++m_column;
        }
    }

    // True at the end of the line or at the start of a comment.
    [[nodiscard]] bool atLineEnd() noexcept
    {
        skipSpaces();
        return m_column >= m_line.size() || m_line[m_column] == '#';
    }

    [[nodiscard]] std::string_view parseIdentifier() noexcept
    {
        const std::size_t start = m_column;
        if (m_column < m_line.size() && isIdentifierStart(m_line[m_column]))
        {
            ++m_column;
            while (m_column < m_line.size() && isIdentifierPart(m_line[m_column]))
            {
                ++m_column;
            }
        }
        return m_line.substr(start, m_column - start);
    }

    [[nodiscard]] core::Result<void> parseLine(TextDocument& document)
    {
        if (atLineEnd())
        {
            return {};
        }
        if (peek() == '[')
        {
            return parseSectionHeader(document);
        }
        if (document.sections.empty())
        {
            return error("a property must follow a section header");
        }

        const std::uint32_t line = m_lineNumber;
        const std::string_view key = parseIdentifier();
        if (key.empty())
        {
            return error("expected a property name");
        }
        skipSpaces();
        if (peek() != '=')
        {
            return error("expected '=' after the property name");
        }
        ++m_column;
        skipSpaces();
        core::Result<TextValue> value = parseValue();
        if (!value)
        {
            return std::unexpected(value.error());
        }
        if (!atLineEnd())
        {
            return error("unexpected text after the value");
        }
        document.sections.back().properties.push_back(
            {std::string(key), std::move(*value), line});
        return {};
    }

    [[nodiscard]] core::Result<void> parseSectionHeader(TextDocument& document)
    {
        TextSection section;
        section.line = m_lineNumber;
        ++m_column;
        skipSpaces();
        section.type = std::string(parseIdentifier());
        if (section.type.empty())
        {
            return error("expected a section type after '['");
        }

        while (true)
        {
            skipSpaces();
            if (peek() == ']')
            {
                ++m_column;
                break;
            }
            const std::string_view key = parseIdentifier();
            if (key.empty())
            {
                return error("expected an attribute name or ']'");
            }
            if (peek() != '=')
            {
                return error("expected '=' directly after the attribute name");
            }
            ++m_column;
            core::Result<TextValue> value = parseValue();
            if (!value)
            {
                return std::unexpected(value.error());
            }
            section.attributes.push_back({std::string(key), std::move(*value), m_lineNumber});
        }

        if (!atLineEnd())
        {
            return error("unexpected text after ']'");
        }
        document.sections.push_back(std::move(section));
        return {};
    }

    [[nodiscard]] core::Result<TextValue> parseValue()
    {
        const char first = peek();
        if (first == '"')
        {
            return parseString();
        }
        if ((first >= '0' && first <= '9') || first == '-' || first == '+' || first == '.')
        {
            return parseNumber();
        }
        if (isIdentifierStart(first))
        {
            const std::size_t start = m_column;
            const std::string_view name = parseIdentifier();
            if (peek() != '(')
            {
                if (name == "true" || name == "false")
                {
                    return TextValue(name == "true");
                }
                m_column = start;
                return error(std::format("unknown value '{}'", name));
            }
            return parseCall(name);
        }
        return error("expected a value");
    }

    [[nodiscard]] core::Result<TextValue> parseCall(std::string_view name)
    {
        TextCall call{std::string(name), {}};
        ++m_column; // '('
        skipSpaces();
        if (peek() == ')')
        {
            ++m_column;
            return TextValue(std::move(call));
        }
        while (true)
        {
            skipSpaces();
            core::Result<TextValue> argument = parseValue();
            if (!argument)
            {
                return argument;
            }
            call.arguments.push_back(std::move(*argument));
            skipSpaces();
            if (peek() == ',')
            {
                ++m_column;
                continue;
            }
            if (peek() == ')')
            {
                ++m_column;
                return TextValue(std::move(call));
            }
            return error(std::format("expected ',' or ')' in {}(...)", name));
        }
    }

    [[nodiscard]] core::Result<TextValue> parseString()
    {
        std::string text;
        ++m_column; // opening quote
        while (m_column < m_line.size())
        {
            const char character = m_line[m_column++];
            if (character == '"')
            {
                return TextValue(std::move(text));
            }
            if (character != '\\')
            {
                text.push_back(character);
                continue;
            }
            switch (m_column < m_line.size() ? m_line[m_column++] : '\0')
            {
            case '"':
                text.push_back('"');
                break;
            case '\\':
                text.push_back('\\');
                break;
            case 'n':
                text.push_back('\n');
                break;
            case 'r':
                text.push_back('\r');
                break;
            case 't':
                text.push_back('\t');
                break;
            default:
                --m_column;
                return error("unknown escape sequence in string");
            }
        }
        return error("unterminated string");
    }

    [[nodiscard]] core::Result<TextValue> parseNumber()
    {
        const std::size_t start = m_column;
        bool isReal = false;
        while (m_column < m_line.size() && isNumberPart(m_line[m_column]))
        {
            const char character = m_line[m_column];
            isReal = isReal || character == '.' || character == 'e' || character == 'E';
            ++m_column;
        }
        std::string_view text = m_line.substr(start, m_column - start);
        // from_chars rejects a leading plus sign.
        if (text.starts_with('+'))
        {
            text.remove_prefix(1);
        }

        const char* const end = text.data() + text.size();
        if (isReal)
        {
            double number = 0.0;
            const auto [pointer, status] = std::from_chars(text.data(), end, number);
            if (status == std::errc() && pointer == end)
            {
                return TextValue(number);
            }
        }
        else
        {
            std::int64_t number = 0;
            const auto [pointer, status] = std::from_chars(text.data(), end, number);
            if (status == std::errc() && pointer == end)
            {
                return TextValue(number);
            }
        }
        m_column = start;
        return error(std::format("invalid number '{}'", m_line.substr(start, text.size())));
    }

    std::string_view m_source;
    std::string_view m_line;
    std::size_t m_column = 0;
    std::uint32_t m_lineNumber = 0;
};

void appendString(std::string& output, std::string_view text)
{
    output.push_back('"');
    for (const char character : text)
    {
        switch (character)
        {
        case '"':
            output += "\\\"";
            break;
        case '\\':
            output += "\\\\";
            break;
        case '\n':
            output += "\\n";
            break;
        case '\r':
            output += "\\r";
            break;
        case '\t':
            output += "\\t";
            break;
        default:
            output.push_back(character);
        }
    }
    output.push_back('"');
}

void appendValue(std::string& output, const TextValue& value)
{
    std::visit(
        [&output](const auto& alternative) {
            using Alternative = std::remove_cvref_t<decltype(alternative)>;
            if constexpr (std::is_same_v<Alternative, bool>)
            {
                output += alternative ? "true" : "false";
            }
            else if constexpr (std::is_same_v<Alternative, std::int64_t>)
            {
                std::format_to(std::back_inserter(output), "{}", alternative);
            }
            else if constexpr (std::is_same_v<Alternative, double>)
            {
                DEVEX_ASSERT_MSG(std::isfinite(alternative), "the text format has no infinities");
                std::format_to(std::back_inserter(output), "{}", alternative);
            }
            else if constexpr (std::is_same_v<Alternative, std::string>)
            {
                appendString(output, alternative);
            }
            else
            {
                const TextCall& call = alternative;
                output += call.name;
                output.push_back('(');
                for (std::size_t index = 0; index < call.arguments.size(); ++index)
                {
                    if (index > 0)
                    {
                        output += ", ";
                    }
                    appendValue(output, call.arguments[index]);
                }
                output.push_back(')');
            }
        },
        static_cast<const TextValue::variant&>(value));
}

[[nodiscard]] const TextValue* findByKey(const std::vector<TextProperty>& properties,
                                         std::string_view key) noexcept
{
    for (const TextProperty& property : properties)
    {
        if (property.key == key)
        {
            return &property.value;
        }
    }
    return nullptr;
}

} // namespace

TextValue makeCall(std::string name, std::vector<TextValue> arguments)
{
    return TextValue(TextCall{std::move(name), std::move(arguments)});
}

std::optional<double> asNumber(const TextValue& value) noexcept
{
    if (const auto* real = std::get_if<double>(&value))
    {
        return *real;
    }
    if (const auto* integer = std::get_if<std::int64_t>(&value))
    {
        return static_cast<double>(*integer);
    }
    return std::nullopt;
}

std::optional<std::int64_t> asInteger(const TextValue& value) noexcept
{
    if (const auto* integer = std::get_if<std::int64_t>(&value))
    {
        return *integer;
    }
    return std::nullopt;
}

std::optional<bool> asBool(const TextValue& value) noexcept
{
    if (const auto* boolean = std::get_if<bool>(&value))
    {
        return *boolean;
    }
    return std::nullopt;
}

const std::string* asString(const TextValue& value) noexcept
{
    return std::get_if<std::string>(&value);
}

const TextCall* asCall(const TextValue& value, std::string_view name) noexcept
{
    const auto* call = std::get_if<TextCall>(&value);
    return call != nullptr && call->name == name ? call : nullptr;
}

std::string formatValue(const TextValue& value)
{
    std::string output;
    appendValue(output, value);
    return output;
}

const TextValue* TextSection::findAttribute(std::string_view key) const noexcept
{
    return findByKey(attributes, key);
}

const TextValue* TextSection::findProperty(std::string_view key) const noexcept
{
    return findByKey(properties, key);
}

core::Result<TextDocument> parseText(std::string_view source)
{
    return Parser(source).parse();
}

std::optional<TextValue> parseValue(std::string_view text)
{
    // A value alone is read as the one property of a document of one section.
    const core::Result<TextDocument> document =
        parseText("[value]\nv = " + std::string(text) + "\n");
    if (!document || document->sections.size() != 1 ||
        document->sections.front().properties.size() != 1)
    {
        return std::nullopt;
    }
    return document->sections.front().properties.front().value;
}

std::string writeText(const TextDocument& document)
{
    std::string output;
    for (const TextSection& section : document.sections)
    {
        if (!output.empty())
        {
            output.push_back('\n');
        }
        output.push_back('[');
        output += section.type;
        for (const TextProperty& attribute : section.attributes)
        {
            output.push_back(' ');
            output += attribute.key;
            output.push_back('=');
            appendValue(output, attribute.value);
        }
        output += "]\n";
        for (const TextProperty& property : section.properties)
        {
            output += property.key;
            output += " = ";
            appendValue(output, property.value);
            output.push_back('\n');
        }
    }
    return output;
}

} // namespace devex::serialization
