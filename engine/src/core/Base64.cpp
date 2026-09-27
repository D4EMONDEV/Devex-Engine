#include <devex/core/Base64.hpp>

#include <algorithm>
#include <array>
#include <cstdint>

namespace devex::core {
namespace {

constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// The value of each character, or -1 for those outside the alphabet.
constexpr std::array<std::int8_t, 256> values = [] {
    std::array<std::int8_t, 256> table{};
    table.fill(-1);
    for (std::size_t index = 0; index < alphabet.size(); ++index)
    {
        table[static_cast<unsigned char>(alphabet[index])] = static_cast<std::int8_t>(index);
    }
    return table;
}();

} // namespace

std::string encodeBase64(std::span<const std::byte> bytes)
{
    std::string text;
    text.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t index = 0; index < bytes.size(); index += 3)
    {
        const std::size_t count = std::min<std::size_t>(3, bytes.size() - index);
        std::uint32_t group = 0;
        for (std::size_t offset = 0; offset < 3; ++offset)
        {
            group = (group << 8) | (offset < count ? std::to_integer<std::uint32_t>(bytes[index + offset]) : 0u);
        }
        for (std::size_t character = 0; character < 4; ++character)
        {
            text.push_back(character <= count ? alphabet[(group >> (18 - 6 * character)) & 0x3F] : '=');
        }
    }
    return text;
}

std::optional<std::vector<std::byte>> decodeBase64(std::string_view text)
{
    if (text.size() % 4 != 0)
    {
        return std::nullopt;
    }
    std::vector<std::byte> bytes;
    bytes.reserve(text.size() / 4 * 3);
    for (std::size_t index = 0; index < text.size(); index += 4)
    {
        std::uint32_t group = 0;
        std::size_t padding = 0;
        for (std::size_t offset = 0; offset < 4; ++offset)
        {
            const char character = text[index + offset];
            // Padding only ends the text, after two characters at least.
            if (character == '=' && index + 4 == text.size() && offset >= 2)
            {
                ++padding;
                group <<= 6;
                continue;
            }
            const std::int8_t value = values[static_cast<unsigned char>(character)];
            if (value < 0 || padding > 0)
            {
                return std::nullopt;
            }
            group = (group << 6) | static_cast<std::uint32_t>(value);
        }
        for (std::size_t offset = 0; offset < 3 - padding; ++offset)
        {
            bytes.push_back(static_cast<std::byte>((group >> (16 - 8 * offset)) & 0xFF));
        }
    }
    return bytes;
}

} // namespace devex::core
